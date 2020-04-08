/*
    This file is part of KDevelop

    Copyright 2013 Olivier de Gaalon <olivier.jg@gmail.com>
    Copyright 2013 Milian Wolff <mail@milianw.de>

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

#ifndef CLANGPARSEJOB_H
#define CLANGPARSEJOB_H

#include <QHash>

#include <language/backgroundparser/parsejob.h>
#include "duchain/clangparsingenvironment.h"
#include "duchain/unsavedfile.h"
#include "duchain/parsesession.h"

class ClangSupport;

class ClangParseJob : public KDevelop::ParseJob
{
    Q_OBJECT
public:
    ClangParseJob(const KDevelop::IndexedString& url,
                  KDevelop::ILanguageSupport* languageSupport);

    ClangSupport* clang() const;

    enum CustomFeatures {
        Rescheduled = (KDevelop::TopDUContext::LastFeature << 1),
        AttachASTWithoutUpdating = (Rescheduled << 1), ///< Used when context is up to date, but has no AST attached.
        UpdateHighlighting = (AttachASTWithoutUpdating << 1) ///< Used when we only need to update highlighting
    };

protected:
    void run(ThreadWeaver::JobPointer self, ThreadWeaver::Thread *thread) override;

    const KDevelop::ParsingEnvironment* environment() const override;

private:
    QExplicitlySharedDataPointer<ParseSessionData> createSessionData() const;

    ClangParsingEnvironment m_environment;
    QVector<UnsavedFile> m_unsavedFiles;
    ParseSessionData::Options m_options;
    bool m_tuDocumentIsUnsaved = false;
    QHash<KDevelop::IndexedString, KDevelop::ModificationRevision> m_unsavedRevisions;
};

#endif // CLANGPARSEJOB_H
