/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2017 René Bertin <rjvbertin@gmail.com>                      *
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

#ifndef KDEVPLATFORM_PROJECTWATCHER_H
#define KDEVPLATFORM_PROJECTWATCHER_H

#include "projectexport.h"

#include <KDirWatch>

namespace KDevelop {

class IProject;
class ProjectFilterManager;

class KDEVPLATFORMPROJECT_EXPORT ProjectWatcher : public KDirWatch
{
    Q_OBJECT
public:
    /**
     * Create a dirwatcher for @p project based on KDirWatch but
     * that will add or remove each directory only once.
     */
    explicit ProjectWatcher(IProject* project);

    /**
     * Add directory @p path to the project dirwatcher if it is not
     * already being watched.
     */
    void addDir(const QString& path, WatchModes watchModes = WatchDirOnly);
    void removeDir(const QString& path);
    /**
     * Add file @p file to the project dirwatcher if it is not
     * already being watched.
     */
    void addFile(const QString& file);
    void removeFile(const QString& file);

    /**
     * return the current number of directories being watched.
     */
    int size() const;

private:
    int m_watchedCount;
};

}
#endif //KDEVPLATFORM_PROJECTWATCHER_H
