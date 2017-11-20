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

#ifndef KDEVPLATFORM_STANDARDDOCUMENTATIONVIEW_P_H
#define KDEVPLATFORM_STANDARDDOCUMENTATIONVIEW_P_H

class QWebView;
class QWebEngineView;

namespace KDevelop
{

class ZoomController;
class IDocumentation;
class StandardDocumentationView;

class HelpViewer;
class StandardDocumentationPage;

class StandardDocumentationViewPrivate
{
public:
    ZoomController* m_zoomController = nullptr;
    IDocumentation::Ptr m_doc;
    StandardDocumentationView* m_parent;

#ifdef USE_QTEXTBROWSER
    HelpViewer* m_view = nullptr;
#elif defined(USE_QTWEBKIT)
    QWebView* m_view = nullptr;
#else
    QWebEngineView* m_view = nullptr;
    StandardDocumentationPage* m_page = nullptr;
#endif

    void init(StandardDocumentationView* parent);
    void setup();
};

}

#endif // KDEVPLATFORM_STANDARDDOCUMENTATIONVIEW_P_H


