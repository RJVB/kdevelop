/*  This file is part of KDevelop
    Copyright 2017 René J.V. Bertin <rjvbertin@gmail.com>

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

#include "qthelpexternalassistant.h"

#include <QString>
#include <QUrl>
#include <QStandardPaths>
#include <QFileInfo>

#include <AppKit/AppKit.h>

#include "debug.h"

using namespace KDevelop;

bool QtHelpExternalAssistant::hasExternalViewer(QString* path)
{
    bool ret = false;
    QString execPath;
    QFileInfo info;

    if (this == s_self ){
        if (!m_externalViewerExecutable.isEmpty()) {
            execPath = m_externalViewerExecutable;
            info = QFileInfo(execPath);
            if (info.isBundle()) {
                NSString* bundleExec = [[NSBundle bundleWithPath:m_externalViewerExecutable.toNSString()] executablePath];
                if (bundleExec) {
                    execPath = QString::fromNSString(bundleExec);
                    info = QFileInfo(execPath);
                    qCDebug(QTHELP) << "Found bundle exec" << execPath << "for external viewer" << m_externalViewerExecutable;
                } else {
                    qCWarning(QTHELP) << "Could not find the bundle exec for external viewer" << m_externalViewerExecutable;
                }
            }
            if (info.isExecutable()) {
                ret = true;
            } else {
                qCWarning(QTHELP) << "External viewer" << execPath << "is not executable!";
            }
            // always return the configured executable path
            if (path) {
                *path = execPath;
            }
        } else if (path) {
            // look for the application on the current path. The result is not cached so the
            // lookup is performed only when we can return the result.
            // First try to find a regular executable, which the user could have created
            // to invoke the right Assistant.app
            const QString assistant = QStandardPaths::findExecutable(QLatin1String("assistant"));
            if (!assistant.isEmpty()) {
                ret = true;
                *path = assistant;
                qCDebug(QTHELP) << "External viewer found at" << assistant;
            } else {
                // try to locate a Qt5 Assistant app bundle
                // There is no way to predict which we'll get beyond "probably the same as last time".
                NSString *assistantAppBundle = [[NSWorkspace sharedWorkspace]
                    absolutePathForAppBundleWithIdentifier:@"org.qt-project.assistant"];
                if (!assistantAppBundle) {
                    // fall back to finding whatever Assistant.app application might be available.
                    assistantAppBundle = [[NSWorkspace sharedWorkspace] fullPathForApplication:@"Assistant.app"];
                }
                NSString* assistantExec = assistantAppBundle ? [[NSBundle bundleWithPath:assistantAppBundle] executablePath] : nullptr;
                if (assistantExec) {
                    execPath = QString::fromNSString(assistantExec);
                    ret = true;
                    *path = execPath;
                    qCDebug(QTHELP) << "Found bundle exec" << execPath << "for Assistant.app" << QString::fromNSString(assistantAppBundle);
                } else {
                    qCWarning(QTHELP) << "External viewer not found!";
                }
            }
        }
    } else {
        return s_self->hasExternalViewer(path);
    }
    return ret;
}
