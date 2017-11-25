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

#include <QtConcurrentRun>
#include <QDir>
#include <QTimer>

using namespace KDevelop;

FileManagerListJob::FileManagerListJob(ProjectFolderItem* item)
    : KIO::Job(), m_item(item), m_baseItem(item), m_aborted(false)
    , m_emitWatchDir(!qEnvironmentVariableIsSet("KDEV_PROJECT_INTREE_DIRWATCHING_MODE"))
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
    Q_ASSERT(!m_listQueue.contains(item));
    Q_ASSERT(!m_item || m_item == item || m_item->path().isDirectParentOf(item->path()));

    m_listQueue.enqueue(item);
}

void FileManagerListJob::removeSubDir(ProjectFolderItem* item)
{
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
                entry.insert(KIO::UDSEntry::UDS_NAME, info.fileName());
                if (info.isDir()) {
                    entry.insert(KIO::UDSEntry::UDS_FILE_TYPE, QT_STAT_DIR);
                }
                if (info.isSymLink()) {
                    entry.insert(KIO::UDSEntry::UDS_LINK_DEST, info.symLinkTarget());
                }
                return entry;
            });
            QMetaObject::invokeMethod(this, "handleResults", Q_ARG(KIO::UDSEntryList, results));
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
        emitResult();

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

    bool killed = kill();
    m_killCount += 1;
    if (!killed) {
        qCWarning(PROJECT) << "failed to kill" << this << "kill count=" << m_killCount;
    }
    Q_ASSERT(killed);
    Q_UNUSED(killed);
}

void FileManagerListJob::start(int msDelay)
{
    QTimer::singleShot(msDelay, this, SLOT(startNextJob()));
}

void FileManagerListJob::start()
{
    start(0);
}
