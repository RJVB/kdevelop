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

#ifndef QTHELPEXTERNALASSISTANT_H
#define QTHELPEXTERNALASSISTANT_H

#include <QObject>
#include <QProcess>

#include "debug.h"

class QUrl;

namespace KDevelop{

/**
 * @brief Provides support for using Qt's Assistant (or compatible applications)
 * as an external viewer for QtHelp documentation. A single instance will be
 * launched when required (one external browser per KDevelop session), and
 * controlled via the Assistant's remote control protocol. The application will
 * be terminated when KDevelop exits.
 */
class QtHelpExternalAssistant : public QObject
{
    Q_OBJECT
public:
    QtHelpExternalAssistant(QObject* parent);
    ~QtHelpExternalAssistant();

    /**
     * @brief returns the current external viewer instance,
     * creating and initialising it if necessary. It does
     * not start the viewer.
     */
    static QtHelpExternalAssistant* self();

    /**
     * @brief open @param url in the external viewer,
     * launching it if required. No attempt is made to raise
     * or unhide the viewer, beyond what the viewer does itself.
     * @returns true when the command is transmitted successfully.
     */
    static bool openUrl(const QUrl& url);

    /**
     * @brief enable or disable use of the external viewer
     */
    void setUseExternalViewer(bool extViewer);
    bool useExternalViewer() { return m_useExternalViewer; }

    /**
     * @brief sets the @param path to the external viewer executable.
     */
    void setExternalViewerExecutable(QString& path);
    /**
     * @brief checks if a valid external viewer executable has been
     * configured; @returns true if this is the case and links can thus
     * be opened in this viewer instead of in the embedded browser.
     * 
     * @param path can be used to obtain the configured path or the
     * path to the Assistant copy found automatically.
     */
    bool hasExternalViewer(QString* path = nullptr);

private Q_SLOTS:
    void externalViewerExit(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void start();

    static QtHelpExternalAssistant* s_self;
    QProcess* m_externalViewerProcess = nullptr;
    bool m_useExternalViewer = false;
    QString m_externalViewerExecutable;
};

}

#endif //QTHELPEXTERNALASSISTANT_H
