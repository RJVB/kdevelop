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

#ifndef KDEVPLATFORM_FILEMANAGERLISTJOB_H
#define KDEVPLATFORM_FILEMANAGERLISTJOB_H

#include <KIO/Job>
#include <QQueue>
#include <QSemaphore>

// uncomment to time import jobs
// #define TIME_IMPORT_JOB

#ifdef TIME_IMPORT_JOB
#include <QElapsedTimer>
#endif


#include "path.h"

namespace KDevelop
{
class ProjectFolderItem;
class ProjectBaseItem;
class IProject;

class FileManagerListJob : public KIO::Job
{
    Q_OBJECT

public:
    /**
     * KIO::Job variant that lists the files in a project folder. The list
     * is generated recursively unless @a recursive==false and the folder
     * has already been indexed.
     **/
    explicit FileManagerListJob(ProjectFolderItem* item, bool recursive = true);
    virtual ~FileManagerListJob();

    ProjectFolderItem* item() const;
    IProject* project() const;
    QQueue<ProjectFolderItem*> itemQueue() const;
    Path basePath() const;

    void addSubDir(ProjectFolderItem* item, bool forceRecursive = false);
    void removeSubDir(ProjectFolderItem* item);
    void handleRemovedItem(ProjectBaseItem* item);

    void abort();
    bool doKill() override;
    void start() override;
    void start(int msDelay);
    bool started() const { return m_started; }

    /**
     * will/should this job recurse over all subdirs?
     */
    bool isRecursive() const { return m_recursive; }
    void setRecursive(bool enabled);

    /**
     * Is/should this be a job that can be aborted, for
     * instance when it's not been started on the user's
     * explicit request?
     * Jobs are never disposable by default.
     */
    bool isDisposable() const { return m_disposable; }
    void setDisposable(bool enabled);

Q_SIGNALS:
    void entries(FileManagerListJob* job, ProjectFolderItem* baseItem,
                 const KIO::UDSEntryList& entries);
    void nextJob();
    void watchDir(const QString& path);

private Q_SLOTS:
    void slotEntries(KIO::Job* job, const KIO::UDSEntryList& entriesIn );
    void slotResult(KJob* job) override;
    void handleResults(const KIO::UDSEntryList& entries);
    void startNextJob();

private:

    QQueue<ProjectFolderItem*> m_listQueue;
    /// current base dir
    ProjectFolderItem* m_item;
    /// current project
    IProject* m_project;
    /// entrypoint
    Path m_basePath;
    KIO::UDSEntryList entryList;
    // kill does not delete the job instantaneously
    QAtomicInt m_aborted;
    QSemaphore m_listing;

#ifdef TIME_IMPORT_JOB
    QElapsedTimer m_timer;
    QElapsedTimer m_subTimer;
    qint64 m_subWaited = 0;
#endif
    bool m_emitWatchDir;
    int m_killCount = 0;
    bool m_recursive;
    bool m_started;
    bool m_disposable;
};

}

#endif // KDEVPLATFORM_FILEMANAGERLISTJOB_H
