#define TIME_IMPORT_JOB
/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2010-2012 Milian Wolff <mail@milianw.de>                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "abstractfilemanagerplugin.h"

#include "filemanagerlistjob.h"
#include "projectmodel.h"
#include "helper.h"

#include <QHashIterator>
#include <QFileInfo>
#include <QApplication>
#include <QTimer>
#ifdef TIME_IMPORT_JOB
#include <QElapsedTimer>
#endif
#include <QPointer>

#include <KMessageBox>
#include <KLocalizedString>
#include <KDirWatch>

#include <interfaces/iproject.h>
#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <serialization/indexedstring.h>

#include "projectfiltermanager.h"
#include "projectwatcher.h"
#include "debug.h"

#define ifDebug(x)

using namespace KDevelop;

//BEGIN Helper

namespace {

/**
 * Returns the parent folder item for a given item or the project root item if there is no parent.
 */
ProjectFolderItem* parentFolder(ProjectBaseItem* item)
{
    if ( item->parent() ) {
        return static_cast<ProjectFolderItem*>(item->parent());
    } else {
        return item->project()->projectItem();
    }
}

}

//END Helper

//BEGIN Private

typedef QPointer<FileManagerListJob> FileManagerListJobPtr;

class KDevelop::AbstractFileManagerPluginPrivate
{
public:
    explicit AbstractFileManagerPluginPrivate(AbstractFileManagerPlugin* qq)
        : q(qq)
        , m_intreeDirWatching(qEnvironmentVariableIsSet("KDEV_PROJECT_INTREE_DIRWATCHING_MODE"))
    {
    }

    AbstractFileManagerPlugin* q;

    /**
     * The just returned must be started in one way or another for this method
     * to have any affect. The job will then auto-delete itself upon completion.
     */
    Q_REQUIRED_RESULT FileManagerListJob* eventuallyReadFolder(ProjectFolderItem* item, bool recursive = true);
    void addJobItems(FileManagerListJob* job,
                     ProjectFolderItem* baseItem,
                     const KIO::UDSEntryList& entries);

    void deleted(const QString &path);
    void dirty(const QString &path, bool isCreated = false);

    void projectClosing(IProject* project);
    void jobFinished(KJob* job);

    /// Stops watching the given folder for changes, only useful for local files.
    void stopWatcher(ProjectFolderItem* folder);
    /// Continues watching the given folder for changes.
    void continueWatcher(ProjectFolderItem* folder);
    /// Common renaming function.
    bool rename(ProjectBaseItem* item, const Path& newPath);

    QHash<IProject*, ProjectWatcher*> m_watchers;
    QHash<IProject*, QList<FileManagerListJobPtr> > m_projectJobs;
    QVector<QString> m_stoppedFolders;
    ProjectFilterManager m_filters;
    // intree dirwatching is the original mode that feeds all files
    // and directories to the dirwatcher. It takes longer to load but
    // works better for certain (large) projects that use in-tree builds.
    bool m_intreeDirWatching;
    QVector<QString> m_queuedFolders;
};

void AbstractFileManagerPluginPrivate::projectClosing(IProject* project)
{
    const auto projectJobIt = m_projectJobs.constFind(project);
    if (projectJobIt != m_projectJobs.constEnd()) {
        // make sure the import job does not live longer than the project
        // see also addLotsOfFiles test
        for (FileManagerListJob* job : *projectJobIt) {
            qCDebug(FILEMANAGER) << "killing project job:" << job;
            job->abort();
        }
        m_projectJobs.remove(project);
    }
#ifdef TIME_IMPORT_JOB
    QElapsedTimer timer;
    if (m_watchers.contains(project)) {
        timer.start();
    }
#endif
    delete m_watchers.take(project);
#ifdef TIME_IMPORT_JOB
    if (timer.isValid()) {
        qCInfo(FILEMANAGER) << "Deleting dir watcher took" << timer.elapsed() / 1000.0 << "seconds for project" << project->name();
    }
#endif
    m_filters.remove(project);
}

FileManagerListJob* AbstractFileManagerPluginPrivate::eventuallyReadFolder(ProjectFolderItem* item, bool recursive)
{
    IProject* project = item->project();
    ProjectWatcher* watcher = m_watchers.value( project, nullptr );

    // Before we create a new list job it's a good idea to
    // walk the list backwards, checking for duplicates and aborting
    // any previously started jobs loading the same directory.
    const auto jobList = m_projectJobs[project];
    auto jobListIt = jobList.constEnd();
    auto jobListHead = jobList.constBegin();
    const auto path = item->path().path();
    while (jobListIt != jobListHead) {
        auto job = *(--jobListIt);
        // check the other jobs that are still running (baseItem() != NULL)
        // abort them if justified, but only if marked as disposable (= jobs
        // started in reaction to a KDirWatch change notification).
        if (job && job->basePath().isValid() && (job->isDisposable() || !job->started())) {
            const auto jobPath = job->basePath().path();
            if (jobPath == path || (job->isRecursive() && jobPath.startsWith(path))) {
                // this job is already reloading @p item or one of its subdirs: abort it
                // because the new job will provide a more up-to-date representation.
                // Annoyingly pure watching of only directory change often gives multiple
                // dirty signals in succession.
                qCDebug(FILEMANAGER) << "aborting old job" << job << "before starting job for" << item->path();
                m_projectJobs[project].removeOne(job);
                job->abort();
            } else if (job->itemQueue().contains(item)) {
                job->removeSubDir(item);
                qCDebug(FILEMANAGER) << "unqueueing reload of old job" << job << "before starting one for" << item->path();
            } else if (job->item() == item) {
                qCWarning(FILEMANAGER) << "old job" << job << "is already reloading" << item->path();
                if (job->started()) {
                    // not much more we can do here, we have to return a valid FileManagerListJob.
                    job->setRecursive(false);
                } else {
                    job->abort();
                }
            }
        }
    }

    // FileManagerListJob detects KDEV_PROJECT_INTREE_DIRWATCHING_MODE itself
    // this is safe as long as it's not feasible to change our own env. variables
    FileManagerListJob* listJob = new FileManagerListJob( item, recursive );
    m_projectJobs[ project ] << listJob;
    qCDebug(FILEMANAGER) << "adding job" << listJob << item << item->path() << "for project" << project;

    q->connect( listJob, &FileManagerListJob::finished,
                q, [&] (KJob* job) { jobFinished(job); } );

    q->connect( listJob, &FileManagerListJob::entries,
                q, [&] (FileManagerListJob* job, ProjectFolderItem* baseItem, const KIO::UDSEntryList& entries) {
                    addJobItems(job, baseItem, entries); } );

    if (!m_intreeDirWatching) {
        q->connect( listJob, &FileManagerListJob::watchDir,
                watcher, [watcher] (const QString& path) {
                    watcher->addDir(path); } );
    }
 
    // set a relevant name (for listing in the runController)
    listJob->setObjectName(i18n("Reload of %1:%2", project->name(), item->path().toLocalFile()));
    return listJob;
}

void AbstractFileManagerPluginPrivate::jobFinished(KJob* job)
{
// removed from the 5.3 branch:
//     FileManagerListJob* gmlJob = qobject_cast<FileManagerListJob*>(job);
//     if (gmlJob && gmlJob->project()) {
//         ifDebug(qCDebug(FILEMANAGER) << job << gmlJob << gmlJob->item();)
//         m_projectJobs[ gmlJob->project() ].removeOne( gmlJob );
//      } else
    // ensure we don't keep a dangling point in our list
    // NOTE: job is potentially emitting its finished signal from its destructor
    // or the item that was used internally may have been deleted already
    for (auto& jobs : m_projectJobs) {
        if (jobs.removeOne(reinterpret_cast<FileManagerListJob*>(job))) {
            break;
        }
    }
}

void AbstractFileManagerPluginPrivate::addJobItems(FileManagerListJob* job,
                                                     ProjectFolderItem* baseItem,
                                                     const KIO::UDSEntryList& entries)
{
    qCDebug(FILEMANAGER) << "reading entries of" << baseItem->path();

    // build lists of valid files and folders with paths relative to the project folder
    Path::List files;
    Path::List folders;
    for (const KIO::UDSEntry& entry : entries) {
        QString name = entry.stringValue( KIO::UDSEntry::UDS_NAME );
        if (name == QLatin1String(".") || name == QLatin1String("..")) {
            continue;
        }

        Path path(baseItem->path(), name);

        if ( !q->isValid( path, entry.isDir(), baseItem->project() ) ) {
            continue;
        } else {
            if ( entry.isDir() ) {
                if( entry.isLink() ) {
                    const Path linkedPath = baseItem->path().cd(entry.stringValue( KIO::UDSEntry::UDS_LINK_DEST ));
                    // make sure we don't end in an infinite loop
                    if( linkedPath.isParentOf( baseItem->project()->path() ) ||
                        baseItem->project()->path().isParentOf( linkedPath ) ||
                        linkedPath == baseItem->project()->path() )
                    {
                        continue;
                    }
                }
                folders << path;
            } else {
                files << path;
            }
        }
    }

    ifDebug(qCDebug(FILEMANAGER) << "valid folders:" << folders;)
    ifDebug(qCDebug(FILEMANAGER) << "valid files:" << files;)

    // remove obsolete rows
    for ( int j = 0; j < baseItem->rowCount(); ++j ) {
        if ( ProjectFolderItem* f = baseItem->child(j)->folder() ) {
            // check if this is still a valid folder
            int index = folders.indexOf( f->path() );
            if ( index == -1 ) {
                // folder got removed or is now invalid
                delete f;
                --j;
            } else {
                // this folder already exists in the view
                folders.remove( index );
                // no need to add this item, but we still want to recurse into it
                job->addSubDir( f );
                emit q->reloadedFolderItem( f );
            }
        } else if ( ProjectFileItem* f =  baseItem->child(j)->file() ) {
            // check if this is still a valid file
            int index = files.indexOf( f->path() );
            if ( index == -1 ) {
                // file got removed or is now invalid
                ifDebug(qCDebug(FILEMANAGER) << "removing file:" << f << f->path();)
                delete f;
                --j;
            } else {
                // this file already exists in the view
                files.remove( index );
                emit q->reloadedFileItem( f );
            }
        }
    }

    // add new rows
    for (const Path& path : qAsConst(files)) {
        ProjectFileItem* file = q->createFileItem( baseItem->project(), path, baseItem );
        if (file) {
            emit q->fileAdded( file );
        }
    }
    for (const Path& path : qAsConst(folders)) {
        ProjectFolderItem* folder = q->createFolderItem( baseItem->project(), path, baseItem );
        if (folder) {
            emit q->folderAdded( folder );
            // new folder, so ignore the job's recursion setting.
            job->addSubDir( folder, true );
        }
    }
}

void AbstractFileManagerPluginPrivate::dirty(const QString& path_, bool isCreated)
{
    if (isCreated) {
        qCDebug(FILEMANAGER) << "created:" << path_;
    } else {
        qCDebug(FILEMANAGER) << "dirty:" << path_;
    }
    QFileInfo info(path_);
    if (!info.exists()) {
        // we delay handling of the signal, so maybe the path actually got removed again
        return;
    }

    ///FIXME: share memory with parent
    const Path path(path_);
    const IndexedString indexedPath(path.pathOrUrl());
    const IndexedString indexedParent(path.parent().pathOrUrl());

    QHashIterator<IProject*, ProjectWatcher*> it(m_watchers);
    while (it.hasNext()) {
        const auto p = it.next().key();
        if ( !p->projectItem()->model() ) {
            // not yet finished with loading
            // FIXME: how should this be handled? see unit test
            continue;
        }
        if ( !q->isValid(path, info.isDir(), p) ) {
            continue;
        }
        if ( info.isDir() ) {
            bool found = false;
            const auto folderItems = p->foldersForPath(indexedPath);
            for (ProjectFolderItem* folder : folderItems) {
                // exists already in this project, happens e.g. when we restart the dirwatcher
                // or if we delete and remove folders consecutively https://bugs.kde.org/show_bug.cgi?id=260741
                // or when a change is signalled in the directory contents.
                qCDebug(FILEMANAGER) << "force reload of" << path << folder;
                // schedule a reload, a priori non-recursively because we have already been
                // listed and the current change notification is not for one of our subfolders.
                // The recursion setting will be overridden if the change notification is for
                // newly created directories.
                auto job = eventuallyReadFolder( folder, false );
                job->setDisposable(true);
                job->start(1000);
                found = true;
            }
            if ( found ) {
                continue;
            }
        } else if (!p->filesForPath(indexedPath).isEmpty()) {
            // also gets triggered for kate's backup files
            continue;
        }
        if (isCreated) {
            const auto parentItems = p->foldersForPath(indexedParent);
            for (ProjectFolderItem* parentItem : parentItems) {
                if ( info.isDir() ) {
                    ProjectFolderItem* folder = q->createFolderItem( p, path, parentItem );
                    if (folder) {
                        emit q->folderAdded( folder );
                        auto job = eventuallyReadFolder( folder );
                        job->start();
                    }
                } else {
                    ProjectFileItem* file = q->createFileItem( p, path, parentItem );
                    if (file) {
                        emit q->fileAdded( file );
                    }
                }
            }
        }
    }
}

void AbstractFileManagerPluginPrivate::deleted(const QString& path_)
{
    if ( QFile::exists(path_) ) {
        // we delay handling of the signal, so maybe the path actually exists again
        return;
    }
    // ensure that the path is not inside a stopped folder
    for (const QString& folder : qAsConst(m_stoppedFolders)) {
        if (path_.startsWith(folder)) {
            return;
        }
    }
    qCDebug(FILEMANAGER) << "deleted:" << path_;

    const Path path(QUrl::fromLocalFile(path_));
    const IndexedString indexed(path.pathOrUrl());

    QHashIterator<IProject*, ProjectWatcher*> it(m_watchers);
    while (it.hasNext()) {
        const auto p = it.next().key();
        if (path == p->path()) {
            KMessageBox::error(qApp->activeWindow(),
                               i18n("The base folder of project <b>%1</b>"
                                    " got deleted or moved outside of KDevelop.\n"
                                    "The project has to be closed.", p->name()),
                               i18nc("@title:window", "Project Folder Deleted") );
            ICore::self()->projectController()->closeProject(p);
            continue;
        }
        if ( !p->projectItem()->model() ) {
            // not yet finished with loading
            // FIXME: how should this be handled? see unit test
            continue;
        }
        const auto folderItems = p->foldersForPath(indexed);
        for (ProjectFolderItem* item : folderItems) {
            // do we need to abort jobs as we did in 5.3?
            if (!m_intreeDirWatching && item->path().isLocalFile()) {
                ProjectWatcher* watcher = m_watchers.value(item->project(), nullptr);
                Q_ASSERT(watcher);
                watcher->removeDir(item->path().toLocalFile());
            }
            delete item;
        }
        const auto fileItems = p->filesForPath(indexed);
        for (ProjectFileItem* item : fileItems) {
            emit q->fileRemoved(item);
            ifDebug(qCDebug(FILEMANAGER) << "removing file" << item;)
            delete item;
        }
    }
}

bool AbstractFileManagerPluginPrivate::rename(ProjectBaseItem* item, const Path& newPath)
{
    if ( !q->isValid(newPath, true, item->project()) ) {
        int cancel = KMessageBox::warningContinueCancel( qApp->activeWindow(),
            i18n("You tried to rename '%1' to '%2', but the latter is filtered and will be hidden.\n"
                 "Do you want to continue?", item->text(), newPath.lastPathSegment()),
            QString(), KStandardGuiItem::cont(), KStandardGuiItem::cancel(), QStringLiteral("GenericManagerRenameToFiltered")
        );
        if ( cancel == KMessageBox::Cancel ) {
            return false;
        }
    }
    const auto parentItems = item->project()->foldersForPath(IndexedString(newPath.parent().pathOrUrl()));
    for (ProjectFolderItem* parent : parentItems) {
        if ( parent->folder() ) {
            stopWatcher(parent);
            const Path source = item->path();
            bool success = renameUrl( item->project(), source.toUrl(), newPath.toUrl() );
            if ( success ) {
                item->setPath( newPath );
                item->parent()->takeRow( item->row() );
                parent->appendRow( item );
                if (item->file()) {
                    emit q->fileRenamed(source, item->file());
                } else {
                    Q_ASSERT(item->folder());
                    emit q->folderRenamed(source, item->folder());
                }
            }
            continueWatcher(parent);
            return success;
        }
    }
    return false;
}

void AbstractFileManagerPluginPrivate::stopWatcher(ProjectFolderItem* folder)
{
    if ( !folder->path().isLocalFile() ) {
        return;
    }
    Q_ASSERT(m_watchers.contains(folder->project()));
    const QString path = folder->path().toLocalFile();
    m_watchers[folder->project()]->stopDirScan(path);
    m_stoppedFolders.append(path);
}

void AbstractFileManagerPluginPrivate::continueWatcher(ProjectFolderItem* folder)
{
    if ( !folder->path().isLocalFile() ) {
        return;
    }
    auto watcher = m_watchers.value(folder->project(), nullptr);
    Q_ASSERT(watcher);
    const QString path = folder->path().toLocalFile();
    if (!watcher->restartDirScan(path)) {
        // path wasn't being watched yet - can we be 100% certain of that will never happen?
        qCWarning(FILEMANAGER) << "Folder" << path << "in project" << folder->project()->name() << "wasn't yet being watched";
        watcher->addDir(path);
    }
    const int idx = m_stoppedFolders.indexOf(path);
    if (idx != -1) {
        m_stoppedFolders.remove(idx);
    }
}
//END Private

//BEGIN Plugin

AbstractFileManagerPlugin::AbstractFileManagerPlugin( const QString& componentName,
                                                      QObject *parent,
                                                      const QVariantList & /*args*/ )
    : IProjectFileManager(),
      IPlugin( componentName, parent ),
      d_ptr(new AbstractFileManagerPluginPrivate(this))
{
    connect(core()->projectController(), &IProjectController::projectClosing,
            this, [this] (IProject* project) { Q_D(AbstractFileManagerPlugin); d->projectClosing(project); });
    connect(core()->projectController()->projectModel(), &ProjectModel::rowsAboutToBeRemoved,
            this, [this] (const QModelIndex& parent, int first, int last) {
                Q_D(AbstractFileManagerPlugin);
                // cleanup list jobs to remove about-to-be-dangling pointers
                auto* model = core()->projectController()->projectModel();
                for (int i = first; i <= last; ++i) {
                    const auto index = model->index(i, 0, parent);
                    auto* item = index.data(ProjectModel::ProjectItemRole).value<ProjectBaseItem*>();
                    Q_ASSERT(item);
                    // the change "auto* job" -> "auto job" has nothing to do with other3-dirwatching patch
                    // but rather with a build issue with clang 5(?!)
                    for (auto job : d->m_projectJobs.value(item->project())) {
                        job->handleRemovedItem(item);
                    }
                }
            });
}

AbstractFileManagerPlugin::~AbstractFileManagerPlugin() = default;

IProjectFileManager::Features AbstractFileManagerPlugin::features() const
{
    return Features( Folders | Files );
}

QList<ProjectFolderItem*> AbstractFileManagerPlugin::parse( ProjectFolderItem *item )
{
    // we are async, can't return anything here
    qCDebug(FILEMANAGER) << "note: parse will always return an empty list";
    Q_UNUSED(item);
    return QList<ProjectFolderItem*>();
}

ProjectFolderItem *AbstractFileManagerPlugin::import( IProject *project )
{
    Q_D(AbstractFileManagerPlugin);

    ProjectFolderItem *projectRoot = createFolderItem( project, project->path(), nullptr );
    emit folderAdded( projectRoot );
    qCDebug(FILEMANAGER) << "imported new project" << project->name() << "at" << projectRoot->path();

    ///TODO: check if this works for remote files when something gets changed through another KDE app
    if ( project->path().isLocalFile() ) {
        auto watcher = new ProjectWatcher( project );

        // set up the signal handling; feeding the dirwatcher is handled by FileManagerListJob.
        // NOTE: We delay handling of the creation/deletion events here by one second to prevent
        //       useless or even outright wrong handling of events during common git workflows.
        //       I.e. sometimes we used to get a 'delete' event during a rebase which was never
        //       followed up by a 'created' signal, even though the file actually exists after
        //       the rebase.
        //       see also: https://bugs.kde.org/show_bug.cgi?id=404184
        connect(watcher, &KDirWatch::deleted,
                this, [this] (const QString& path) {
                    Q_D(AbstractFileManagerPlugin);
                    // queue the path so that any other change signals will be ignored
                    d->m_queuedFolders.append(path);
                    QTimer::singleShot(1000, this, [d, path]() {
                        d->m_queuedFolders.removeAll(path);
                        d->deleted(path);
                    });
                });
        if (d->m_intreeDirWatching) {
            connect(watcher, &KDirWatch::created,
                    this, [this] (const QString& path) {
                        Q_D(AbstractFileManagerPlugin);
                        // check if we're a subfolder of a folder that is already queued for reloading
                        for (const QString& folder : qAsConst(d->m_queuedFolders)) {
                            if (path.startsWith(folder)) {
                                return;
                            }
                        }
                        d->m_queuedFolders.append(path);
                        QTimer::singleShot(1000, this, [d, path]() {
                            d->dirty(path, true);
                            d->m_queuedFolders.removeOne(path);
                        });
                    });
            watcher->addDir(project->path().toLocalFile(), KDirWatch::WatchSubDirs | KDirWatch:: WatchFiles );
        } else {
            connect(watcher, &KDirWatch::dirty,
                    this, [this] (const QString& path) {
                        Q_D(AbstractFileManagerPlugin);
                        // check if we're a subfolder of a folder that is already queued for reloading
                        for (const QString& folder : qAsConst(d->m_queuedFolders)) {
                            if (path.startsWith(folder)) {
                                return;
                            }
                        }
                        d->m_queuedFolders.append(path);
                        QTimer::singleShot(1000, this, [d, path]() {
                            d->dirty(path);
                            d->m_queuedFolders.removeOne(path);
                        });
                    });
        }
        d->m_watchers[project] = watcher;
    }

    d->m_filters.add(project);

    return projectRoot;
}

KJob* AbstractFileManagerPlugin::createImportJob(ProjectFolderItem* item)
{
    Q_D(AbstractFileManagerPlugin);

    return d->eventuallyReadFolder(item);
}

bool AbstractFileManagerPlugin::reload( ProjectFolderItem* item )
{
    Q_D(AbstractFileManagerPlugin);

    qCDebug(FILEMANAGER) << "reloading item" << item->path();
    auto job = d->eventuallyReadFolder( item->folder() );
    job->start();
    return true;
}

ProjectFolderItem* AbstractFileManagerPlugin::addFolder( const Path& folder,
        ProjectFolderItem * parent )
{
    Q_D(AbstractFileManagerPlugin);

    qCDebug(FILEMANAGER) << "adding folder" << folder << "to" << parent->path();
    ProjectFolderItem* created = nullptr;
    d->stopWatcher(parent);
    if ( createFolder(folder.toUrl()) ) {
        created = createFolderItem( parent->project(), folder, parent );
        if (created) {
            emit folderAdded(created);
        }
    }
    d->continueWatcher(parent);
    return created;
}


ProjectFileItem* AbstractFileManagerPlugin::addFile( const Path& file,
        ProjectFolderItem * parent )
{
    Q_D(AbstractFileManagerPlugin);

    qCDebug(FILEMANAGER) << "adding file" << file << "to" << parent->path();
    ProjectFileItem* created = nullptr;
    d->stopWatcher(parent);
    if ( createFile(file.toUrl()) ) {
        created = createFileItem( parent->project(), file, parent );
        if (created) {
            emit fileAdded(created);
        }
    }
    d->continueWatcher(parent);
    return created;
}

bool AbstractFileManagerPlugin::renameFolder(ProjectFolderItem* folder, const Path& newPath)
{
    Q_D(AbstractFileManagerPlugin);

    qCDebug(FILEMANAGER) << "trying to rename a folder:" << folder->path() << newPath;
    return d->rename(folder, newPath);
}

bool AbstractFileManagerPlugin::renameFile(ProjectFileItem* file, const Path& newPath)
{
    Q_D(AbstractFileManagerPlugin);

    qCDebug(FILEMANAGER) << "trying to rename a file:" << file->path() << newPath;
    return d->rename(file, newPath);
}

bool AbstractFileManagerPlugin::removeFilesAndFolders(const QList<ProjectBaseItem*> &items)
{
    Q_D(AbstractFileManagerPlugin);

    bool success = true;
    for (ProjectBaseItem* item : items) {
        Q_ASSERT(item->folder() || item->file());

        ProjectFolderItem* parent = parentFolder(item);
        d->stopWatcher(parent);

        success &= removeUrl(parent->project(), item->path().toUrl(), true);
        if ( success ) {
            if (item->file()) {
                emit fileRemoved(item->file());
            } else {
                Q_ASSERT(item->folder());
                emit folderRemoved(item->folder());
            }
            delete item;
        }

        d->continueWatcher(parent);
        if ( !success )
            break;
    }
    return success;
}

bool AbstractFileManagerPlugin::moveFilesAndFolders(const QList< ProjectBaseItem* >& items, ProjectFolderItem* newParent)
{
    Q_D(AbstractFileManagerPlugin);

    bool success = true;
    for (ProjectBaseItem* item : items) {
        Q_ASSERT(item->folder() || item->file());

        ProjectFolderItem* oldParent = parentFolder(item);
        d->stopWatcher(oldParent);
        d->stopWatcher(newParent);

        const Path oldPath = item->path();
        const Path newPath(newParent->path(), item->baseName());

        success &= renameUrl(oldParent->project(), oldPath.toUrl(), newPath. toUrl());
        if ( success ) {
            if (item->file()) {
                emit fileRemoved(item->file());
            } else {
                emit folderRemoved(item->folder());
            }
            delete item;
            KIO::Job *readJob = d->eventuallyReadFolder(newParent);
            // reload first level synchronously, deeper levels will run async
            // this is required for code that expects the new item to exist after
            // this method finished
            readJob->exec();
        }

        d->continueWatcher(oldParent);
        d->continueWatcher(newParent);
        if ( !success )
            break;
    }
    return success;
}

bool AbstractFileManagerPlugin::copyFilesAndFolders(const Path::List& items, ProjectFolderItem* newParent)
{
    Q_D(AbstractFileManagerPlugin);

    bool success = true;
    for (const Path& item : items) {
        d->stopWatcher(newParent);

        success &= copyUrl(newParent->project(), item.toUrl(), newParent->path().toUrl());
        if ( success ) {
            KIO::Job *readJob = d->eventuallyReadFolder(newParent);
            // reload first level synchronously, deeper levels will run async
            // this is required for code that expects the new item to exist after
            // this method finished
            readJob->exec();
        }

        d->continueWatcher(newParent);
        if ( !success )
            break;
    }
    return success;
}

bool AbstractFileManagerPlugin::isValid( const Path& path, const bool isFolder,
                                         IProject* project ) const
{
    Q_D(const AbstractFileManagerPlugin);

    return d->m_filters.isValid( path, isFolder, project );
}

ProjectFileItem* AbstractFileManagerPlugin::createFileItem( IProject* project, const Path& path,
                                                            ProjectBaseItem* parent )
{
    return new ProjectFileItem( project, path, parent );
}

ProjectFolderItem* AbstractFileManagerPlugin::createFolderItem( IProject* project, const Path& path,
                                                                ProjectBaseItem* parent )
{
    return new ProjectFolderItem( project, path, parent );
}

KDirWatch* AbstractFileManagerPlugin::projectWatcher( IProject* project ) const
{
    Q_D(const AbstractFileManagerPlugin);

    return d->m_watchers.value( project, nullptr );
}

//END Plugin

#include "moc_abstractfilemanagerplugin.cpp"
