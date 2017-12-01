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
#include <QProcess>
#include <QApplication>
#include <QFileInfo>

#include <interfaces/icore.h>

#include "debug.h"

using namespace KDevelop;

QtHelpExternalAssistant* QtHelpExternalAssistant::s_self = nullptr;

QtHelpExternalAssistant::QtHelpExternalAssistant(QObject *parent)
    : QObject(parent)
{
}

QtHelpExternalAssistant::~QtHelpExternalAssistant()
{
    if (m_externalViewerProcess) {
        // stop monitoring the process. Prevents potential warnings
        // (QCoreApplication::postEvent: Unexpected null receiver)
        m_externalViewerProcess->disconnect();
#ifdef Q_OS_UNIX
        qint64 pid = m_externalViewerProcess->processId();
        if (pid > 0) {
            QProcess* hup = new QProcess();
            hup->startDetached(QString::fromLatin1("kill -1 %1").arg(pid));
            hup->waitForFinished(50);
            hup->deleteLater();
        } else
#endif
            m_externalViewerProcess->terminate();
        // give the process a bit of time to react, just to prevent
        // "QProcess destroyed while process still running" warnings.
        m_externalViewerProcess->waitForFinished(50);
        m_externalViewerProcess->deleteLater();
        m_externalViewerProcess = nullptr;
    }
    if (this == s_self) {
        s_self = nullptr;
    }
}

void QtHelpExternalAssistant::externalViewerExit(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (this == s_self) {
        if (!m_externalViewerProcess) {
            qCCritical(QTHELP) << "exit notification for inexistant viewer process, this shouldn't happen!";
        } else {
            if (exitStatus == QProcess::ExitStatus::NormalExit) {
                qCDebug(QTHELP) << "externalViewer" << m_externalViewerProcess << "has exited";
            } else {
                qCWarning(QTHELP) << "externalViewer" << m_externalViewerProcess << "has exited with code"
                    << exitCode << "and status" << exitStatus << m_externalViewerProcess->errorString();
            }
            // TODO: shouldn't this be m_externalViewerProcess->deleteLater() ??
            m_externalViewerProcess->deleteLater();
            m_externalViewerProcess = nullptr;
        }
    } else {
        qCCritical(QTHELP) << "externalViewerExit called for the wrong QtHelpExternalAssistant instance (forwarding)";
        s_self->externalViewerExit(exitCode, exitStatus);
    }
}

void QtHelpExternalAssistant::setUseExternalViewer(bool extViewer)
{
    if (this == s_self) {
        m_useExternalViewer = extViewer;
    } else {
        s_self->setUseExternalViewer(extViewer);
    }
}

void QtHelpExternalAssistant::setExternalViewerExecutable(QString& path)
{
    if (this == s_self) {
        m_externalViewerExecutable = path;
    } else {
        s_self->setExternalViewerExecutable(path);
    }
}

#ifndef Q_OS_MACOS
bool QtHelpExternalAssistant::hasExternalViewer(QString* path)
{
    bool ret = false;
    if (this == s_self) {
        if (!m_externalViewerExecutable.isEmpty()) {
            QFileInfo info(m_externalViewerExecutable);
            if (info.isExecutable()) {
                ret = true;
            } else {
                qCWarning(QTHELP) << "External viewer" << m_externalViewerExecutable << "is not executable!";
            }
            // always return the configured executable path
            if (path) {
                *path = m_externalViewerExecutable;
            }
        } else if (path) {
            // look for the application on the current path. The result is not cached so the
            // lookup is performed only when we can return the result.
            const QString assistant = QStandardPaths::findExecutable(QLatin1String("assistant"));
            if (!assistant.isEmpty()) {
                ret = true;
                *path = assistant;
                qCDebug(QTHELP) << "External viewer found at" << assistant;
            } else {
                qCWarning(QTHELP) << "External viewer not found!";
            }
        }
    } else {
        return s_self->hasExternalViewer(path);
    }
    return ret;
}
#endif

QtHelpExternalAssistant* QtHelpExternalAssistant::self()
{
    if (ICore::self()->shuttingDown()) {
        // don't create a new instance if we're shutting down.
        return s_self;
    }

    if (!s_self) {
        s_self = new QtHelpExternalAssistant(qApp);
    }
    return s_self;
}

bool QtHelpExternalAssistant::openUrl(const QUrl& url)
{
    if (ICore::self()->shuttingDown()) {
        return false;
    }
    if (s_self || self()) {
        // start the viewer if required (moved here from self())
        QString path;
        if (!s_self->m_externalViewerProcess && s_self->hasExternalViewer(&path)) {
            s_self->m_externalViewerProcess = new QProcess();
            connect(s_self->m_externalViewerProcess,
                    static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    s_self, &QtHelpExternalAssistant::externalViewerExit);
            QStringList args = {"-enableRemoteControl"};
            s_self->m_externalViewerProcess->start(path, args, QIODevice::WriteOnly|QIODevice::Append);
            if (!s_self->m_externalViewerProcess->waitForStarted()) {
                qCWarning(QTHELP) << "failure starting" << path << ":" << s_self->m_externalViewerProcess->errorString();
                s_self->m_externalViewerProcess->deleteLater();
                s_self->m_externalViewerProcess = nullptr;
            } else {
                s_self->m_externalViewerProcess->setObjectName(path);
            }
        }
        const QByteArray command = QByteArrayLiteral("setSource ")
            + url.toString().toUtf8() + QByteArrayLiteral("\n");
        if (s_self->m_externalViewerProcess->write(command) < 0
            || s_self->m_externalViewerProcess->write("show contents\n") < 0) {
            return false;
        }
        s_self->m_externalViewerProcess->write("syncContents\n");
        return true;
    }
    return false;
}
