/* This file is part of KDevelop
    Copyright 2009  Radu Benea <radub82@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "filemanagerlistjob.h"

#include <interfaces/iproject.h>
#include <project/projectmodel.h>

#include "path.h"
#include "debug.h"
// KF
#include <kio_version.h>
// Qt
#include <QtConcurrentRun>
#include <QDir>
#include <QTimer>

#include <interfaces/icore.h>
#include <interfaces/iruncontroller.h>

using namespace KDevelop;

class KDevelop::RunControllerProxy : public KJob
{
public:
    RunControllerProxy(FileManagerListJob* proxied)
        : KJob()
        , m_proxied(proxied)
    {
        setCapabilities(KJob::Killable);
        setObjectName(proxied->objectName());
        ICore::self()->runController()->registerJob(this);
    }

    ~RunControllerProxy()
    {
        done();
    }

    // we don't run anything ourselves
    void start()
    {}

    bool doKill()
    {
        if (m_proxied) {
            qCDebug(PROJECT) << "Aborting" << m_proxied;
            m_proxied->abort();
            done();
        }
        return true;
    }

    void done()
    {
        if (m_proxied) {
            ICore::self()->runController()->unregisterJob(this);
            m_proxied = nullptr;
        }
    }

    FileManagerListJob* m_proxied;
};

FileManagerListJob::FileManagerListJob(ProjectFolderItem* item)
    : KIO::Job(), m_item(item), m_baseItem(item), m_aborted(false)
    , m_emitWatchDir(!qEnvironmentVariableIsSet("KDEV_PROJECT_INTREE_DIRWATCHING_MODE"))
    , m_rcProxy(nullptr)
{
    qRegisterMetaType<KIO::UDSEntryList>("KIO::UDSEntryList");
    qRegisterMetaType<KIO::Job*>();
    qRegisterMetaType<KJob*>();

    /* the following line is not an error in judgment, apparently starting a
     * listJob while the previous one hasn't self-destructed takes a lot of time,
     * so we give the job a chance to selfdestruct first */
    connect( this, &FileManagerListJob::nextJob, this, &FileManagerListJob::startNextJob, Qt::QueuedConnection );

    addSubDir(item);

#ifdef TIME_IMPORT_JOB
    m_timer.start();
#endif
}

FileManagerListJob::~FileManagerListJob()
{
    if (m_rcProxy) {
        m_rcProxy->done();
        m_rcProxy->deleteLater();
    }
    m_item = m_baseItem = nullptr;
    m_rcProxy = nullptr;
}

ProjectFolderItem* FileManagerListJob::item() const
{
    return m_item;
}

QQueue<ProjectFolderItem*> FileManagerListJob::itemQueue() const
{
    return m_listQueue;
}

ProjectFolderItem* FileManagerListJob::baseItem() const
{
    return m_baseItem;
}

void FileManagerListJob::addSubDir( ProjectFolderItem* item )
{
    if (m_aborted) {
        return;
    }

    Q_ASSERT(!m_listQueue.contains(item));
    Q_ASSERT(!m_item || m_item == item || m_item->path().isDirectParentOf(item->path()));

    m_listQueue.enqueue(item);
}

void FileManagerListJob::removeSubDir(ProjectFolderItem* item)
{
    if (m_aborted) {
        return;
    }

    m_listQueue.removeAll(item);
}

void FileManagerListJob::slotEntries(KIO::Job* job, const KIO::UDSEntryList& entriesIn)
{
    Q_UNUSED(job);
    entryList.append(entriesIn);
}

void FileManagerListJob::startNextJob()
{
    if ( m_listQueue.isEmpty() || m_aborted ) {
        return;
    }

#ifdef TIME_IMPORT_JOB
    m_subTimer.start();
#endif

    m_item = m_listQueue.dequeue();
    if (m_item->path().isLocalFile()) {
        // optimized version for local projects using QDir directly
        QtConcurrent::run([this] (const Path& path) {
            if (m_aborted) {
                return;
            }
            QDir dir(path.toLocalFile());
            const auto entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden);
            if (m_aborted) {
                return;
            }
            if (m_emitWatchDir) {
                // signal that this directory has to be watched
                emit watchDir(path.toLocalFile());
            }
            KIO::UDSEntryList results;
            std::transform(entries.begin(), entries.end(), std::back_inserter(results), [] (const QFileInfo& info) -> KIO::UDSEntry {
                KIO::UDSEntry entry;
#if KIO_VERSION < QT_VERSION_CHECK(5,48,0)
                entry.insert(KIO::UDSEntry::UDS_NAME, info.fileName());
#else
                entry.fastInsert(KIO::UDSEntry::UDS_NAME, info.fileName());
#endif
                if (info.isDir()) {
#if KIO_VERSION < QT_VERSION_CHECK(5,48,0)
                    entry.insert(KIO::UDSEntry::UDS_FILE_TYPE, QT_STAT_DIR);
#else
                    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, QT_STAT_DIR);
#endif
                }
                if (info.isSymLink()) {
#if KIO_VERSION < QT_VERSION_CHECK(5,48,0)
                    entry.insert(KIO::UDSEntry::UDS_LINK_DEST, info.symLinkTarget());
#else
                    entry.fastInsert(KIO::UDSEntry::UDS_LINK_DEST, info.symLinkTarget());
#endif
                }
                return entry;
            });
            if (!m_aborted) {
                QMetaObject::invokeMethod(this, "handleResults", Q_ARG(KIO::UDSEntryList, results));
            }
        }, m_item->path());
    } else {
        KIO::ListJob* job = KIO::listDir( m_item->path().toUrl(), KIO::HideProgressInfo );
        job->addMetaData(QStringLiteral("details"), QStringLiteral("0"));
        job->setParentJob( this );
        connect( job, &KIO::ListJob::entries,
                this, &FileManagerListJob::slotEntries );
        connect( job, &KIO::ListJob::result, this, &FileManagerListJob::slotResult );
    }
}

void FileManagerListJob::slotResult(KJob* job)
{
    if (m_aborted) {
        return;
    }

    if( job && job->error() ) {
        qCDebug(FILEMANAGER) << "error in list job:" << job->error() << job->errorString();
    }

    handleResults(entryList);
    entryList.clear();
}


void FileManagerListJob::handleResults(const KIO::UDSEntryList& entriesIn)
{
    if (m_aborted) {
        return;
    }

#ifdef TIME_IMPORT_JOB
    {
        auto waited = m_subTimer.elapsed();
        m_subWaited += waited;
        qCDebug(PROJECT) << "TIME FOR SUB JOB:" << waited << m_subWaited;
    }
#endif

    emit entries(this, m_item, entriesIn);

    if( m_listQueue.isEmpty() ) {
        m_baseItem = nullptr;
        emitResult();
        if (m_rcProxy) {
            m_rcProxy->done();
        }

#ifdef TIME_IMPORT_JOB
        qCDebug(PROJECT) << "TIME FOR LISTJOB:" << m_timer.elapsed();
#endif
    } else {
        emit nextJob();
    }
}

void FileManagerListJob::abort()
{
    m_aborted = true;
    m_listQueue.clear();

    if (m_rcProxy) {
        m_rcProxy->done();
    }

    m_baseItem = nullptr;

    bool killed = kill();
    m_killCount += 1;
    if (!killed) {
        // let's check if kill failures aren't because of trying to kill a corpse
        qCWarning(PROJECT) << "failed to kill" << this << "kill count=" << m_killCount;
    }
    Q_ASSERT(killed);
    Q_UNUSED(killed);
}

void FileManagerListJob::start(int msDelay)
{
    if (msDelay > 0) {
        QTimer::singleShot(msDelay, this, SLOT(start()));
    } else {
        start();
    }
}

void FileManagerListJob::start()
{
    if (!m_rcProxy) {
        m_rcProxy = new RunControllerProxy(this);
    }
    startNextJob();
}
