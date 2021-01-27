/*
 * This file is part of KDevelop
 *
 * Copyright 2009 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "colorcache.h"

#include "configurablecolors.h"
#include "codehighlighting.h"

#include <KColorScheme>

#include "../../interfaces/icore.h"
#include "../../interfaces/ilanguagecontroller.h"
#include "../../interfaces/icompletionsettings.h"
#include "../../interfaces/idocument.h"
#include "../../interfaces/idocumentcontroller.h"
#include "../interfaces/ilanguagesupport.h"
#include "../duchain/duchain.h"
#include "../duchain/duchainlock.h"
#include <debug.h>
#include "widgetcolorizer.h"

#include <ktexteditor_version.h>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <KTextEditor/ConfigInterface>

#define ifDebug(x)

namespace KDevelop {
ColorCache* ColorCache::m_self = nullptr;

ColorCache::ColorCache(QObject* parent)
    : QObject(parent)
    , m_defaultColors(nullptr)
    , m_validColorCount(0)
    , m_colorOffset(0)
    , m_localColorRatio(0)
    , m_globalColorRatio(0)
    , m_boldDeclarations(true)
{
    Q_ASSERT(m_self == nullptr);

    updateColorsFromScheme(); // default / fallback
    updateColorsFromSettings();

    connect(ICore::self()->languageController()->completionSettings(), &ICompletionSettings::settingsChanged,
            this, &ColorCache::updateColorsFromSettings, Qt::QueuedConnection);

    connect(ICore::self()->documentController(), &IDocumentController::documentActivated,
            this, &ColorCache::slotDocumentActivated);

    bool hadDoc = tryActiveDocument();

    updateInternal();

    m_self = this;

    if (!hadDoc) {
        // try to update later on again
        QMetaObject::invokeMethod(this, "tryActiveDocument", Qt::QueuedConnection);
    }
}

bool ColorCache::tryActiveDocument()
{
    KTextEditor::View* view = ICore::self()->documentController()->activeTextDocumentView();
    if (view) {
        updateColorsFromView(view);
        return true;
    }
    return false;
}

ColorCache::~ColorCache()
{
    m_self = nullptr;
    delete m_defaultColors;
    m_defaultColors = nullptr;
}

ColorCache* ColorCache::self()
{
    if (!m_self) {
        m_self = new ColorCache;
    }
    return m_self;
}

void ColorCache::generateColors()
{
    if (m_defaultColors) {
        delete m_defaultColors;
    }

    m_defaultColors = new CodeHighlightingColors(this);

    // Primary colors taken from: http://colorbrewer2.org/?type=qualitative&scheme=Paired&n=12
    const QColor colors[] = {
        {"#b15928"}, {"#ff7f00"}, {"#b2df8a"}, {"#33a02c"}, {"#a6cee3"},
        {"#1f78b4"}, {"#6a3d9a"}, {"#cab2d6"}, {"#e31a1c"}, {"#fb9a99"}
    };
    const int colorCount = std::extent<decltype(colors)>::value;

    // Supplementary colors generated by: http://tools.medialab.sciences-po.fr/iwanthue/
    const QColor supplementaryColors[] = {
        {"#D33B67"}, {"#5EC764"}, {"#6CC82D"}, {"#995729"}, {"#FB4D84"},
        {"#4B8828"}, {"#D847D0"}, {"#B56AC5"}, {"#E96F0C"}, {"#DC7161"},
        {"#4D7279"}, {"#01AAF1"}, {"#D2A237"}, {"#F08CA5"}, {"#C83E93"},
        {"#5D7DF7"}, {"#EFBB51"}, {"#108BBB"}, {"#5C84B8"}, {"#02F8BC"},
        {"#A5A9F7"}, {"#F28E64"}, {"#A461E6"}, {"#6372D3"}
    };
    const int supplementaryColorCount = std::extent<decltype(supplementaryColors)>::value;

    m_colors.clear();
    m_colors.reserve(colorCount + supplementaryColorCount);

    for (const auto& color: colors) {
        m_colors.append(blendLocalColor(color));
    }

    m_primaryColorCount = m_colors.count();

    for (const auto& color: supplementaryColors) {
        m_colors.append(blendLocalColor(color));
    }

    m_validColorCount = m_colors.count();
}

void ColorCache::slotDocumentActivated()
{
    KTextEditor::View* view = ICore::self()->documentController()->activeTextDocumentView();
    ifDebug(qCDebug(LANGUAGE) << "doc activated:" << doc; )
    if (view) {
        updateColorsFromView(view);
    }
}

void ColorCache::slotViewSettingsChanged()
{
    auto* view = qobject_cast<KTextEditor::View*>(sender());
    Q_ASSERT(view);

    ifDebug(qCDebug(LANGUAGE) << "settings changed" << view; )
    updateColorsFromView(view);
}

void ColorCache::updateColorsFromView(KTextEditor::View* view)
{
    if (!view) {
        // yeah, the HighlightInterface methods returning an Attribute
        // require a View... kill me for that mess
        return;
    }

    QColor foreground(QColor::Invalid);
    QColor background(QColor::Invalid);

    KTextEditor::Attribute::Ptr style = view->defaultStyleAttribute(KTextEditor::dsNormal);
    foreground = style->foreground().color();
    if (style->hasProperty(QTextFormat::BackgroundBrush)) {
        background = style->background().color();
    }

    // FIXME: this is in kateview
//     qCDebug(LANGUAGE) << "got foreground:" << foreground.name() << "old is:" << m_foregroundColor.name();
//NOTE: this slot is defined in KatePart > 4.4, see ApiDocs of the ConfigInterface

    // the signal is not defined in ConfigInterface, but according to the docs it should be
    // can't use new signal slot syntax here, since ConfigInterface is not a QObject
    if (KTextEditor::View* view = m_view.data()) {
        Q_ASSERT(qobject_cast<KTextEditor::ConfigInterface*>(view));
        // we only listen to a single view, i.e. the active one
#if KTEXTEDITOR_VERSION >= QT_VERSION_CHECK(5, 79, 0)
        disconnect(view, &KTextEditor::View::configChanged, this, &ColorCache::slotViewSettingsChanged);
#else
        disconnect(view, SIGNAL(configChanged()), this, SLOT(slotViewSettingsChanged()));
#endif
    }
    Q_ASSERT(qobject_cast<KTextEditor::ConfigInterface*>(view));
#if KTEXTEDITOR_VERSION >= QT_VERSION_CHECK(5, 79, 0)
    connect(view, &KTextEditor::View::configChanged, this, &ColorCache::slotViewSettingsChanged);
#else
    connect(view, SIGNAL(configChanged()), this, SLOT(slotViewSettingsChanged()));
#endif
    m_view = view;

    if (!foreground.isValid()) {
        // fallback to colorscheme variant
        ifDebug(qCDebug(LANGUAGE) << "updating from scheme"; )
        updateColorsFromScheme();
    } else if (m_foregroundColor != foreground || m_backgroundColor != background) {
        m_foregroundColor = foreground;
        m_backgroundColor = background;

        ifDebug(qCDebug(LANGUAGE) << "updating from document"; )
        update();
    }
}

void ColorCache::updateColorsFromScheme()
{
    KColorScheme scheme(QPalette::Normal, KColorScheme::View);

    QColor foreground = scheme.foreground(KColorScheme::NormalText).color();
    QColor background = scheme.background(KColorScheme::NormalBackground).color();

    if (foreground != m_foregroundColor || background != m_backgroundColor) {
        m_foregroundColor = foreground;
        m_backgroundColor = background;
        update();
    }
}

void ColorCache::updateColorsFromSettings()
{
    int localRatio = ICore::self()->languageController()->completionSettings()->localColorizationLevel();
    int globalRatio = ICore::self()->languageController()->completionSettings()->globalColorizationLevel();
    bool boldDeclartions = ICore::self()->languageController()->completionSettings()->boldDeclarations();

    if (localRatio != m_localColorRatio || globalRatio != m_globalColorRatio) {
        m_localColorRatio = localRatio;
        m_globalColorRatio = globalRatio;
        update();
    }
    if (boldDeclartions != m_boldDeclarations) {
        m_boldDeclarations = boldDeclartions;
        update();
    }
}

void ColorCache::update()
{
    if (!m_self) {
        ifDebug(qCDebug(LANGUAGE) << "not updating - still initializating"; )
        // don't update on startup, updateInternal is called directly there
        return;
    }

    QMetaObject::invokeMethod(this, "updateInternal", Qt::QueuedConnection);
}

void ColorCache::updateInternal()
{
    ifDebug(qCDebug(LANGUAGE) << "update internal" << m_self; )
    generateColors();

    if (!m_self) {
        // don't do anything else fancy on startup
        return;
    }

    emit colorsGotChanged();

    // rehighlight open documents
    if (!ICore::self() || ICore::self()->shuttingDown()) {
        return;
    }
    const auto documents = ICore::self()->documentController()->openDocuments();
    for (IDocument* doc : documents) {
        const auto languages = ICore::self()->languageController()->languagesForUrl(doc->url());
        for (const auto lang : languages) {
            ReferencedTopDUContext top;
            {
                DUChainReadLocker lock;
                top = lang->standardContext(doc->url());
            }

            if (top) {
                if (ICodeHighlighting* highlighting = lang->codeHighlighting()) {
                    highlighting->highlightDUChain(top);
                }
            }
        }
    }
}

QColor ColorCache::blend(QColor color, uchar ratio) const
{
    Q_ASSERT(m_backgroundColor.isValid());
    Q_ASSERT(m_foregroundColor.isValid());
    return WidgetColorizer::blendForeground(color, float( ratio ) / float( 0xff ), m_foregroundColor,
                                            m_backgroundColor);
}

QColor ColorCache::blendBackground(QColor color, uchar ratio) const
{
    return WidgetColorizer::blendBackground(color, float( ratio ) / float( 0xff ), m_foregroundColor,
                                            m_backgroundColor);
}

QColor ColorCache::blendGlobalColor(QColor color) const
{
    return blend(color, m_globalColorRatio);
}

QColor ColorCache::blendLocalColor(QColor color) const
{
    return blend(color, m_localColorRatio);
}

CodeHighlightingColors* ColorCache::defaultColors() const
{
    Q_ASSERT(m_defaultColors);
    return m_defaultColors;
}

QColor ColorCache::generatedColor(uint num) const
{
    return num > ( uint )m_colors.size() ? foregroundColor() : m_colors[num];
}

uint ColorCache::validColorCount() const
{
    return m_validColorCount;
}

uint ColorCache::primaryColorCount() const
{
    return m_primaryColorCount;
}

QColor ColorCache::foregroundColor() const
{
    return m_foregroundColor;
}
}
