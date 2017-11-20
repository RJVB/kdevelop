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

#include "projectwatcher.h"
#include "iproject.h"
#include "projectfiltermanager.h"
#include "path.h"

#include <KDirWatch>

using namespace KDevelop;

KDevelop::ProjectWatcher::ProjectWatcher(IProject* project, ProjectFilterManager* filter)
    : KDirWatch(project)
    , m_project(project)
    , m_filter(filter)
    , m_watchedCount(0)
{}

void KDevelop::ProjectWatcher::addDir(const QString& path, WatchModes watchModes)
{
    if (/*m_filter->isValid(Path(path), true, m_project) &&*/ !contains(path)) {
        KDirWatch::addDir(path, watchModes);
        m_watchedCount += 1;
    }
}

void KDevelop::ProjectWatcher::removeDir(const QString& path)
{
    if (contains(path)) {
        KDirWatch::removeDir(path);
        m_watchedCount -= 1;
    }
}

int KDevelop::ProjectWatcher::size() const
{
    return m_watchedCount;
}

