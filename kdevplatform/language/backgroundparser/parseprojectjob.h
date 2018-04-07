/*
   Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>
 
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

#ifndef KDEVPLATFORM_PARSEPROJECTJOB_H
#define KDEVPLATFORM_PARSEPROJECTJOB_H

#include <kjob.h>
#include <serialization/indexedstring.h>
#include <language/languageexport.h>

namespace KDevelop {
class ReferencedTopDUContext;
class IProject;

///A job that parses all project-files in the given project
///Deletes itself as soon as the project is deleted
class KDEVPLATFORMLANGUAGE_EXPORT ParseProjectJob : public KJob
{
    Q_OBJECT
public:
    explicit ParseProjectJob(KDevelop::IProject* project, bool forceUpdate = false, bool forceAll = false );
    ~ParseProjectJob() override;
    void start() override;
    bool doKill() override;

private Q_SLOTS:
    void deleteNow();
    void updateReady(const KDevelop::IndexedString& url, KDevelop::ReferencedTopDUContext topContext);

private:
    void updateProgress();

private:
    const QScopedPointer<class ParseProjectJobPrivate> d;
    bool forceAll;
};

}

#endif // KDEVPLATFORM_PARSEPROJECTJOB_H
