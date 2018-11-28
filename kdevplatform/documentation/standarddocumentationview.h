/*
 * This file is part of KDevelop
 * Copyright 2010 Aleix Pol Gonzalez <aleixpol@kde.org>
 * Copyright 2016 Igor Kushnir <igorkuo@gmail.com>
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

#ifndef KDEVPLATFORM_STANDARDDOCUMENTATIONVIEW_H
#define KDEVPLATFORM_STANDARDDOCUMENTATIONVIEW_H

#include <QWidget>
#include "documentationexport.h"
#include "documentationfindwidget.h"
#include <interfaces/idocumentation.h>

class QNetworkAccessManager;
class QMenu;

namespace KDevelop
{

/**
 * A standard documentation view, based on QtWebKit or QtWebEngine, depending on your distribution preferences.
 */
class KDEVPLATFORMDOCUMENTATION_EXPORT StandardDocumentationView : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY(StandardDocumentationView)
public:
    explicit StandardDocumentationView(DocumentationFindWidget* findWidget, QWidget* parent = nullptr );
    ~StandardDocumentationView() override;

    /**
     * @brief Enables zoom functionality
     *
     * @param configSubGroup KConfigGroup nested group name used to store zoom factor.
     *        Should uniquely describe current documentation provider.
     *
     * @warning Call this function at most once
     */
    void initZoom(const QString& configSubGroup);

    void setDocumentation(const IDocumentation::Ptr& doc);

    void setOverrideCss(const QUrl &url);

    void load(const QUrl &url);
    /**
     * @brief delegate method for the QTextBrowser::loadResource(type,url)
     * overload of the QTextBrowser backend. Override this method if your
     * plugin can handle URLs that QTextBrowser cannot handle itself.
     *
     * @param type the QTextDocument::ResourceType type of the address to load
     * @param url the address to be loaded; can be rewritten (e.g. with a resolved URL)
     * @param content return variable for the loaded content. @p content is
     * guaranteed to be invalid upon entry.
     * 
     * The function should return true if content was loaded successfully.
     */
    virtual bool loadResource(int type, QUrl& url, QVariant& content);

#ifdef USE_QTEXTBROWSER
    /**
     * @brief load a page with the given content
     * 
     * @param url the address with a scheme QTextBrowser doesn't support
     * @param content content that QTextBrowser cannot obtain itself.
     * 
     * Url and content are cached internally.
     */
    void load(const QUrl &url, const QByteArray& content);
    /**
     * @brief restore the cached url and content information
     */
    void restore();
#endif
    void setHtml(const QString &html);
    void setNetworkAccessManager(QNetworkAccessManager* manager);

    /**
     *
     */
    void setDelegateLinks(bool delegate);

    virtual QMenu* createStandardContextMenu(const QPoint& pos = QPoint());

    /**
     * is @param url one using a supported scheme?
     */
    static bool isUrlSchemeSupported(const QUrl& url);

    /**
     * @brief returns the underlying view widget
     */
    QWidget* view() const;

Q_SIGNALS:
    void linkClicked(const QUrl &link);

public Q_SLOTS:
    /**
     * Search for @p text in the documentation view.
     */
    void search(const QString& text, KDevelop::DocumentationFindWidget::FindOptions options);
    void searchIncremental(const QString& text, KDevelop::DocumentationFindWidget::FindOptions options);
    void finishSearch();

    /**
     * Updates the contents, in case it was initialized with a documentation instance,
     * doesn't change anything otherwise
     *
     * @sa setDocumentation(IDocumentation::Ptr)
     */
    void update();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private Q_SLOTS:
    void updateZoomFactor(double zoomFactor);

private:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    const QScopedPointer<class StandardDocumentationViewPrivate> d;
};

}
#endif // KDEVPLATFORM_STANDARDDOCUMENTATIONVIEW_H
