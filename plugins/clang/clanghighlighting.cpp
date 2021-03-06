/*
    This file is part of KDevelop

    Copyright 2015 Sergey Kalinichev <kalinichev.so.0@gmail.com>

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

#include "clanghighlighting.h"

#include "duchain/macrodefinition.h"

#include <language/duchain/topducontext.h>

using namespace KDevelop;

class ClangHighlighting::Instance : public KDevelop::CodeHighlightingInstance
{
public:
    explicit Instance(const KDevelop::CodeHighlighting* highlighting);

    KDevelop::HighlightingEnumContainer::Types typeForDeclaration(KDevelop::Declaration* dec, KDevelop::DUContext* context) const override
    {
        if (auto macro = dynamic_cast<MacroDefinition*>(dec)) {
            if (macro->isFunctionLike()) {
                return KDevelop::HighlightingEnumContainer::MacroFunctionLikeType;
            }
        }

        return CodeHighlightingInstance::typeForDeclaration(dec, context);
    }

    bool useRainbowColor(KDevelop::Declaration* dec) const override
    {
        return dec->context()->type() == DUContext::Function || dec->context()->type() == DUContext::Other;
    }

};

ClangHighlighting::Instance::Instance(const KDevelop::CodeHighlighting* highlighting)
    : KDevelop::CodeHighlightingInstance(highlighting)
{
}

ClangHighlighting::ClangHighlighting(QObject* parent)
    : KDevelop::CodeHighlighting(parent)
{
}

KDevelop::CodeHighlightingInstance* ClangHighlighting::createInstance() const
{
    return new Instance(this);
}
