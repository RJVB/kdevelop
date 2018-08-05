/* This  is part of KDevelop
    Copyright 2006-2008 Hamish Rodda <rodda@kde.org>
    Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "duchain.h"
#include "duchainlock.h"

#include <QApplication>
#include <QHash>
#include <QMultiMap>
#include <QReadWriteLock>
#include <QAtomicInt>
#include <QThread>
#include <QMutex>
#include <QTimer>

#include <interfaces/idocumentcontroller.h>
#include <interfaces/icore.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/isession.h>

#include "../interfaces/ilanguagesupport.h"
#include "../interfaces/icodehighlighting.h"
#include "../backgroundparser/backgroundparser.h"
#include <debug.h>

#include "language-features.h"
#include "topducontext.h"
#include "topducontextdata.h"
#include "topducontextdynamicdata.h"
#include "parsingenvironment.h"
#include "declaration.h"
#include "definitions.h"
#include "duchainutils.h"
#include "use.h"
#include "uses.h"
#include "abstractfunctiondeclaration.h"
#include "duchainregister.h"
#include "persistentsymboltable.h"
#include "serialization/itemrepository.h"
#include "waitforupdate.h"
#include "importers.h"

#if HAVE_MALLOC_TRIM
#include "malloc.h"
#endif

namespace {
//Additional "soft" cleanup steps that are done before the actual cleanup.
//During "soft" cleanup, the consistency is not guaranteed. The repository is
//marked to be updating during soft cleanup, so if kdevelop crashes, it will be cleared.
//The big advantage of the soft cleanup steps is, that the duchain is always only locked for
//short times, which leads to no lockup in the UI.
const int SOFT_CLEANUP_STEPS = 1;

// seconds to wait before trying to cleanup the DUChain
const uint cleanupEverySeconds = 200;

///Approximate maximum count of top-contexts that are checked during final cleanup
const uint maxFinalCleanupCheckContexts = 2000;
const uint minimumFinalCleanupCheckContextsPercentage = 10; //Check at least n% of all top-contexts during cleanup
//Set to true as soon as the duchain is deleted
}

namespace KDevelop
{
bool DUChain::m_deleted = false;

///Must be locked through KDevelop::SpinLock before using chainsByIndex
///This lock should be locked only for very short times
QMutex DUChain::chainsByIndexLock;
std::vector<TopDUContext*> DUChain::chainsByIndex;

//This thing is not actually used, but it's needed for compiling
DEFINE_LIST_MEMBER_HASH(EnvironmentInformationListItem, items, uint)

//An entry for the item-repository that holds some meta-data. Behind this entry, the actual ParsingEnvironmentFileData is stored.
class EnvironmentInformationItem {
  public:
  EnvironmentInformationItem(uint topContext, uint size) : m_topContext(topContext), m_size(size) {
  }

  ~EnvironmentInformationItem() {
  }

  unsigned int hash() const {
    return m_topContext;
  }

  unsigned int itemSize() const {
    return sizeof(*this) + m_size;
  }

  uint m_topContext;
  uint m_size;//Size of the data behind, that holds the actual item
};

struct ItemRepositoryIndexHash
{
  uint
  operator()(unsigned int __x) const
  { return 173*(__x>>2) + 11 * (__x >> 16); }
};


class EnvironmentInformationRequest {
  public:

  ///This constructor should only be used for lookup
  EnvironmentInformationRequest(uint topContextIndex) : m_file(nullptr), m_index(topContextIndex) {
  }

  EnvironmentInformationRequest(const ParsingEnvironmentFile* file) : m_file(file), m_index(file->indexedTopContext().index()) {
  }

  enum {
    AverageSize = 32 //This should be the approximate average size of an Item
  };

  unsigned int hash() const {
    return m_index;
  }

  uint itemSize() const {
    return sizeof(EnvironmentInformationItem) + DUChainItemSystem::self().dynamicSize(*m_file->d_func());
  }

  void createItem(EnvironmentInformationItem* item) const {
    new (item) EnvironmentInformationItem(m_index, DUChainItemSystem::self().dynamicSize(*m_file->d_func()));
    Q_ASSERT(m_file->d_func()->m_dynamic);
    DUChainBaseData* data = reinterpret_cast<DUChainBaseData*>(reinterpret_cast<char*>(item) + sizeof(EnvironmentInformationItem));
    DUChainItemSystem::self().copy(*m_file->d_func(), *data, true);
    Q_ASSERT(data->m_range == m_file->d_func()->m_range);
    Q_ASSERT(data->classId == m_file->d_func()->classId);
    Q_ASSERT(data->m_dynamic == false);
  }

  static void destroy(EnvironmentInformationItem* item, KDevelop::AbstractItemRepository&) {
    item->~EnvironmentInformationItem();
    //We don't need to call the destructor, because that's done in DUChainBase::makeDynamic()
    //We just need to make sure that every environment-file is dynamic when it's deleted
//     DUChainItemSystem::self().callDestructor((DUChainBaseData*)(((char*)item) + sizeof(EnvironmentInformationItem)));
  }

  static bool persistent(const EnvironmentInformationItem* ) {
    //Cleanup done separately
    return true;
  }

  bool equals(const EnvironmentInformationItem* item) const {
    return m_index == item->m_topContext;
  }

  const ParsingEnvironmentFile* m_file;
  uint m_index;
};

///A list of environment-information/top-contexts mapped to a file-name
class EnvironmentInformationListItem {
  public:
  EnvironmentInformationListItem() {
    initializeAppendedLists(true);
  }

  EnvironmentInformationListItem(const EnvironmentInformationListItem& rhs, bool dynamic = true) {
    initializeAppendedLists(dynamic);
    m_file = rhs.m_file;
    copyListsFrom(rhs);
  }

  ~EnvironmentInformationListItem() {
    freeAppendedLists();
  }

  unsigned int hash() const {
    //We only compare the declaration. This allows us implementing a map, although the item-repository
    //originally represents a set.
    return m_file.hash();
  }

  unsigned short int itemSize() const {
    return dynamicSize();
  }

  IndexedString m_file;

  uint classSize() const {
    return sizeof(*this);
  }

  START_APPENDED_LISTS(EnvironmentInformationListItem);
  ///Contains the index of each contained environment-item
  APPENDED_LIST_FIRST(EnvironmentInformationListItem, uint, items);
  END_APPENDED_LISTS(EnvironmentInformationListItem, items);
};

class EnvironmentInformationListRequest {
  public:

  ///This constructor should only be used for lookup
  EnvironmentInformationListRequest(const IndexedString& file) : m_file(file), m_item(nullptr) {
  }
  ///This is used to actually construct the information in the repository
  EnvironmentInformationListRequest(const IndexedString& file, const EnvironmentInformationListItem& item) : m_file(file), m_item(&item) {
  }

  enum {
    AverageSize = 160 //This should be the approximate average size of an Item
  };

  unsigned int hash() const {
    return m_file.hash();
  }

  uint itemSize() const {
    return m_item->itemSize();
  }

  void createItem(EnvironmentInformationListItem* item) const {
    Q_ASSERT(m_item->m_file == m_file);
    new (item) EnvironmentInformationListItem(*m_item, false);
  }

  static void destroy(EnvironmentInformationListItem* item, KDevelop::AbstractItemRepository&) {
    item->~EnvironmentInformationListItem();
  }

  static bool persistent(const EnvironmentInformationListItem*) {
    //Cleanup is done separately
    return true;
  }

  bool equals(const EnvironmentInformationListItem* item) const {
    return m_file == item->m_file;
  }

  IndexedString m_file;
  const EnvironmentInformationListItem* m_item;
};

class DUChainPrivate;
static DUChainPrivate* duChainPrivateSelf = nullptr;
class DUChainPrivate
{
  class CleanupThread : public QThread {
    public:
      explicit CleanupThread(DUChainPrivate* data)
        : m_data(data)
      {
      }

      void stopThread()
      {
        quit();
        wait();
      }

    private:
      void run() override {
        QTimer timer;
        connect(&timer, &QTimer::timeout, [this]() {
          //Just to make sure the cache is cleared periodically
          ModificationRevisionSet::clearCache();

          m_data->doMoreCleanup(SOFT_CLEANUP_STEPS, TryLock);
        });
        timer.start(cleanupEverySeconds * 1000);
        exec();
      }
      DUChainPrivate* m_data;
  };
public:
  DUChainPrivate() : m_chainsMutex(QMutex::Recursive), m_cleanupMutex(QMutex::Recursive), instance(nullptr), m_cleanupDisabled(false), m_destroyed(false), m_environmentListInfo(QStringLiteral("Environment Lists")), m_environmentInfo(QStringLiteral("Environment Information"))
  {
#if defined(TEST_NO_CLEANUP)
    m_cleanupDisabled = true;
#endif

    duChainPrivateSelf = this;
    qRegisterMetaType<DUChainBasePointer>("KDevelop::DUChainBasePointer");
    qRegisterMetaType<DUContextPointer>("KDevelop::DUContextPointer");
    qRegisterMetaType<TopDUContextPointer>("KDevelop::TopDUContextPointer");
    qRegisterMetaType<DeclarationPointer>("KDevelop::DeclarationPointer");
    qRegisterMetaType<FunctionDeclarationPointer>("KDevelop::FunctionDeclarationPointer");
    qRegisterMetaType<KDevelop::IndexedString>("KDevelop::IndexedString");
    qRegisterMetaType<KDevelop::IndexedTopDUContext>("KDevelop::IndexedTopDUContext");
    qRegisterMetaType<KDevelop::ReferencedTopDUContext>("KDevelop::ReferencedTopDUContext");

    instance = new DUChain();
    m_cleanup = new CleanupThread(this);
    m_cleanup->start();

    DUChain::m_deleted = false;

    ///Loading of some static data:
    {
      ///@todo Solve this more duchain-like
      QFile f(globalItemRepositoryRegistry().path() + "/parsing_environment_data");
      bool opened = f.open(QIODevice::ReadOnly);
      ///FIXME: ugh, so ugly
      ParsingEnvironmentFile::m_staticData = reinterpret_cast<StaticParsingEnvironmentData*>( new char[sizeof(StaticParsingEnvironmentData)]);
      if(opened) {
        qCDebug(LANGUAGE) << "reading parsing-environment static data";
        //Read
        f.read((char*)ParsingEnvironmentFile::m_staticData, sizeof(StaticParsingEnvironmentData));
      }else{
        qCDebug(LANGUAGE) << "creating new parsing-environment static data";
        //Initialize
        new (ParsingEnvironmentFile::m_staticData) StaticParsingEnvironmentData();
      }
    }

    ///Read in the list of available top-context indices
    {
      QFile f(globalItemRepositoryRegistry().path() + "/available_top_context_indices");
      bool opened = f.open(QIODevice::ReadOnly);
      if(opened)
      {
        Q_ASSERT( (f.size() % sizeof(uint)) == 0);
        m_availableTopContextIndices.resize(f.size()/(int)sizeof(uint));
        f.read((char*)m_availableTopContextIndices.data(), f.size());
      }
    }
  }
  ~DUChainPrivate() {
    qCDebug(LANGUAGE) << "Destroying";
    DUChain::m_deleted = true;
    m_cleanup->stopThread();
    delete m_cleanup;
    delete instance;
  }

  void clear() {

    if(!m_cleanupDisabled)
      doMoreCleanup();

    DUChainWriteLocker writeLock(DUChain::lock());

    QMutexLocker l(&m_chainsMutex);

    foreach(TopDUContext* top, m_chainsByUrl)
      removeDocumentChainFromMemory(top);

    m_indexEnvironmentInformations.clear();
    m_fileEnvironmentInformations.clear();

    Q_ASSERT(m_fileEnvironmentInformations.isEmpty());
    Q_ASSERT(m_chainsByUrl.isEmpty());
  }

  ///DUChain must be write-locked
  ///Also removes from the environment-manager if the top-context is not on disk
  void removeDocumentChainFromMemory(TopDUContext* context) {
    QMutexLocker l(&m_chainsMutex);

    {
      QMutexLocker l(&m_referenceCountsMutex);

      if(m_referenceCounts.contains(context)) {
      //This happens during shutdown, since everything is unloaded
      qCDebug(LANGUAGE) << "removed a top-context that was reference-counted:" << context->url().str() << context->ownIndex();
      m_referenceCounts.remove(context);
      }
    }

    uint index = context->ownIndex();

    // qCDebug(LANGUAGE) << "duchain: removing document" << context->url().str();
    Q_ASSERT(hasChainForIndex(index));
    Q_ASSERT(m_chainsByUrl.contains(context->url(), context));

    m_chainsByUrl.remove(context->url(), context);

    if(!context->isOnDisk())
      instance->removeFromEnvironmentManager(context);

    l.unlock();
    //DUChain is write-locked, so we can do whatever we want on the top-context, including deleting it
    context->deleteSelf();
    l.relock();

    Q_ASSERT(hasChainForIndex(index));

    QMutexLocker lock(&DUChain::chainsByIndexLock);
    DUChain::chainsByIndex[index] = nullptr;
  }

  ///Must be locked before accessing content of this class.
  ///Should be released during expensive disk-operations and such.
  QMutex m_chainsMutex;

  QMutex m_cleanupMutex;

  CleanupThread* m_cleanup;

  DUChain* instance;
  DUChainLock lock;
  QMultiMap<IndexedString, TopDUContext*> m_chainsByUrl;

  //Must be locked before accessing m_referenceCounts
  QMutex m_referenceCountsMutex;
  QHash<TopDUContext*, uint> m_referenceCounts;

  Definitions m_definitions;
  Uses m_uses;
  QSet<uint> m_loading;
  bool m_cleanupDisabled;

  //List of available top-context indices, protected by m_chainsMutex
  QVector<uint> m_availableTopContextIndices;

  ///Used to keep alive the top-context that belong to documents loaded in the editor
  QSet<ReferencedTopDUContext> m_openDocumentContexts;

  bool m_destroyed;

  ///The item must not be stored yet
  ///m_chainsMutex should not be locked, since this can trigger I/O
  void addEnvironmentInformation(ParsingEnvironmentFilePointer info) {
    Q_ASSERT(!findInformation(info->indexedTopContext().index()));
    Q_ASSERT(m_environmentInfo.findIndex(info->indexedTopContext().index()) == 0);

    QMutexLocker lock(&m_chainsMutex);
    m_fileEnvironmentInformations.insert(info->url(), info);

    m_indexEnvironmentInformations.insert(info->indexedTopContext().index(), info);

    Q_ASSERT(info->d_func()->classId);
  }

  ///The item must be managed currently
  ///m_chainsMutex does not need to be locked
  void removeEnvironmentInformation(ParsingEnvironmentFilePointer info) {

    info->makeDynamic(); //By doing this, we make sure the data is actually being destroyed in the destructor

    bool removed = false;
    bool removed2 = false;
    {
      QMutexLocker lock(&m_chainsMutex);
      removed = m_fileEnvironmentInformations.remove(info->url(), info);
      removed2 = m_indexEnvironmentInformations.remove(info->indexedTopContext().index());
    }

    {
      //Remove it from the environment information lists if it was there
      QMutexLocker lock(m_environmentListInfo.mutex());
      uint index = m_environmentListInfo.findIndex(info->url());

      if(index) {
        EnvironmentInformationListItem item(*m_environmentListInfo.itemFromIndex(index));
        if(item.itemsList().removeOne(info->indexedTopContext().index())) {
          m_environmentListInfo.deleteItem(index);
          if(!item.itemsList().isEmpty())
            m_environmentListInfo.index(EnvironmentInformationListRequest(info->url(), item));
        }
      }
    }

    QMutexLocker lock(m_environmentInfo.mutex());
    uint index = m_environmentInfo.findIndex(info->indexedTopContext().index());
    if(index) {
      m_environmentInfo.deleteItem(index);
    }

    Q_UNUSED(removed);
    Q_UNUSED(removed2);
    Q_ASSERT(index || (removed && removed2));
    Q_ASSERT(!findInformation(info->indexedTopContext().index()));
  }

  ///m_chainsMutex should _not_ be locked, because this may trigger I/O
  QList<ParsingEnvironmentFilePointer> getEnvironmentInformation(const IndexedString& url) {
    QList<ParsingEnvironmentFilePointer> ret;
    uint listIndex = m_environmentListInfo.findIndex(url);

    if(listIndex) {
      KDevVarLengthArray<uint> topContextIndices;

      {
        //First store all the possible intices into the KDevVarLengthArray, so we can unlock the mutex before processing them.

        QMutexLocker lock(m_environmentListInfo.mutex()); //Lock the mutex to make sure the item isn't changed while it's being iterated
        const EnvironmentInformationListItem* item = m_environmentListInfo.itemFromIndex(listIndex);
        FOREACH_FUNCTION(uint topContextIndex, item->items)
          topContextIndices << topContextIndex;
      }

      //Process the indices in a separate step after copying them from the array, so we don't need m_environmentListInfo.mutex locked,
      //and can call loadInformation(..) safely, which else might lead to a deadlock.
      foreach (uint topContextIndex, topContextIndices) {
        QExplicitlySharedDataPointer< ParsingEnvironmentFile > p = ParsingEnvironmentFilePointer(loadInformation(topContextIndex));
        if(p) {
         ret << p;
        }else{
          qCDebug(LANGUAGE) << "Failed to load enviromment-information for" << TopDUContextDynamicData::loadUrl(topContextIndex).str();
        }
      }
    }

    QMutexLocker l(&m_chainsMutex);

    //Add those information that have not been added to the stored lists yet
    foreach(const ParsingEnvironmentFilePointer& file, m_fileEnvironmentInformations.values(url))
      if(!ret.contains(file))
        ret << file;

    return ret;
  }

  ///Must be called _without_ the chainsByIndex spin-lock locked
  static inline bool hasChainForIndex(uint index) {
    QMutexLocker lock(&DUChain::chainsByIndexLock);
    return (DUChain::chainsByIndex.size() > index) && DUChain::chainsByIndex[index];
  }

  ///Must be called _without_ the chainsByIndex spin-lock locked. Returns the top-context if it is loaded.
  static inline TopDUContext* readChainForIndex(uint index) {
    QMutexLocker lock(&DUChain::chainsByIndexLock);
    if(DUChain::chainsByIndex.size() > index)
      return DUChain::chainsByIndex[index];
    else
      return nullptr;
  }

  ///Makes sure that the chain with the given index is loaded
  ///@warning m_chainsMutex must NOT be locked when this is called
  void loadChain(uint index, QSet<uint>& loaded) {

    QMutexLocker l(&m_chainsMutex);

    if(!hasChainForIndex(index)) {

      if(m_loading.contains(index)) {
        //It's probably being loaded by another thread. So wait until the load is ready
        while(m_loading.contains(index)) {
          l.unlock();
          qCDebug(LANGUAGE) << "waiting for another thread to load index" << index;
          QThread::usleep(50000);
          l.relock();
        }
        loaded.insert(index);
        return;
      }
      m_loading.insert(index);
      loaded.insert(index);

      l.unlock();
      qCDebug(LANGUAGE) << "loading top-context" << index;
      TopDUContext* chain = TopDUContextDynamicData::load(index);
      if(chain) {
        chain->setParsingEnvironmentFile(loadInformation(chain->ownIndex()));

        if(!chain->usingImportsCache()) {
          //Eventually also load all the imported chains, so the import-structure is built
          foreach(const DUContext::Import &import, chain->DUContext::importedParentContexts()) {
            if(!loaded.contains(import.topContextIndex())) {
              loadChain(import.topContextIndex(), loaded);
            }
          }
         }
        chain->rebuildDynamicImportStructure();

        chain->setInDuChain(true);
        instance->addDocumentChain(chain);
      }

      l.relock();
      m_loading.remove(index);
    }
  }


  ///Stores all environment-information
  ///Also makes sure that all information that stays is referenced, so it stays alive.
  ///@param atomic If this is false, the write-lock will be released time by time
  void storeAllInformation(bool atomic, DUChainWriteLocker& locker) {

    uint cnt = 0;

    QList<IndexedString> urls;
    {
      QMutexLocker lock(&m_chainsMutex);
      urls += m_fileEnvironmentInformations.keys();
    }

    foreach(const IndexedString &url, urls) {
      QList<ParsingEnvironmentFilePointer> check;
      {
        QMutexLocker lock(&m_chainsMutex);
        check = m_fileEnvironmentInformations.values(url);
      }

      foreach(ParsingEnvironmentFilePointer file, check) {

        EnvironmentInformationRequest req(file.data());
        QMutexLocker lock(m_environmentInfo.mutex());
        uint index = m_environmentInfo.findIndex(req);

        if(file->d_func()->isDynamic()) {
          //This item has been changed, or isn't in the repository yet

          //Eventually remove an old entry
          if(index)
            m_environmentInfo.deleteItem(index);

          //Add the new entry to the item repository
          index = m_environmentInfo.index(req);
          Q_ASSERT(index);

          EnvironmentInformationItem* item = const_cast<EnvironmentInformationItem*>(m_environmentInfo.itemFromIndex(index));
          DUChainBaseData* theData = reinterpret_cast<DUChainBaseData*>(reinterpret_cast<char*>(item) + sizeof(EnvironmentInformationItem));

          Q_ASSERT(theData->m_range == file->d_func()->m_range);
          Q_ASSERT(theData->m_dynamic == false);
          Q_ASSERT(theData->classId == file->d_func()->classId);

          file->setData( theData );

          ++cnt;
        }else{
          m_environmentInfo.itemFromIndex(index); //Prevent unloading of the data, by accessing the item
        }
      }

      ///We must not release the lock while holding a reference to a ParsingEnvironmentFilePointer, else we may miss the deletion of an
      ///information, and will get crashes.
      if(!atomic && (cnt % 100 == 0)) {
          //Release the lock on a regular basis
          locker.unlock();
          locker.lock();
      }

      storeInformationList(url);

      //Access the data in the repository, so the bucket isn't unloaded
      uint index = m_environmentListInfo.findIndex(EnvironmentInformationListRequest(url));
      if(index) {
        m_environmentListInfo.itemFromIndex(index);
      }else{
        QMutexLocker lock(&m_chainsMutex);
        qCDebug(LANGUAGE) << "Did not find stored item for" << url.str() << "count:" << m_fileEnvironmentInformations.values(url);
      }
      if(!atomic) {
        locker.unlock();
        locker.lock();
      }
    }
  }

  QMutex& cleanupMutex() {
    return m_cleanupMutex;
  }

  /// defines how we interact with the ongoing language parse jobs
  enum LockFlag {
    /// no locking required, only used when we locked previously
    NoLock = 0,
    /// lock all parse jobs and block until we succeeded. required at shutdown
    BlockingLock = 1,
    /// only try to lock and abort on failure, good for the intermittent cleanups
    TryLock = 2,
  };
  ///@param retries When this is nonzero, then doMoreCleanup will do the specified amount of cycles
  ///doing the cleanup without permanently locking the du-chain. During these steps the consistency
  ///of the disk-storage is not guaranteed, but only few changes will be done during these steps,
  ///so the final step where the duchain is permanently locked is much faster.
  void doMoreCleanup(int retries = 0, LockFlag lockFlag = BlockingLock) {

    if(m_cleanupDisabled)
      return;

    //This mutex makes sure that there's never 2 threads at he same time trying to clean up
    QMutexLocker lockCleanupMutex(&cleanupMutex());

    if(m_destroyed || m_cleanupDisabled)
      return;

    Q_ASSERT(!instance->lock()->currentThreadHasReadLock() && !instance->lock()->currentThreadHasWriteLock());
    DUChainWriteLocker writeLock(instance->lock());

    //This is used to stop all parsing before starting to do the cleanup. This way less happens during the
    //soft cleanups, and we have a good chance that during the "hard" cleanup only few data has to be written.
    QList<QReadWriteLock*> locked;

    if (lockFlag != NoLock) {
      QList<ILanguageSupport*> languages;
      if (ICore* core = ICore::self())
        if (ILanguageController* lc = core->languageController())
          languages = lc->loadedLanguages();

      writeLock.unlock();

      //Here we wait for all parsing-threads to stop their processing
      foreach(const auto language, languages) {
        if (lockFlag == TryLock) {
          if (!language->parseLock() || !language->parseLock()->tryLockForWrite()) {
            qCDebug(LANGUAGE) << "Aborting cleanup because language plugin is still parsing:" << language->name();
            // some language is still parsing, don't interfere with the cleanup
            foreach(auto* lock, locked) {
              lock->unlock();
            }
            return;
          }
        } else {
          language->parseLock()->lockForWrite();
        }
        locked << language->parseLock();
      }

      writeLock.lock();

      globalItemRepositoryRegistry().lockForWriting();
      qCDebug(LANGUAGE) << "starting cleanup";
    }

    QTime startTime = QTime::currentTime();
    PersistentSymbolTable::self().clearCache();

    storeAllInformation(!retries, writeLock); //Puts environment-information into a repository

    //We don't need to increase the reference-count, since the cleanup-mutex is locked
    QSet<TopDUContext*> workOnContexts;

    {
      QMutexLocker l(&m_chainsMutex);

      workOnContexts.reserve(m_chainsByUrl.size());
      foreach(TopDUContext* top, m_chainsByUrl) {
        workOnContexts << top;
        Q_ASSERT(hasChainForIndex(top->ownIndex()));
      }
    }

    foreach(TopDUContext* context, workOnContexts) {

      context->m_dynamicData->store();

      if(retries) {
        //Eventually give other threads a chance to access the duchain
        writeLock.unlock();
        //Sleep to give the other threads a realistic chance to get a read-lock in between
        QThread::usleep(500);
        writeLock.lock();
      }
    }

      //Unload all top-contexts that don't have a reference-count and that are not imported by a referenced one

      QSet<IndexedString> unloadedNames;
      bool unloadedOne = true;

      bool unloadAllUnreferenced = !retries;

      //Now unload contexts, but only ones that are not imported by any other currently loaded context
      //The complication: Since during the lock-break new references may be added, we must never keep
      //the du-chain in an invalid state. Thus we can only unload contexts that are not imported by any
      //currently loaded contexts. In case of loops, we have to unload everything at once.
      while(unloadedOne) {
        unloadedOne = false;
        int hadUnloadable = 0;

        unloadContexts:

        foreach(TopDUContext* unload, workOnContexts) {

          bool hasReference = false;

          {
            QMutexLocker l(&m_referenceCountsMutex);
            //Test if the context is imported by a referenced one
            for (auto it = m_referenceCounts.constBegin(), end = m_referenceCounts.constEnd(); it != end; ++it) {
              auto* context = it.key();
              if(context == unload || context->imports(unload, CursorInRevision())) {
                workOnContexts.remove(unload);
                hasReference = true;
              }
            }
          }

          if(!hasReference)
            ++hadUnloadable; //We have found a context that is not referenced
          else
            continue; //This context is referenced

          bool isImportedByLoaded = !unload->loadedImporters().isEmpty();

          //If we unload a context that is imported by other contexts, we create a bad loaded state
          if(isImportedByLoaded && !unloadAllUnreferenced)
            continue;

          unloadedNames.insert(unload->url());
          //Since we've released the write-lock in between, we've got to call store() again to be sure that none of the data is dynamic
          //If nothing has changed, it is only a low-cost call.
          unload->m_dynamicData->store();
          Q_ASSERT(!unload->d_func()->m_dynamic);
          removeDocumentChainFromMemory(unload);
          workOnContexts.remove(unload);
          unloadedOne = true;

          if(!unloadAllUnreferenced) {
            //Eventually give other threads a chance to access the duchain
            writeLock.unlock();
            //Sleep to give the other threads a realistic chance to get a read-lock in between
            QThread::usleep(500);
            writeLock.lock();
          }
        }
        if(hadUnloadable && !unloadedOne) {
          Q_ASSERT(!unloadAllUnreferenced);
          //This can happen in case of loops. We have o unload everything at one time.
          qCDebug(LANGUAGE) << "found" << hadUnloadable << "unloadable contexts, but could not unload separately. Unloading atomically.";
          unloadAllUnreferenced = true;
          hadUnloadable = 0; //Reset to 0, so we cannot loop forever
          goto unloadContexts;
        }
      }


      if(retries == 0) {
        QMutexLocker lock(&m_chainsMutex);
        //Do this atomically, since we must be sure that _everything_ is already saved
        for(QMultiMap<IndexedString, ParsingEnvironmentFilePointer>::iterator it = m_fileEnvironmentInformations.begin(); it != m_fileEnvironmentInformations.end(); ) {
          ParsingEnvironmentFile* f = it->data();
          Q_ASSERT(f->d_func()->classId);
          if(f->ref.load() == 1) {
            Q_ASSERT(!f->d_func()->isDynamic()); //It cannot be dynamic, since we have stored before
            //The ParsingEnvironmentFilePointer is only referenced once. This means that it does not belong to any
            //loaded top-context, so just remove it to save some memory and processing time.
            ///@todo use some kind of timeout before removing
            it = m_fileEnvironmentInformations.erase(it);
          }else{
            ++it;
          }
        }
      }

      if(retries)
        writeLock.unlock();

      //This must be the last step, due to the on-disk reference counting
      globalItemRepositoryRegistry().store(); //Stores all repositories

      {
        //Store the static parsing-environment file data
        ///@todo Solve this more elegantly, using a general mechanism to store static duchain-like data
        Q_ASSERT(ParsingEnvironmentFile::m_staticData);
        QFile f(globalItemRepositoryRegistry().path() + "/parsing_environment_data");
        bool opened = f.open(QIODevice::WriteOnly);
        Q_ASSERT(opened);
        Q_UNUSED(opened);
        f.write((char*)ParsingEnvironmentFile::m_staticData, sizeof(StaticParsingEnvironmentData));
      }

      ///Write out the list of available top-context indices
      {
        QMutexLocker lock(&m_chainsMutex);

        QFile f(globalItemRepositoryRegistry().path() + "/available_top_context_indices");
        bool opened = f.open(QIODevice::WriteOnly);
        Q_ASSERT(opened);
        Q_UNUSED(opened);

        f.write((char*)m_availableTopContextIndices.data(), m_availableTopContextIndices.size() * sizeof(uint));
      }


      if(retries) {
        doMoreCleanup(retries-1, NoLock);
        writeLock.lock();
      }

      if(lockFlag != NoLock) {
        globalItemRepositoryRegistry().unlockForWriting();

        const auto elapsedMS = startTime.msecsTo(QTime::currentTime());
        qCDebug(LANGUAGE) << "time spent doing cleanup:" << elapsedMS << "ms - top-contexts still open:" << m_chainsByUrl.size() << "- retries" << retries;
      }

      foreach(QReadWriteLock* lock, locked)
        lock->unlock();

#if HAVE_MALLOC_TRIM
    // trim unused memory but keep a pad buffer of about 50 MB
    // this can greatly decrease the perceived memory consumption of kdevelop
    // see: https://sourceware.org/bugzilla/show_bug.cgi?id=14827
    malloc_trim(50 * 1024 * 1024);
#endif
  }

  ///Checks whether the information is already loaded.
  ParsingEnvironmentFile* findInformation(uint topContextIndex) {
    QMutexLocker lock(&m_chainsMutex);
    QHash<uint, ParsingEnvironmentFilePointer>::iterator it = m_indexEnvironmentInformations.find(topContextIndex);
    if(it != m_indexEnvironmentInformations.end())
      return (*it).data();
    return nullptr;
  }

  ///Loads/gets the environment-information for the given top-context index, or returns zero if none exists
  ///@warning m_chainsMutex should NOT be locked when this is called, because it triggers I/O
  ///@warning no other mutexes should be locked, as that may lead to a dedalock
  ParsingEnvironmentFile* loadInformation(uint topContextIndex) {

    ParsingEnvironmentFile* alreadyLoaded = findInformation(topContextIndex);
    if(alreadyLoaded)
      return alreadyLoaded;

    //Step two: Check if it is on disk, and if is, load it
    uint dataIndex = m_environmentInfo.findIndex(EnvironmentInformationRequest(topContextIndex));
    if(!dataIndex) {
      //No environment-information stored for this top-context
      return nullptr;
    }

    const EnvironmentInformationItem& item(*m_environmentInfo.itemFromIndex(dataIndex));

    QMutexLocker lock(&m_chainsMutex);

    //Due to multi-threading, we must do this check after locking the mutex, so we can be sure we don't create the same item twice at the same time
    alreadyLoaded = findInformation(topContextIndex);
    if(alreadyLoaded)
      return alreadyLoaded;

    ///FIXME: ugly, and remove const_cast
    ParsingEnvironmentFile* ret = dynamic_cast<ParsingEnvironmentFile*>(DUChainItemSystem::self().create(
      const_cast<DUChainBaseData*>(reinterpret_cast<const DUChainBaseData*>(reinterpret_cast<const char*>(&item) + sizeof(EnvironmentInformationItem)))
    ));
    if(ret) {
      Q_ASSERT(ret->d_func()->classId);
      Q_ASSERT(ret->indexedTopContext().index() == topContextIndex);
      ParsingEnvironmentFilePointer retPtr(ret);

      m_fileEnvironmentInformations.insert(ret->url(), retPtr);

      Q_ASSERT(!m_indexEnvironmentInformations.contains(ret->indexedTopContext().index()));
      m_indexEnvironmentInformations.insert(ret->indexedTopContext().index(), retPtr);
    }
    return ret;
  }

  struct CleanupListVisitor {
    QList<uint> checkContexts;
    bool operator()(const EnvironmentInformationItem* item) {
      checkContexts << item->m_topContext;
      return true;
    }
  };

  ///Will check a selection of all top-contexts for up-to-date ness, and remove them if out of date
  void cleanupTopContexts() {
    DUChainWriteLocker lock( DUChain::lock() );
    qCDebug(LANGUAGE) << "cleaning top-contexts";
    CleanupListVisitor visitor;
    uint startPos = 0;
    m_environmentInfo.visitAllItems(visitor);

    int checkContextsCount = maxFinalCleanupCheckContexts;
    int percentageOfContexts = (visitor.checkContexts.size() * 100) / minimumFinalCleanupCheckContextsPercentage;

    if(checkContextsCount < percentageOfContexts)
      checkContextsCount = percentageOfContexts;

    if(visitor.checkContexts.size() > (int)checkContextsCount)
      startPos = qrand() % (visitor.checkContexts.size() - checkContextsCount);

    int endPos = startPos + maxFinalCleanupCheckContexts;
    if(endPos > visitor.checkContexts.size())
      endPos = visitor.checkContexts.size();
    QSet< uint > check;
    for(int a = startPos; a < endPos && check.size() < checkContextsCount; ++a)
      if(check.size() < checkContextsCount)
        addContextsForRemoval(check, IndexedTopDUContext(visitor.checkContexts[a]));

    foreach(uint topIndex, check) {
      IndexedTopDUContext top(topIndex);
      if(top.data()) {
        qCDebug(LANGUAGE) << "removing top-context for" << top.data()->url().str() << "because it is out of date";
        instance->removeDocumentChain(top.data());
      }
    }
    qCDebug(LANGUAGE) << "check ready";
  }

private:

  void addContextsForRemoval(QSet<uint>& topContexts, IndexedTopDUContext top) {
    if(topContexts.contains(top.index()))
      return;

    QExplicitlySharedDataPointer<ParsingEnvironmentFile> info( instance->environmentFileForDocument(top) );
    ///@todo Also check if the context is "useful"(Not a duplicate context, imported by a useful one, ...)
    if(info && info->needsUpdate()) {
      //This context will be removed
    }else{
      return;
    }

    topContexts.insert(top.index());

    if(info) {
      //Check whether importers need to be removed as well
      QList< QExplicitlySharedDataPointer<ParsingEnvironmentFile> > importers = info->importers();

      QSet< QExplicitlySharedDataPointer<ParsingEnvironmentFile> > checkNext;

      //Do breadth first search, so less imports/importers have to be loaded, and a lower depth is reached

      for(QList< QExplicitlySharedDataPointer<ParsingEnvironmentFile> >::iterator it = importers.begin(); it != importers.end(); ++it) {
        IndexedTopDUContext c = (*it)->indexedTopContext();
        if(!topContexts.contains(c.index())) {
          topContexts.insert(c.index()); //Prevent useless recursion
          checkNext.insert(*it);
        }
      }

      for(QSet< QExplicitlySharedDataPointer<ParsingEnvironmentFile> >::const_iterator it = checkNext.begin(); it != checkNext.end(); ++it) {
        topContexts.remove((*it)->indexedTopContext().index()); //Enable full check again
        addContextsForRemoval(topContexts, (*it)->indexedTopContext());
      }
    }
  }

  ///Stores the environment-information for the given url
  void storeInformationList(const IndexedString& url) {

    QMutexLocker lock(m_environmentListInfo.mutex());

    EnvironmentInformationListItem newItem;
    newItem.m_file = url;

    QSet<uint> newItems;

    {
      QMutexLocker lock(&m_chainsMutex);
      QMultiMap<IndexedString, ParsingEnvironmentFilePointer>::iterator start = m_fileEnvironmentInformations.lowerBound(url);
      QMultiMap<IndexedString, ParsingEnvironmentFilePointer>::iterator end = m_fileEnvironmentInformations.upperBound(url);

      for(QMultiMap<IndexedString, ParsingEnvironmentFilePointer>::iterator it = start; it != end; ++it) {
        uint topContextIndex = (*it)->indexedTopContext().index();
        newItems.insert(topContextIndex);
        newItem.itemsList().append(topContextIndex);
      }
    }

    uint index = m_environmentListInfo.findIndex(url);

    if(index) {
      //We only handle adding items here, since we can never be sure whether everything is loaded
      //Removal is handled directly in removeEnvironmentInformation

      const EnvironmentInformationListItem* item = m_environmentListInfo.itemFromIndex(index);
      QSet<uint> oldItems;
      FOREACH_FUNCTION(uint topContextIndex, item->items) {
        oldItems.insert(topContextIndex);
        if(!newItems.contains(topContextIndex)) {
          newItems.insert(topContextIndex);
          newItem.itemsList().append(topContextIndex);
        }
      }

      if(oldItems == newItems)
        return;

      ///Update/insert a new list
      m_environmentListInfo.deleteItem(index); //Remove the previous item
    }

    Q_ASSERT(m_environmentListInfo.findIndex(EnvironmentInformationListRequest(url)) == 0);

    //Insert the new item
    m_environmentListInfo.index(EnvironmentInformationListRequest(url, newItem));

    Q_ASSERT(m_environmentListInfo.findIndex(EnvironmentInformationListRequest(url)));
  }

  //Loaded environment information. Protected by m_chainsMutex
  QMultiMap<IndexedString, ParsingEnvironmentFilePointer> m_fileEnvironmentInformations;
  QHash<uint, ParsingEnvironmentFilePointer> m_indexEnvironmentInformations;

  ///The following repositories are thread-safe, and m_chainsMutex should not be locked when using them, because
  ///they may trigger I/O. Still it may be required to lock their local mutexes.
  ///Maps filenames to a list of top-contexts/environment-information.
  ItemRepository<EnvironmentInformationListItem, EnvironmentInformationListRequest> m_environmentListInfo;
  ///Maps top-context-indices to environment-information item.
  ItemRepository<EnvironmentInformationItem, EnvironmentInformationRequest> m_environmentInfo;
};

Q_GLOBAL_STATIC(DUChainPrivate, sdDUChainPrivate)


DUChain::DUChain()
{
  Q_ASSERT(ICore::self());

  connect(ICore::self()->documentController(), &IDocumentController::documentLoadedPrepare, this, &DUChain::documentLoadedPrepare);
  connect(ICore::self()->documentController(), &IDocumentController::documentUrlChanged, this, &DUChain::documentRenamed);
  connect(ICore::self()->documentController(), &IDocumentController::documentActivated, this, &DUChain::documentActivated);
  connect(ICore::self()->documentController(), &IDocumentController::documentClosed, this, &DUChain::documentClosed);
}

DUChain::~DUChain()
{
  DUChain::m_deleted = true;
}

DUChain* DUChain::self()
{
  return sdDUChainPrivate->instance;
}

extern void initModificationRevisionSetRepository();
extern void initDeclarationRepositories();
extern void initIdentifierRepository();
extern void initTypeRepository();
extern void initInstantiationInformationRepository();

void DUChain::initialize()
{
  // Initialize the global item repository as first thing after loading the session
  Q_ASSERT(ICore::self());
  Q_ASSERT(ICore::self()->activeSession());
  ItemRepositoryRegistry::initialize(ICore::self()->activeSessionLock());

  initReferenceCounting();

  // This needs to be initialized here too as the function is not threadsafe, but can
  // sometimes be called from different threads. This results in the underlying QFile
  // being 0 and hence crashes at some point later when accessing the contents via
  // read. See https://bugs.kde.org/show_bug.cgi?id=250779
  RecursiveImportRepository::repository();
  RecursiveImportCacheRepository::repository();

  // similar to above, see https://bugs.kde.org/show_bug.cgi?id=255323
  initDeclarationRepositories();

  initModificationRevisionSetRepository();
  initIdentifierRepository();
  initTypeRepository();
  initInstantiationInformationRepository();

  Importers::self();

  globalImportIdentifier();
  globalIndexedImportIdentifier();
  globalAliasIdentifier();
  globalIndexedAliasIdentifier();
}

DUChainLock* DUChain::lock()
{
  return &sdDUChainPrivate->lock;
}

QList<TopDUContext*> DUChain::allChains() const
{
  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);
  return sdDUChainPrivate->m_chainsByUrl.values();
}

void DUChain::updateContextEnvironment( TopDUContext* context, ParsingEnvironmentFile* file ) {

  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);

  removeFromEnvironmentManager( context );

  context->setParsingEnvironmentFile( file );

  addToEnvironmentManager( context );
}

void DUChain::removeDocumentChain( TopDUContext* context )
{
  ENSURE_CHAIN_WRITE_LOCKED;
  IndexedTopDUContext indexed(context->indexed());
  Q_ASSERT(indexed.data() == context); ///This assertion fails if you call removeDocumentChain(..) on a document that has not been added to the du-chain
  context->m_dynamicData->deleteOnDisk();
  Q_ASSERT(indexed.data() == context);
  sdDUChainPrivate->removeDocumentChainFromMemory(context);
  Q_ASSERT(!indexed.data());
  Q_ASSERT(!environmentFileForDocument(indexed));

  QMutexLocker lock(&sdDUChainPrivate->m_chainsMutex);
  sdDUChainPrivate->m_availableTopContextIndices.push_back(indexed.index());
}

void DUChain::addDocumentChain( TopDUContext * chain )
{
  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);

//   qCDebug(LANGUAGE) << "duchain: adding document" << chain->url().str() << " " << chain;
  Q_ASSERT(chain);

  Q_ASSERT(!sdDUChainPrivate->hasChainForIndex(chain->ownIndex()));

  {
    QMutexLocker lock(&DUChain::chainsByIndexLock);
    if(DUChain::chainsByIndex.size() <= chain->ownIndex())
      DUChain::chainsByIndex.resize(chain->ownIndex() + 100, nullptr);

    DUChain::chainsByIndex[chain->ownIndex()] = chain;
  }
  {
    Q_ASSERT(DUChain::chainsByIndex[chain->ownIndex()]);
  }
  Q_ASSERT(sdDUChainPrivate->hasChainForIndex(chain->ownIndex()));

  sdDUChainPrivate->m_chainsByUrl.insert(chain->url(), chain);

  Q_ASSERT(sdDUChainPrivate->hasChainForIndex(chain->ownIndex()));

  chain->setInDuChain(true);

  l.unlock();

  addToEnvironmentManager(chain);

  // This function might be called during shutdown by stale parse jobs
  // Make sure we don't access null-pointers here
  if (ICore::self() && ICore::self()->languageController() &&
      ICore::self()->languageController()->backgroundParser()->trackerForUrl(chain->url()))
  {
    //Make sure the context stays alive at least as long as the context is open
    ReferencedTopDUContext ctx(chain);
    sdDUChainPrivate->m_openDocumentContexts.insert(ctx);
  }
}

void DUChain::addToEnvironmentManager( TopDUContext * chain ) {

  ParsingEnvironmentFilePointer file = chain->parsingEnvironmentFile();
  if( !file )
    return; //We don't need to manage

  Q_ASSERT(file->indexedTopContext().index() == chain->ownIndex());

  if(ParsingEnvironmentFile* alreadyHave = sdDUChainPrivate->findInformation(file->indexedTopContext().index()))
  {
    ///If this triggers, there has already been another environment-information registered for this top-context.
    ///removeFromEnvironmentManager should have been called before to remove the old environment-information.
    Q_ASSERT(alreadyHave == file.data());
    Q_UNUSED(alreadyHave);
    return;
  }

  sdDUChainPrivate->addEnvironmentInformation(file);
}

void DUChain::removeFromEnvironmentManager( TopDUContext * chain ) {

  ParsingEnvironmentFilePointer file = chain->parsingEnvironmentFile();
  if( !file )
    return; //We don't need to manage

  sdDUChainPrivate->removeEnvironmentInformation(file);
}

TopDUContext* DUChain::chainForDocument(const QUrl& document, bool proxyContext) const {
  return chainForDocument(IndexedString(document), proxyContext);
}

bool DUChain::isInMemory(uint topContextIndex) const {
  return DUChainPrivate::hasChainForIndex(topContextIndex);
}

IndexedString DUChain::urlForIndex(uint index) const {
  {
    TopDUContext* chain = DUChainPrivate::readChainForIndex(index);
    if(chain)
      return chain->url();
  }

  return TopDUContextDynamicData::loadUrl(index);
}

TopDUContext* DUChain::loadChain(uint index)
{
  QSet<uint> loaded;
  sdDUChainPrivate->loadChain(index, loaded);

  {
    QMutexLocker lock(&chainsByIndexLock);

    if(chainsByIndex.size() > index)
    {
      TopDUContext* top = chainsByIndex[index];
      if(top)
        return top;
    }
  }

  return nullptr;
}

TopDUContext* DUChain::chainForDocument(const KDevelop::IndexedString& document, bool proxyContext) const
{
  ENSURE_CHAIN_READ_LOCKED;

  if(sdDUChainPrivate->m_destroyed)
    return nullptr;

  QList<ParsingEnvironmentFilePointer> list = sdDUChainPrivate->getEnvironmentInformation(document);

  foreach(const ParsingEnvironmentFilePointer &file, list)
    if(isInMemory(file->indexedTopContext().index()) && file->isProxyContext() == proxyContext) {
      return file->topContext();
    }

  foreach(const ParsingEnvironmentFilePointer &file, list)
    if(proxyContext == file->isProxyContext()) {
      return file->topContext();
    }

  //Allow selecting a top-context even if there is no ParsingEnvironmentFile
  QList< TopDUContext* > ret = chainsForDocument(document);
  foreach(TopDUContext* ctx, ret) {
    if(!ctx->parsingEnvironmentFile() || (ctx->parsingEnvironmentFile()->isProxyContext() == proxyContext))
      return ctx;
  }

  return nullptr;
}

QList<TopDUContext*> DUChain::chainsForDocument(const QUrl& document) const
{
  return chainsForDocument(IndexedString(document));
}

QList<TopDUContext*> DUChain::chainsForDocument(const IndexedString& document) const
{
  QList<TopDUContext*> chains;

  if(sdDUChainPrivate->m_destroyed)
    return chains;

  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);

  // Match all parsed versions of this document
  for (auto it = sdDUChainPrivate->m_chainsByUrl.lowerBound(document); it != sdDUChainPrivate->m_chainsByUrl.end(); ++it) {
    if (it.key() == document)
      chains << it.value();
    else
      break;
  }

  return chains;
}

TopDUContext* DUChain::chainForDocument( const QUrl& document, const KDevelop::ParsingEnvironment* environment, bool proxyContext ) const {
  return chainForDocument( IndexedString(document), environment, proxyContext );
}

ParsingEnvironmentFilePointer DUChain::environmentFileForDocument( const IndexedString& document, const ParsingEnvironment* environment, bool proxyContext ) const {

  ENSURE_CHAIN_READ_LOCKED;

  if(sdDUChainPrivate->m_destroyed)
    return ParsingEnvironmentFilePointer();
  QList< ParsingEnvironmentFilePointer> list = sdDUChainPrivate->getEnvironmentInformation(document);

//    qCDebug(LANGUAGE) << document.str() << ": matching" << list.size() << (onlyProxyContexts ? "proxy-contexts" : (noProxyContexts ? "content-contexts" : "contexts"));

  auto it = list.constBegin();
  while(it != list.constEnd()) {
    if(*it && ((*it)->isProxyContext() == proxyContext) && (*it)->matchEnvironment(environment) &&
      // Verify that the environment-file and its top-context are "good": The top-context must exist,
      // and there must be a content-context associated to the proxy-context.
      (*it)->topContext() && (!proxyContext || DUChainUtils::contentContextFromProxyContext((*it)->topContext())) ) {
      return *it;
    }
    ++it;
  }
  return ParsingEnvironmentFilePointer();
}

QList<ParsingEnvironmentFilePointer> DUChain::allEnvironmentFiles(const IndexedString& document) {
  return sdDUChainPrivate->getEnvironmentInformation(document);
}

ParsingEnvironmentFilePointer DUChain::environmentFileForDocument(IndexedTopDUContext topContext) const {
   if(topContext.index() == 0)
     return ParsingEnvironmentFilePointer();

   return ParsingEnvironmentFilePointer(sdDUChainPrivate->loadInformation(topContext.index()));
}

TopDUContext* DUChain::chainForDocument( const IndexedString& document, const ParsingEnvironment* environment, bool proxyContext ) const {

  if(sdDUChainPrivate->m_destroyed)
    return nullptr;
  ParsingEnvironmentFilePointer envFile = environmentFileForDocument(document, environment, proxyContext);
  if(envFile) {
    return envFile->topContext();
  }else{
    return nullptr;
  }
}

QList<QUrl> DUChain::documents() const
{
  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);

  QList<QUrl> ret;
  ret.reserve(sdDUChainPrivate->m_chainsByUrl.count());
  foreach(TopDUContext* top, sdDUChainPrivate->m_chainsByUrl) {
    ret << top->url().toUrl();
  }

  return ret;
}

QList<IndexedString> DUChain::indexedDocuments() const
{
  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);

  QList<IndexedString> ret;
  ret.reserve(sdDUChainPrivate->m_chainsByUrl.count());
  foreach(TopDUContext* top, sdDUChainPrivate->m_chainsByUrl) {
    ret << top->url();
  }

  return ret;
}

void DUChain::documentActivated(KDevelop::IDocument* doc)
{
  if(sdDUChainPrivate->m_destroyed)
    return;

  DUChainReadLocker lock( DUChain::lock() );
  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);

  auto backgroundParser = ICore::self()->languageController()->backgroundParser();
  auto addWithHighPriority = [backgroundParser, doc]() {
    backgroundParser->addDocument(IndexedString(doc->url()),
                                  TopDUContext::VisibleDeclarationsAndContexts,
                                  BackgroundParser::BestPriority);
  };

  TopDUContext* ctx = DUChainUtils::standardContextForUrl(doc->url(), true);
  //Check whether the document has an attached environment-manager, and whether that one thinks the document needs to be updated.
  //If yes, update it.
  if (ctx && ctx->parsingEnvironmentFile() && ctx->parsingEnvironmentFile()->needsUpdate()) {
    qCDebug(LANGUAGE) << "Document needs update, using best priority since it just got activated:" << doc->url();
    addWithHighPriority();
  } else if (backgroundParser->managedDocuments().contains(IndexedString(doc->url()))) {
    // increase priority if there's already parse job of this document in the queue
    qCDebug(LANGUAGE) << "Prioritizing activated document:" << doc->url();
    addWithHighPriority();
  }
}

void DUChain::documentClosed(IDocument* document)
{
  if(sdDUChainPrivate->m_destroyed)
    return;

  IndexedString url(document->url());

  foreach(const ReferencedTopDUContext &top, sdDUChainPrivate->m_openDocumentContexts)
    if(top && top->url() == url)
      sdDUChainPrivate->m_openDocumentContexts.remove(top);
}

void DUChain::documentLoadedPrepare(KDevelop::IDocument* doc)
{
  if(sdDUChainPrivate->m_destroyed)
    return;

  const IndexedString url(doc->url());
  DUChainWriteLocker lock( DUChain::lock() );
  QMutexLocker l(&sdDUChainPrivate->m_chainsMutex);

  TopDUContext* standardContext = DUChainUtils::standardContextForUrl(doc->url());
  QList<TopDUContext*> chains = chainsForDocument(url);

  auto languages = ICore::self()->languageController()->languagesForUrl(doc->url());

  if(standardContext) {
    Q_ASSERT(chains.contains(standardContext)); //We have just loaded it
    Q_ASSERT((standardContext->url() == url));

    sdDUChainPrivate->m_openDocumentContexts.insert(standardContext);

    bool needsUpdate = standardContext->parsingEnvironmentFile() && standardContext->parsingEnvironmentFile()->needsUpdate();
    if(!needsUpdate) {

        //Only apply the highlighting if we don't need to update, else we might highlight total crap
        //Do instant highlighting only if all imports are loaded, to make sure that we don't block the user-interface too long
        //Else the highlighting will be done in the background-thread
        //This is not exactly right, as the direct imports don't necessarily equal the real imports used by uses
        //but it approximates the correct behavior.
        bool allImportsLoaded = true;
        foreach(const DUContext::Import& import, standardContext->importedParentContexts())
          if(!import.indexedContext().indexedTopContext().isLoaded())
            allImportsLoaded = false;

        if(allImportsLoaded) {
          l.unlock();
          lock.unlock();
          foreach(const auto language, languages) {
            if(language->codeHighlighting()) {
              language->codeHighlighting()->highlightDUChain(standardContext);
            }
          }
          qCDebug(LANGUAGE) << "highlighted" << doc->url() << "in foreground";
          return;
        }
    }else{
      qCDebug(LANGUAGE) << "not highlighting the duchain because the documents needs an update";
    }

    if(needsUpdate || !(standardContext->features() & TopDUContext::AllDeclarationsContextsAndUses)) {
      ICore::self()->languageController()->backgroundParser()->addDocument(IndexedString(doc->url()), (TopDUContext::Features)(TopDUContext::AllDeclarationsContextsAndUses | TopDUContext::ForceUpdate));
      return;
    }
  }

  //Add for highlighting etc.
  ICore::self()->languageController()->backgroundParser()->addDocument(IndexedString(doc->url()), TopDUContext::AllDeclarationsContextsAndUses);
}

void DUChain::documentRenamed(KDevelop::IDocument* doc)
{
  if(sdDUChainPrivate->m_destroyed)
    return;

  if(!doc->url().isValid()) {
    ///Maybe this happens when a file was deleted?
    qCWarning(LANGUAGE) << "Strange, url of renamed document is invalid!";
  }else{
    ICore::self()->languageController()->backgroundParser()->addDocument(IndexedString(doc->url()), (TopDUContext::Features)(TopDUContext::AllDeclarationsContextsAndUses | TopDUContext::ForceUpdate));
  }
}

Uses* DUChain::uses()
{
  return &sdDUChainPrivate->m_uses;
}

Definitions* DUChain::definitions()
{
  return &sdDUChainPrivate->m_definitions;
}

static void finalCleanup()
{
  DUChainWriteLocker writeLock(DUChain::lock());
  qCDebug(LANGUAGE) << "doing final cleanup";

  int cleaned = 0;
  int passes = 0;
  while((cleaned = globalItemRepositoryRegistry().finalCleanup())) {
    passes += 1;
    qCDebug(LANGUAGE) << "cleaned" << cleaned << "B";
    if(cleaned < 1000) {
      qCDebug(LANGUAGE) << "cleaned enough";
      break;
    } else if (passes >= 100) {
      qCWarning(LANGUAGE) << "cleaned" << passes << "passes, last was" << cleaned << "B; that's enough";
      break;
    }
  }
  qCDebug(LANGUAGE) << "final cleanup ready";
}

void DUChain::shutdown()
{
  // if core is not shutting down, we can end up in deadlocks or crashes
  // since language plugins might still try to access static duchain stuff
  Q_ASSERT(!ICore::self() || ICore::self()->shuttingDown());

  qCDebug(LANGUAGE) << "Cleaning up and shutting down DUChain";

  QMutexLocker lock(&sdDUChainPrivate->cleanupMutex());

  {
    //Acquire write-lock of the repository, so when kdevelop crashes in that process, the repository is discarded
    //Crashes here may happen in an inconsistent state, thus this makes sense, to protect the user from more crashes
    globalItemRepositoryRegistry().lockForWriting();
    sdDUChainPrivate->cleanupTopContexts();
    globalItemRepositoryRegistry().unlockForWriting();
  }

  sdDUChainPrivate->doMoreCleanup(); //Must be done _before_ finalCleanup, else we may be deleting yet needed data

  sdDUChainPrivate->m_openDocumentContexts.clear();
  sdDUChainPrivate->m_destroyed = true;
  sdDUChainPrivate->clear();

  {
    //Acquire write-lock of the repository, so when kdevelop crashes in that process, the repository is discarded
    //Crashes here may happen in an inconsistent state, thus this makes sense, to protect the user from more crashes
    globalItemRepositoryRegistry().lockForWriting();
    finalCleanup();
    globalItemRepositoryRegistry().unlockForWriting();
  }

  globalItemRepositoryRegistry().shutdown();
}

uint DUChain::newTopContextIndex() {
  {
    QMutexLocker lock(&sdDUChainPrivate->m_chainsMutex);
    if(!sdDUChainPrivate->m_availableTopContextIndices.isEmpty())
    {
      uint ret = sdDUChainPrivate->m_availableTopContextIndices.back();
      sdDUChainPrivate->m_availableTopContextIndices.pop_back();
      if(TopDUContextDynamicData::fileExists(ret))
      {
        qCWarning(LANGUAGE) << "Problem in the management of available top-context indices";
        return newTopContextIndex();
      }
      return ret;
    }
  }
  static QAtomicInt& currentId( globalItemRepositoryRegistry().getCustomCounter(QStringLiteral("Top-Context Counter"), 1) );
  return currentId.fetchAndAddRelaxed(1);
}

void DUChain::refCountUp(TopDUContext* top) {
  QMutexLocker l(&sdDUChainPrivate->m_referenceCountsMutex);
  // note: value is default-constructed to zero if it does not exist
  ++sdDUChainPrivate->m_referenceCounts[top];
}

bool DUChain::deleted() {
  return m_deleted;
}

void DUChain::refCountDown(TopDUContext* top) {
  QMutexLocker l(&sdDUChainPrivate->m_referenceCountsMutex);
  auto it = sdDUChainPrivate->m_referenceCounts.find(top);
  if (it == sdDUChainPrivate->m_referenceCounts.end()) {
    //qCWarning(LANGUAGE) << "tried to decrease reference-count for" << top->url().str() << "but this top-context is not referenced";
    return;
  }
  auto& refCount = *it;
  --refCount;
  if (!refCount) {
    sdDUChainPrivate->m_referenceCounts.erase(it);
  }
}

void DUChain::emitDeclarationSelected(const DeclarationPointer& decl)
{
  if(sdDUChainPrivate->m_destroyed)
    return;

  emit declarationSelected(decl);
}

void DUChain::emitUpdateReady(const IndexedString& url, const ReferencedTopDUContext& topContext)
{
  if(sdDUChainPrivate->m_destroyed)
    return;

  emit updateReady(url, topContext);
}

KDevelop::ReferencedTopDUContext DUChain::waitForUpdate(const KDevelop::IndexedString& document, KDevelop::TopDUContext::Features minFeatures, bool proxyContext) {
  Q_ASSERT(!lock()->currentThreadHasReadLock() && !lock()->currentThreadHasWriteLock());

  WaitForUpdate waiter;
  updateContextForUrl(document, minFeatures, &waiter);

  while(!waiter.m_ready) {
    // we might have been shut down in the meanwhile
    if (!ICore::self()) {
      return nullptr;
    }

    QMetaObject::invokeMethod(ICore::self()->languageController()->backgroundParser(), "parseDocuments");
    QApplication::processEvents();
    QThread::usleep(1000);
  }

  if(!proxyContext) {
    DUChainReadLocker readLock(DUChain::lock());
    return DUChainUtils::contentContextFromProxyContext(waiter.m_topContext);
  }

  return waiter.m_topContext;
}

void DUChain::updateContextForUrl(const IndexedString& document, TopDUContext::Features minFeatures, QObject* notifyReady, int priority) const {
  DUChainReadLocker lock( DUChain::lock() );
  TopDUContext* standardContext = DUChainUtils::standardContextForUrl(document.toUrl());
  if(standardContext && standardContext->parsingEnvironmentFile() && !standardContext->parsingEnvironmentFile()->needsUpdate() && standardContext->parsingEnvironmentFile()->featuresSatisfied(minFeatures)) {
    lock.unlock();
    if(notifyReady)
    QMetaObject::invokeMethod(notifyReady, "updateReady", Qt::DirectConnection, Q_ARG(KDevelop::IndexedString, document), Q_ARG(KDevelop::ReferencedTopDUContext, ReferencedTopDUContext(standardContext)));
  }else{
    ///Start a parse-job for the given document
    ICore::self()->languageController()->backgroundParser()->addDocument(document, minFeatures, priority, notifyReady);
  }
}

void DUChain::disablePersistentStorage(bool disable) {
  sdDUChainPrivate->m_cleanupDisabled = disable;
}

void DUChain::storeToDisk() {
  bool wasDisabled = sdDUChainPrivate->m_cleanupDisabled;
  sdDUChainPrivate->m_cleanupDisabled = false;

  sdDUChainPrivate->doMoreCleanup();

  sdDUChainPrivate->m_cleanupDisabled = wasDisabled;
}

bool DUChain::compareToDisk() {

  DUChainWriteLocker writeLock(DUChain::lock());

  ///Step 1: Compare the repositories
  return true;
}

}

