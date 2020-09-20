/*  This file is part of KDevelop
    Copyright 2009 Aleix Pol <aleixpol@kde.org>
    Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>
    Copyright 2010 Benjamin Port <port.benjamin@gmail.com>

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

#ifndef QTHELPDOCUMENTATION_H
#define QTHELPDOCUMENTATION_H

#include <QList>
#include <QMap>
#include <QUrl>
#include <QPointer>
#include <QAction>
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#include <QHelpLink>
#endif

#include <interfaces/idocumentation.h>

namespace KDevelop { class StandardDocumentationView; }
class QModelIndex;
class QNetworkAccessManager;
class QtHelpProviderAbstract;
class QTemporaryFile;

class QtHelpDocumentationView;

class QtHelpDocumentation : public KDevelop::IDocumentation
{
    Q_OBJECT
    public:
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        QtHelpDocumentation(const QString& name, const QList<QHelpLink>& info);
        QtHelpDocumentation(const QString& name, const QList<QHelpLink>& info, const QString& key);
#else
        QtHelpDocumentation(const QString& name, const QMap<QString, QUrl>& info);
        QtHelpDocumentation(const QString& name, const QMap<QString, QUrl>& info, const QString& key);
#endif

        ~QtHelpDocumentation() override;

        QString name() const override { return m_name; }

        QString description() const override;

        QWidget* documentationWidget(KDevelop::DocumentationFindWidget* findWidget, QWidget* parent) override;

        KDevelop::IDocumentationProvider* provider() const override;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        QList<QHelpLink> info() const { return m_info; }
#else
        QMap<QString, QUrl> info() const { return m_info; }
#endif

        static QtHelpProviderAbstract* s_provider;

        bool viewInExternalBrowser() override;

    public Q_SLOTS:
        void viewContextMenuRequested(const QPoint& pos);

    private Q_SLOTS:
        void jumpedTo(const QUrl& newUrl);

    private:
        void setUserStyleSheet(KDevelop::StandardDocumentationView* view, const QUrl& url);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        const QUrl& currentUrl() const { return m_current->url; }
        const QString& currentTitle() const { return m_current->title; }
#else
        const QUrl& currentUrl() const { return m_current.value(); }
        const QString& currentTitle() const { return m_current.key(); }
#endif

    private:
        QtHelpProviderAbstract *m_provider;
        const QString m_name;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        const QList<QHelpLink> m_info;
        const QList<QHelpLink>::const_iterator m_current;
#else
        const QMap<QString, QUrl> m_info;
        const QMap<QString, QUrl>::const_iterator m_current;
#endif

        KDevelop::StandardDocumentationView* lastView;
        QPointer<QTemporaryFile> m_lastStyleSheet;

    friend class QtHelpDocumentationView;
};

class HomeDocumentation : public KDevelop::IDocumentation
{
    Q_OBJECT
    public:
        HomeDocumentation();
        QWidget* documentationWidget(KDevelop::DocumentationFindWidget* findWidget, QWidget* parent = nullptr) override;
        QString description() const override { return QString(); }
        QString name() const override;
        KDevelop::IDocumentationProvider* provider() const override;

    public Q_SLOTS:
        void clicked(const QModelIndex& idx);
    private:
        QtHelpProviderAbstract *m_provider;
        bool eventFilter(QObject *obj, QEvent *event) override;
};

class QtHelpAlternativeLink : public QAction
{
    Q_OBJECT
    public:
        QtHelpAlternativeLink(const QString& name, const QtHelpDocumentation* doc, QObject* parent);

    public Q_SLOTS:
        void showUrl();

    private:
        const QtHelpDocumentation* mDoc;
        const QString mName;
};

#endif
