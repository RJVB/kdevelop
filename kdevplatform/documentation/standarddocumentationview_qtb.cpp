/*
 * This file is part of KDevelop
 * Copyright 2010 Aleix Pol Gonzalez <aleixpol@kde.org>
 * Copyright 2016 Igor Kushnir <igorkuo@gmail.com>
 * Copyright 2017 René J.V. Bertin <rjvbertin@gmail.com>
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

#include "standarddocumentationview.h"
#include "documentationfindwidget.h"
#include "debug.h"

#include <util/zoomcontroller.h>

#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>

#include <QVBoxLayout>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDesktopServices>

#include <QTextBrowser>

#include "standarddocumentationview_p.h"

using namespace KDevelop;

class KDevelop::HelpViewer : public QTextBrowser
{
    Q_OBJECT
public:

    HelpViewer(StandardDocumentationView* parent)
        : QTextBrowser(parent)
        , m_parent(parent)
        , m_loadFinished(false)
        , m_restoreTimer(0)
    {}

    void setSource(const QUrl& url) override
    {
        if (StandardDocumentationView::isUrlSchemeSupported(url)) {
            m_loadFinished = false;
            QTextBrowser::setSource(url);
        } else {
            bool ok = false;
            const QString& scheme = url.scheme();
            if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
                ok = QDesktopServices::openUrl(url);
            }
            if (!ok) {
                qCDebug(DOCUMENTATION) << "ignoring unsupported url" << url;
            }
        }
    }

    void setUrlWithContent(const QUrl& url, const QByteArray& content)
    {
        if (StandardDocumentationView::isUrlSchemeSupported(url)) {
            m_requested = url;
            m_content = qCompress(content, 8);
            if (m_restoreTimer) {
                killTimer(m_restoreTimer);
                m_restoreTimer = 0;
            }
        }
    }

    void reload() override
    {
        if (m_restoreTimer) {
            killTimer(m_restoreTimer);
            m_restoreTimer = 0;
            qCDebug(DOCUMENTATION) << "queued restore of url" << m_requested;
            setSource(m_requested);
        }
        QTextBrowser::reload();
    }

    void queueRestore(int delay)
    {
        if (m_restoreTimer) {
            // kill pending restore timer
            killTimer(m_restoreTimer);
        }
        m_restoreTimer = startTimer(delay);
    }

    // adapted from Qt's assistant
    QVariant loadResource(int type, const QUrl &name) override
    {
        // check if we have a callback and we're not loading a requested html url
        if (!(type == QTextDocument::HtmlResource && name == m_requested)) {
            // the callback is invoked with a QVariant that's explicitly invalid
            QVariant newContent(QVariant::Invalid);
            auto resolvedUrl = name;
            if (m_parent->loadResource(type, resolvedUrl, newContent)) {
                return newContent;
            }
        }
        if (type == QTextDocument::HtmlResource) {
            if (name == m_requested) {
                qCDebug(DOCUMENTATION) << "loadResource type" << type << "url" << name << "cached=" << m_requested;
            } else {
                // the current load is now finished, a new one
                // may be triggered by the slot connected to the
                // linkClicked() signal.
                // TODO: should we handle "file:///" URLs directly here?
                m_loadFinished = true;
                emit m_parent->linkClicked(name);
            }
        } else if (type != QTextDocument::StyleSheetResource) {
            m_loadFinished = true;
            qCDebug(DOCUMENTATION) << "HelpViewer::loadResource called with unsupported type" << type << "name=" << name;
        }
        // always just return the cached content
        return m_content.isEmpty() ? m_content : qUncompress(m_content);
    }

    void timerEvent(QTimerEvent *e) override
    {
        if (e->timerId() == m_restoreTimer) {
            reload();
        }
    }

    StandardDocumentationView* m_parent;
    QUrl m_requested;
    QByteArray m_content;
    bool m_loadFinished;
    int m_restoreTimer;

Q_SIGNALS:
    void loadFinished(const QUrl& url);

public Q_SLOTS:
    void setLoadFinished(bool)
    {
        m_loadFinished = true;
        emit loadFinished(source());
        if (m_restoreTimer) {
            reload();
        }
    }
};

void StandardDocumentationViewPrivate::init(StandardDocumentationView* parent)
{
    m_parent = parent;
    m_view = new HelpViewer(parent);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    parent->connect(m_view, &HelpViewer::loadFinished, parent, &StandardDocumentationView::linkClicked);
    parent->layout()->addWidget(m_view);
}

void StandardDocumentationViewPrivate::setup()
{
}

void StandardDocumentationView::search ( const QString& text, DocumentationFindWidget::FindOptions options )
{
    typedef QTextDocument WebkitThing;
    WebkitThing::FindFlags ff = 0;
    if(options & DocumentationFindWidget::Previous)
        ff |= WebkitThing::FindBackward;

    if(options & DocumentationFindWidget::MatchCase)
        ff |= WebkitThing::FindCaseSensitively;

    d->m_view->find(text, ff);
}

void StandardDocumentationView::searchIncremental(const QString& text, DocumentationFindWidget::FindOptions options)
{
    typedef QTextDocument WebkitThing;
    WebkitThing::FindFlags findFlags;

    if (options & DocumentationFindWidget::MatchCase)
        findFlags |= WebkitThing::FindCaseSensitively;

    d->m_view->find(text, findFlags);
}

void StandardDocumentationView::finishSearch()
{
    // passing emptry string to reset search, as told in API docs
    d->m_view->find(QString());
}

void KDevelop::StandardDocumentationView::setOverrideCss(const QUrl& url)
{
    Q_UNUSED(url);
    return;
}

void KDevelop::StandardDocumentationView::load(const QUrl& url)
{
    d->m_view->setSource(url);
}

void KDevelop::StandardDocumentationView::load(const QUrl& url, const QByteArray& content)
{
    d->m_view->setUrlWithContent(url, content);
    d->m_view->setSource(url);
}

void KDevelop::StandardDocumentationView::restore()
{
    // force a restore of the cached url/content
    // this has to be queued as we cannot be certain if
    // calling QTextBrowser::setSource() will have any
    // effect at all.
    d->m_view->queueRestore(250);
}

void KDevelop::StandardDocumentationView::setHtml(const QString& html)
{
    d->m_view->setHtml(html);
}

void KDevelop::StandardDocumentationView::setNetworkAccessManager(QNetworkAccessManager* manager)
{
    Q_UNUSED(manager);
    return;
}

void KDevelop::StandardDocumentationView::setDelegateLinks(bool delegate)
{
    Q_UNUSED(delegate);
    return;
}

QMenu* StandardDocumentationView::createStandardContextMenu(const QPoint& pos)
{
    auto menu = d->m_view->createStandardContextMenu(pos);
    QAction *reloadAction = new QAction(i18n("Reload"), menu);
    reloadAction->connect(reloadAction, &QAction::triggered, d->m_view, &HelpViewer::reload);
    menu->addAction(reloadAction);
    return menu;
}

bool StandardDocumentationView::eventFilter(QObject* object, QEvent* event)
{
    return QWidget::eventFilter(object, event);
}

void StandardDocumentationView::updateZoomFactor(double zoomFactor)
{
    double fontSize = d->m_view->font().pointSizeF();
    if (fontSize <= 0) {
        return;
    }
    double newSize = fontSize * zoomFactor;
    if (newSize > fontSize) {
        d->m_view->zoomIn(int(newSize - fontSize + 0.5));
    } else if (newSize != fontSize) {
        d->m_view->zoomOut(int(fontSize - newSize + 0.5));
    }
}

QWidget* StandardDocumentationView::view() const
{
    return d->m_view;
}

#include "standarddocumentationview_qtb.moc"
