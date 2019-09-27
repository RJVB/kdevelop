/* This  is part of KDevelop

   Copyright 2014 Milian Wolff <mail@milianw.de>
   Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "topducontextdynamicdata_p.h"
#include "topducontextdynamicdata.h"

#include <typeinfo>
#include <QFile>
#include <QByteArray>

#include "declaration.h"
#include "declarationdata.h"
#include "ducontext.h"
#include "topducontext.h"
#include "topducontextdata.h"
#include "ducontextdata.h"
#include "ducontextdynamicdata.h"
#include "duchainregister.h"
#include "serialization/itemrepository.h"
#include "problem.h"
#include <debug.h>

//#define DEBUG_DATA_INFO
#include <QElapsedTimer>

//This might be problematic on some systems, because really many mmaps are created
#if defined(KDEV_TOPCONTEXTS_USE_FILES) && !defined(KDEV_TOPCONTEXTS_DONT_MMAP)
#define USE_MMAP
#endif

using namespace KDevelop;

namespace {

/**
 * Serialize @p item into @p data and update @p totalDataOffset.
 *
 * If @p isSharedDataItem is true, then the item's internal data pointer is not updated
 * to point to the serialized data. Otherwise the dynamic data is deleted and the items
 * data will point to the constant serialized data.
 *
 * NOTE: The above is required to support serialization of shared-data such as from ProblemPointer.
 * If we'd set the data to point to the constant region, we'd get crashes due to use-after-free when
 * we unmap the data and a shared pointer outlives that.
 */
void saveDUChainItem(QVector<TopDUContextDynamicData::ArrayWithPosition>& data, DUChainBase& item,
                     uint& totalDataOffset, bool isSharedDataItem)
{
    if (!item.d_func()->classId) {
        //If this triggers, you have probably created an own DUChainBase based class, but haven't called setClassId(this) in the constructor.
        qCritical() << "no class-id set for data attached to a declaration of type" << typeid(item).name();
        Q_ASSERT(0);
    }

    int size = DUChainItemSystem::self().dynamicSize(*item.d_func());

    if (data.back().array.size() - int( data.back().position ) < size)
        //Create a new data item
        data.append({QByteArray(size > 10000 ? size : 10000, 0), 0u});

    uint pos = data.back().position;
    data.back().position += size;
    totalDataOffset += size;

    DUChainBaseData& target(*(reinterpret_cast<DUChainBaseData*>(data.back().array.data() + pos)));

    if (item.d_func()->isDynamic()) {
        //Change from dynamic data to constant data

        enableDUChainReferenceCounting(data.back().array.data(), data.back().array.size());
        DUChainItemSystem::self().copy(*item.d_func(), target, true);
        Q_ASSERT(!target.isDynamic());
        if (!isSharedDataItem) {
            item.setData(&target);
        }
        disableDUChainReferenceCounting(data.back().array.data());
    } else {
        //Just copy the data into another place, expensive copy constructors are not needed
#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && (((__GNUC__ * 100) + __GNUC_MINOR__) >= 800)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        memcpy(&target, item.d_func(), size);
#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && (((__GNUC__ * 100) + __GNUC_MINOR__) >= 800)
#pragma GCC diagnostic pop
#endif
        if (!isSharedDataItem) {
            item.setData(&target, false);
        }
    }

    if (!isSharedDataItem) {
        Q_ASSERT(item.d_func() == &target);

        Q_ASSERT(!item.d_func()->isDynamic());
    }
}

uint indexForParentContext(DUContext* context)
{
    return LocalIndexedDUContext(context->parentContext()).localIndex();
}

uint indexForParentContext(Declaration* declaration)
{
    return LocalIndexedDUContext(declaration->context()).localIndex();
}

uint indexForParentContext(const ProblemPointer& /*problem*/)
{
    // always stored in the top context
    return 0;
}

#ifndef QT_NO_DEBUG
void validateItem(const DUChainBaseData* const data, const uchar* const mappedData, const size_t mappedDataSize)
{
    Q_ASSERT(!data->isDynamic());
    if (mappedData) {
        Q_ASSERT((( size_t )data) < (( size_t )mappedData)
                 || (( size_t )data) > (( size_t )mappedData) + mappedDataSize);
    }
}
#endif

const char* pointerInData(const QVector<TopDUContextDynamicData::ArrayWithPosition>& data, uint totalOffset)
{
    for (int a = 0; a < data.size(); ++a) {
        if (totalOffset < data[a].position)
            return data[a].array.constData() + totalOffset;
        totalOffset -= data[a].position;
    }

    Q_ASSERT_X(false, Q_FUNC_INFO, "Offset doesn't exist in the data.");
    return nullptr;
}

void verifyDataInfo(const TopDUContextDynamicData::ItemDataInfo& info,
                    const QVector<TopDUContextDynamicData::ArrayWithPosition>& data)
{
    Q_UNUSED(info);
    Q_UNUSED(data);
#ifdef DEBUG_DATA_INFO
    DUChainBaseData* item = ( DUChainBaseData* )(pointerInData(data, info.dataOffset));
    int size = DUChainItemSystem::self().dynamicSize(*item);
    Q_ASSERT(size);
#endif
}

enum LoadType {
    PartialLoad, ///< Only load the direct member data
    FullLoad   ///< Load everything, including appended lists
};
template <typename F>
void loadTopDUContextData(const uint topContextIndex, LoadType loadType, F callback)
{
    TopDUContextStore store(topContextIndex);
    if (!store.open(QIODevice::ReadOnly)) {
        return;
    }

    uint readValue;
    store.read(( char* )&readValue, sizeof(uint));
    // now readValue is filled with the top-context data size
    Q_ASSERT(readValue >= sizeof(TopDUContextData));
    const QByteArray data = store.read(loadType == FullLoad ? readValue : sizeof(TopDUContextData));
    const auto* topData = reinterpret_cast<const TopDUContextData*>(data.constData());
    callback(topData);
}

template <typename T>
struct PtrType;

template <typename T>
struct PtrType<T*>
{
    using value = T *;
};

template <typename T>
struct PtrType<QExplicitlySharedDataPointer<T>>
{
    using value = T *;
};

template <typename T>
Q_DECL_CONSTEXPR bool isSharedDataItem()
{
    return false;
}

template <>
Q_DECL_CONSTEXPR bool isSharedDataItem<ProblemPointer>()
{
    return true;
}

}

//BEGIN DUChainItemStorage

template <class Item>
TopDUContextDynamicData::DUChainItemStorage<Item>::DUChainItemStorage(TopDUContextDynamicData* data)
    : data(data)
{
}

template <class Item>
TopDUContextDynamicData::DUChainItemStorage<Item>::~DUChainItemStorage()
{
    clearItems();
}

template <class Item>
void TopDUContextDynamicData::DUChainItemStorage<Item>::clearItems()
{
    //Due to template specialization it's possible that a declaration is not reachable through the normal context structure.
    //For that reason we have to check here, and delete all remaining declarations.
    qDeleteAll(temporaryItems);
    temporaryItems.clear();
    qDeleteAll(items);
    items.clear();
}

namespace KDevelop {
template <>
void TopDUContextDynamicData::DUChainItemStorage<ProblemPointer>::clearItems()
{
    // don't delete anything - the problem is shared
    items.clear();
}
}

template <class Item>
void TopDUContextDynamicData::DUChainItemStorage<Item>::clearItemIndex(const Item& item, const uint index)
{
    if (!data->m_dataLoaded)
        data->loadData();

    if (index < (0x0fffffff / 2)) {
        if (index == 0 || index > uint(items.size())) {
            return;
        } else {
            const uint realIndex = index - 1;
            Q_ASSERT(items[realIndex] == item);
            items[realIndex] = nullptr;

            if (realIndex < ( uint )offsets.size()) {
                offsets[realIndex] = ItemDataInfo();
            }
        }
    } else {
        const uint realIndex = 0x0fffffff - index; //We always keep the highest bit at zero
        if (realIndex == 0 || realIndex > uint(temporaryItems.size())) {
            return;
        } else {
            Q_ASSERT(temporaryItems[realIndex - 1] == item);
            temporaryItems[realIndex - 1] = nullptr;
        }
    }

    Q_UNUSED(item);
}

template <class Item>
void TopDUContextDynamicData::DUChainItemStorage<Item>::storeData(uint& currentDataOffset,
                                                                  const QVector<ArrayWithPosition>& oldData)
{
    auto const oldOffsets = offsets;
    offsets.clear();
    offsets.reserve(items.size());
    for (int a = 0; a < items.size(); ++a) {
        auto item = items[a];
        if (!item) {
            if (oldOffsets.size() > a && oldOffsets[a].dataOffset) {
                //Directly copy the old data range into the new data
                const DUChainBaseData* itemData = nullptr;
                if (data->m_mappedData) {
                    itemData = reinterpret_cast<const DUChainBaseData*>(data->m_mappedData + oldOffsets[a].dataOffset);
                } else {
                    itemData =
                        reinterpret_cast<const DUChainBaseData*>(::pointerInData(oldData, oldOffsets[a].dataOffset));
                }
                offsets << data->writeDataInfo(oldOffsets[a], itemData, currentDataOffset);
            } else {
                offsets << ItemDataInfo();
            }
        } else {
            offsets << ItemDataInfo{currentDataOffset, indexForParentContext(item)};
            saveDUChainItem(data->m_data, *item, currentDataOffset, isSharedDataItem<Item>());
        }
    }

#ifndef QT_NO_DEBUG
    if (!isSharedDataItem<Item>()) {
        for (auto item : items) {
            if (item) {
                validateItem(item->d_func(), data->m_mappedData, data->m_mappedDataSize);
            }
        }
    }
#endif
}

template <typename Item>
bool TopDUContextDynamicData::DUChainItemStorage<Item>::itemsHaveChanged() const
{
    for (auto item : items) {
        if (item && item->d_func()->m_dynamic) {
            return true;
        }
    }

    return false;
}

template <class Item>
uint TopDUContextDynamicData::DUChainItemStorage<Item>::allocateItemIndex(const Item& item, const bool temporary)
{
    if (!data->m_dataLoaded) {
        data->loadData();
    }
    if (!temporary) {
        items.append(item);
        return items.size();
    } else {
        temporaryItems.append(item);
        return 0x0fffffff - temporaryItems.size(); //We always keep the highest bit at zero
    }
}

template <class Item>
bool TopDUContextDynamicData::DUChainItemStorage<Item>::isItemForIndexLoaded(uint index) const
{
    if (!data->m_dataLoaded) {
        return false;
    }
    if (index < (0x0fffffff / 2)) {
        if (index == 0 || index > uint(items.size())) {
            return false;
        }
        return items[index - 1];
    } else {
        // temporary item
        return true;
    }
}

template <class Item>
Item TopDUContextDynamicData::DUChainItemStorage<Item>::itemForIndex(uint index) const
{
    if (index >= (0x0fffffff / 2)) {
        index = 0x0fffffff - index; //We always keep the highest bit at zero
        if (index == 0 || index > uint(temporaryItems.size()))
            return {};
        else
            return temporaryItems.at(index - 1);
    }

    if (index == 0 || index > static_cast<uint>(items.size())) {
        qCDebug(LANGUAGE) << "item index out of bounds:" << index << "count:" << items.size();
        return {};
    }
    const uint realIndex = index - 1;
    const auto& item = items.at(realIndex);
    if (item) {
        //Shortcut, because this is the most common case
        return item;
    }

    if (realIndex < ( uint )offsets.size() && offsets[realIndex].dataOffset) {
        Q_ASSERT(!data->m_itemRetrievalForbidden);

        //Construct the context, and eventuall its parent first
        ///TODO: ugly, remove need for const_cast
        auto itemData = const_cast<DUChainBaseData*>(
            reinterpret_cast<const DUChainBaseData*>(data->pointerInData(offsets[realIndex].dataOffset))
                        );

        if (itemData) {
            auto& item = items[realIndex];
            item = dynamic_cast<typename PtrType<Item>::value>(DUChainItemSystem::self().create(itemData));
            if (!item) {
              //When this happens, the item has not been registered correctly.
              //We can stop here, because else we will get crashes later.
              qCritical() << "Failed to load item with identity" << itemData->classId;
              return {};
            }
        } else {
            qCritical() << "Failed to load item at realIndex" << realIndex << "itemData=" << itemData;
            return {};
        }

        if (isSharedDataItem<Item>()) {
            // NOTE: shared data must never point to mmapped data regions as otherwise we might end up with
            // use-after-free or double-deletions etc. pp.
            // thus, make the item always dynamic after deserialization
            item->makeDynamic();
        }

        auto parent = data->contextForIndex(offsets[realIndex].parentContext);
        Q_ASSERT_X(parent, Q_FUNC_INFO, "Could not find parent context for loaded item.\n"
                   "Potentially, the context has been deleted without deleting its children.");
        item->rebuildDynamicData(parent, index);
    } else {
        qCDebug(LANGUAGE) << "invalid item for index" << index << offsets.size() <<
            offsets.value(realIndex).dataOffset;
    }

    return item;
}

template <class Item>
void TopDUContextDynamicData::DUChainItemStorage<Item>::deleteOnDisk()
{
    for (auto& item : items) {
        if (item) {
            item->makeDynamic();
        }
    }
}

template <class Item>
void TopDUContextDynamicData::DUChainItemStorage<Item>::loadData(TopDUContextStore* store) const
{
    Q_ASSERT(offsets.isEmpty());
    Q_ASSERT(items.isEmpty());

    uint readValue;
    store->read(( char* )&readValue, sizeof(uint));
    offsets.resize(readValue);

    store->read(( char* )offsets.data(), sizeof(ItemDataInfo) * offsets.size());

    //Fill with zeroes for now, will be initialized on-demand
    items.resize(offsets.size());
}

template <class Item>
void TopDUContextDynamicData::DUChainItemStorage<Item>::writeData(TopDUContextStore* store)
{
    uint writeValue = offsets.size();
    store->write(( char* )&writeValue, sizeof(uint));
    store->write(( char* )offsets.data(), sizeof(ItemDataInfo) * offsets.size());
}

//END DUChainItemStorage

const char* TopDUContextDynamicData::pointerInData(uint totalOffset) const
{
    Q_ASSERT(!m_mappedData || m_data.isEmpty());

    if (m_mappedData && m_mappedDataSize)
        return ( char* )m_mappedData + totalOffset;

    return ::pointerInData(m_data, totalOffset);
}

TopDUContextDynamicData::TopDUContextDynamicData(TopDUContext* topContext)
    : m_deleting(false)
    , m_topContext(topContext)
    , m_contexts(this)
    , m_declarations(this)
    , m_problems(this)
    , m_onDisk(false)
    , m_dataLoaded(true)
    , m_mappedFile(nullptr)
    , m_mappedData(nullptr)
    , m_mappedDataSize(0)
    , m_itemRetrievalForbidden(false)
{
}

void KDevelop::TopDUContextDynamicData::clear()
{
    m_contexts.clearItems();
    m_declarations.clearItems();
    m_problems.clearItems();
}

TopDUContextDynamicData::~TopDUContextDynamicData()
{
    unmap();
}

void KDevelop::TopDUContextDynamicData::unmap()
{
    delete m_mappedFile;
    m_mappedFile = nullptr;
    m_mappedData = nullptr;
    m_mappedDataSize = 0;
}

bool TopDUContextDynamicData::fileExists(uint topContextIndex)
{
    return TopDUContextStore::exists(topContextIndex);
}

QList<IndexedDUContext> TopDUContextDynamicData::loadImporters(uint topContextIndex)
{
    QList<IndexedDUContext> ret;
    loadTopDUContextData(topContextIndex, FullLoad, [&ret](const TopDUContextData* topData) {
        ret.reserve(topData->m_importersSize());
        FOREACH_FUNCTION(const IndexedDUContext &importer, topData->m_importers)
        ret << importer;
    });
    return ret;
}

QList<IndexedDUContext> TopDUContextDynamicData::loadImports(uint topContextIndex)
{
    QList<IndexedDUContext> ret;
    loadTopDUContextData(topContextIndex, FullLoad, [&ret](const TopDUContextData* topData) {
        ret.reserve(topData->m_importedContextsSize());
        FOREACH_FUNCTION(const DUContext::Import& import, topData->m_importedContexts)
        ret << import.indexedContext();
    });
    return ret;
}

IndexedString TopDUContextDynamicData::loadUrl(uint topContextIndex)
{
    IndexedString url;
    loadTopDUContextData(topContextIndex, PartialLoad, [&url](const TopDUContextData* topData) {
        Q_ASSERT(topData->m_url.isEmpty() || topData->m_url.index() >> 16);
        url = topData->m_url;
    });
    return url;
}

void TopDUContextDynamicData::loadData() const
{
    //This function has to be protected by an additional mutex, since it can be triggered from multiple threads at the same time
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    if (m_dataLoaded)
        return;

    Q_ASSERT(!m_dataLoaded);
    Q_ASSERT(m_data.isEmpty());

    TopDUContextStore* store = new TopDUContextStore(m_topContext->ownIndex());
    bool open = store->open(QIODevice::ReadOnly);
    Q_UNUSED(open);
    Q_ASSERT(open);
    Q_ASSERT(store->size());

    //Skip the offsets, we're already read them
    //Skip top-context data
    uint readValue;
    store->read(( char* )&readValue, sizeof(uint));
    store->seek(readValue + store->pos());

    m_contexts.loadData(store);
    m_declarations.loadData(store);
    m_problems.loadData(store);

#ifdef USE_MMAP

    m_mappedData = store->map(store->pos(), store->size() - store->pos());
    if (m_mappedData) {
        m_mappedFile = store;
        m_mappedDataSize = store->size() - store->pos();
        store->commit(); //Close the store, so there are less open file descriptors (May be problematic)
    } else {
        qCDebug(LANGUAGE) << "Failed to map" << store->fileName();
    }

#endif

    if (!m_mappedFile) {
        QByteArray data = store->readAll();
        m_data.append({data, ( uint )data.size()});
        delete store;
    }

    m_dataLoaded = true;
}

TopDUContext* TopDUContextDynamicData::load(uint topContextIndex)
{
    TopDUContextStore store(topContextIndex);
    if(store.open(QIODevice::ReadOnly)) {
        if(store.size() == 0) {
            qCWarning(LANGUAGE) << "Top-context store is empty" << store.fileName();
            return nullptr;
        }

        uint readValue;
        store.read(( char* )&readValue, sizeof(uint));
        //now readValue is filled with the top-context data size
        QByteArray topContextData = store.read(readValue);

        auto* topData = reinterpret_cast<DUChainBaseData*>(topContextData.data());
        auto* ret = dynamic_cast<TopDUContext*>(DUChainItemSystem::self().create(topData));
        if (!ret) {
            qCWarning(LANGUAGE) << "Cannot load a top-context from store" << store.fileName() <<
                "- the required language-support for handling ID" << topData->classId << "is probably not loaded";
            return nullptr;
        }

        TopDUContextDynamicData& target(*ret->m_dynamicData);

        target.m_data.clear();
        target.m_dataLoaded = false;
        target.m_onDisk = true;
        ret->rebuildDynamicData(nullptr, topContextIndex);
        target.m_topContextData.append({topContextData, ( uint )0});
        return ret;
    } else {
        return nullptr;
    }
}

bool TopDUContextDynamicData::isOnDisk() const
{
    return m_onDisk;
}

void TopDUContextDynamicData::deleteOnDisk()
{
    if (!isOnDisk())
        return;
    qCDebug(LANGUAGE) << "deleting" << m_topContext->ownIndex() << m_topContext->url().str();

    if (!m_dataLoaded)
        loadData();

    m_contexts.deleteOnDisk();
    m_declarations.deleteOnDisk();
    m_problems.deleteOnDisk();

    m_topContext->makeDynamic();

    m_onDisk = false;

    bool successfullyRemoved = TopDUContextStore::remove(m_topContext->ownIndex());
    Q_UNUSED(successfullyRemoved);
    Q_ASSERT(successfullyRemoved);
    qCDebug(LANGUAGE) << "deletion ready";
}

QString TopDUContextDynamicData::basePath()
{
    return globalItemRepositoryRegistry().path() + QLatin1String("/topcontexts/");
}

QString TopDUContextDynamicData::pathForTopContext(const uint topContextIndex)
{
    return basePath() + QString::number(topContextIndex);
}

QString KDevelop::TopDUContextDynamicData::filePath() const
{
    return pathForTopContext(m_topContext->ownIndex());
}

bool TopDUContextDynamicData::hasChanged() const
{
    return !m_onDisk || m_topContext->d_func()->m_dynamic
           || m_contexts.itemsHaveChanged() || m_declarations.itemsHaveChanged()
           || m_problems.itemsHaveChanged();
}

void TopDUContextDynamicData::store()
{
//   qCDebug(LANGUAGE) << "storing" << m_topContext->url().str() << m_topContext->ownIndex() << "import-count:" << m_topContext->importedParentContexts().size();

    //Check if something has changed. If nothing has changed, don't store to disk.
    bool contentDataChanged = hasChanged();
    if (!contentDataChanged) {
        return;
    }

    ///@todo Save the meta-data into a repository, and only the actual content data into a file.
    ///      This will make saving+loading more efficient, and will reduce the disk-usage.
    ///      Then we also won't need to load the data if only the meta-data changed.
    if (!m_dataLoaded)
        loadData();

    ///If the data is mapped, and we re-write the file, we must make sure that the data is copied out of the map,
    ///even if only metadata is changed.
    ///@todo If we split up data and metadata, we don't need to do this
    if (m_mappedData)
        contentDataChanged = true;

    m_topContext->makeDynamic();
    m_topContextData.clear();
    Q_ASSERT(m_topContext->d_func()->m_ownIndex == m_topContext->ownIndex());

    uint topContextDataSize = DUChainItemSystem::self().dynamicSize(*m_topContext->d_func());
    m_topContextData.append({QByteArray(DUChainItemSystem::self().dynamicSize(*m_topContext->d_func()),
                                        topContextDataSize), 0u});
    uint actualTopContextDataSize = 0;

    if (contentDataChanged) {
        //We don't need these structures any more, since we have loaded all the declarations/contexts, and m_data
        //will be reset which these structures pointed into
        //Load all lazy declarations/contexts

        const auto oldData = m_data; //Keep the old data alive until everything is stored into a new data structure

        m_data.clear();

        uint newDataSize = 0;
        for (const ArrayWithPosition& array : oldData) {
            newDataSize += array.position;
        }

        newDataSize = std::max(newDataSize, 10000u);

        //We always put 1 byte to the front, so we don't have zero data-offsets, since those are used for "invalid".
        uint currentDataOffset = 1;
        m_data.append({QByteArray(newDataSize, 0), currentDataOffset});

        m_itemRetrievalForbidden = true;

        m_contexts.storeData(currentDataOffset, oldData);
        m_declarations.storeData(currentDataOffset, oldData);
        m_problems.storeData(currentDataOffset, oldData);

        m_itemRetrievalForbidden = false;
    }

    saveDUChainItem(m_topContextData, *m_topContext, actualTopContextDataSize, false);
    Q_ASSERT(actualTopContextDataSize == topContextDataSize);
    Q_ASSERT(m_topContextData.size() == 1);
    Q_ASSERT(!m_topContext->d_func()->isDynamic());

    unmap();

    QDir().mkpath(basePath());

    QElapsedTimer timer;
    qint64 nBytes = 0;
    if (Q_LIKELY(QFileInfo(basePath()).isWritable())) {
        timer.start();
        TopDUContextStore store(m_topContext->ownIndex());
        if(store.open(QIODevice::WriteOnly)) {

            store.resize(0);

            store.write(( char* )&topContextDataSize, sizeof(uint));
            for (const ArrayWithPosition& pos : qAsConst(m_topContextData)) {
                store.write(pos.array.constData(), pos.position);
            }

            m_contexts.writeData(&store);
            m_declarations.writeData(&store);
            m_problems.writeData(&store);

            for (const ArrayWithPosition& pos : qAsConst(m_data)) {
                store.write(pos.array.constData(), pos.position);
            }

            m_onDisk = true;

            nBytes = store.size();
            if (nBytes == 0) {
                qCWarning(LANGUAGE) << "Saving zero size top ducontext data";
            }
            store.commit();
        } else {
            qCWarning(LANGUAGE) << "Cannot open topcontext" << store.fileName() << "for writing:" << store.errorString();
        }
//       qCDebug(LANGUAGE) << "stored" << m_topContext->url().str() << m_topContext->ownIndex() << "import-count:" << m_topContext->importedParentContexts().size();
    } else {
        static bool warned = false;
        if (!warned) {
            qCWarning(LANGUAGE) << "Topcontexts directory" << basePath() << "is not writable, topcontext files won't be stored.";
            warned = true;
        }
    }
    if (timer.isValid()) {
        auto elapsed = timer.elapsed();
        static quint64 totalBytes = 0;
        static double totalElapsed = 0.0;
        totalBytes += nBytes;
        totalElapsed += elapsed / 1000.0;
        if (totalBytes && totalElapsed >= 0.5) {
            qCDebug(LANGUAGE) << "Stored" << totalBytes << "topcontext bytes at" << totalBytes / totalElapsed << "bytes/second";
            totalBytes = 0, totalElapsed = 0.0;
        }
    }
}

TopDUContextDynamicData::ItemDataInfo TopDUContextDynamicData::writeDataInfo(const ItemDataInfo& info,
                                                                             const DUChainBaseData* data,
                                                                             uint& totalDataOffset)
{
    ItemDataInfo ret(info);
    Q_ASSERT(info.dataOffset);
    const auto size = DUChainItemSystem::self().dynamicSize(*data);
    Q_ASSERT(size);

    if (m_data.back().array.size() - m_data.back().position < size) {
        //Create a new m_data item
        m_data.append({QByteArray(std::max(size, 10000u), 0), 0u});
    }

    ret.dataOffset = totalDataOffset;

    uint pos = m_data.back().position;
    m_data.back().position += size;
    totalDataOffset += size;

    auto target = reinterpret_cast<DUChainBaseData*>(m_data.back().array.data() + pos);
#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && (((__GNUC__ * 100) + __GNUC_MINOR__) >= 800)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
    memcpy(target, data, size);
#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && (((__GNUC__ * 100) + __GNUC_MINOR__) >= 800)
#pragma GCC diagnostic pop
#endif

    verifyDataInfo(ret, m_data);
    return ret;
}

uint TopDUContextDynamicData::allocateDeclarationIndex(Declaration* decl, bool temporary)
{
    return m_declarations.allocateItemIndex(decl, temporary);
}

uint TopDUContextDynamicData::allocateContextIndex(DUContext* context, bool temporary)
{
    return m_contexts.allocateItemIndex(context, temporary);
}

uint TopDUContextDynamicData::allocateProblemIndex(const ProblemPointer& problem)
{
    return m_problems.allocateItemIndex(problem, false);
}

bool TopDUContextDynamicData::isDeclarationForIndexLoaded(uint index) const
{
    return m_declarations.isItemForIndexLoaded(index);
}

bool TopDUContextDynamicData::isContextForIndexLoaded(uint index) const
{
    return m_contexts.isItemForIndexLoaded(index);
}

bool TopDUContextDynamicData::isTemporaryContextIndex(uint index) const
{
    return !(index < (0x0fffffff / 2));
}

bool TopDUContextDynamicData::isTemporaryDeclarationIndex(uint index) const
{
    return !(index < (0x0fffffff / 2));
}

DUContext* TopDUContextDynamicData::contextForIndex(uint index) const
{
    if (!m_dataLoaded)
        loadData();

    if (index == 0) {
        return m_topContext;
    }

    return m_contexts.itemForIndex(index);
}

Declaration* TopDUContextDynamicData::declarationForIndex(uint index) const
{
    if (!m_dataLoaded)
        loadData();

    return m_declarations.itemForIndex(index);
}

ProblemPointer TopDUContextDynamicData::problemForIndex(uint index) const
{
    if (!m_dataLoaded)
        loadData();

    return m_problems.itemForIndex(index);
}

void TopDUContextDynamicData::clearDeclarationIndex(Declaration* decl)
{
    m_declarations.clearItemIndex(decl, decl->m_indexInTopContext);
}

void TopDUContextDynamicData::clearContextIndex(DUContext* context)
{
    m_contexts.clearItemIndex(context, context->m_dynamicData->m_indexInTopContext);
}

void TopDUContextDynamicData::clearProblems()
{
    m_problems.clearItems();
}
