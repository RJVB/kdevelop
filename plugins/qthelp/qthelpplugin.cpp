/*  This file is part of KDevelop
    Copyright 2009 Aleix Pol <aleixpol@kde.org>
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

#include "qthelpplugin.h"

#include <interfaces/icore.h>
#include <interfaces/idocumentationcontroller.h>
#include "qthelpprovider.h"
#include "qthelpqtdoc.h"
#include "qthelpexternalassistant.h"
#include "qthelp_config_shared.h"
#include "debug.h"
#include "qthelpconfig.h"

#include <KPluginFactory>

#include <QDirIterator>

QtHelpPlugin *QtHelpPlugin::s_plugin = nullptr;

K_PLUGIN_FACTORY_WITH_JSON(QtHelpPluginFactory, "kdevqthelp.json", registerPlugin<QtHelpPlugin>(); )

QtHelpPlugin::QtHelpPlugin(QObject* parent, const QVariantList& args)
    : KDevelop::IPlugin(QStringLiteral("kdevqthelp"), parent)
    , m_qtHelpProviders()
    , m_qtDoc(new QtHelpQtDoc(this, QVariantList()))
    , m_loadSystemQtDoc(false)
    , m_useExternalViewer(false)
{
    Q_UNUSED(args);
    s_plugin = this;
    connect(this, &QtHelpPlugin::changedProvidersList, KDevelop::ICore::self()->documentationController(), &KDevelop::IDocumentationController::changedDocumentationProviders);
    QMetaObject::invokeMethod(this, "readConfig", Qt::QueuedConnection);
}

QtHelpPlugin::~QtHelpPlugin()
{
}


void QtHelpPlugin::readConfig()
{
    QStringList iconList, nameList, pathList, ghnsList;
    QString searchDir;
    ExternalViewerSettings extViewer;
    qtHelpReadConfig(iconList, nameList, pathList, ghnsList, searchDir, m_loadSystemQtDoc, extViewer);

    searchHelpDirectory(pathList, nameList, iconList, searchDir);
    loadQtHelpProvider(pathList, nameList, iconList);
    loadQtDocumentation(m_loadSystemQtDoc);
    m_useExternalViewer = extViewer.useExtViewer;
    m_externalViewerExecutable = extViewer.extViewerExecutable;
    if (m_useExternalViewer) {
        KDevelop::QtHelpExternalAssistant::self()->setUseExternalViewer(m_useExternalViewer);
        KDevelop::QtHelpExternalAssistant::self()->setExternalViewerExecutable(m_externalViewerExecutable);
    }

    emit changedProvidersList();
}

void QtHelpPlugin::loadQtDocumentation(bool loadQtDoc)
{
    if (!qEnvironmentVariableIsSet("KDEV_NO_QT_DOCUMENTATION")) {
        if(!loadQtDoc ){
            m_qtDoc->unloadDocumentation();
        } else if(loadQtDoc) {
            m_qtDoc->loadDocumentation();
        }
    }
}

void QtHelpPlugin::searchHelpDirectory(QStringList& pathList, QStringList& nameList, QStringList& iconList, const QString& searchDir)
{
    if (searchDir.isEmpty()) {
        return;
    }

    qCDebug(QTHELP) << "Searching qch files in: " << searchDir;
    QDirIterator dirIt(searchDir, QStringList() << QStringLiteral("*.qch"), QDir::Files, QDirIterator::Subdirectories);
    const QString logo(QStringLiteral("qtlogo"));
    while(dirIt.hasNext() == true)
    {
        dirIt.next();
        qCDebug(QTHELP) << "qch found: " << dirIt.filePath();
        pathList.append(dirIt.filePath());
        nameList.append(dirIt.fileInfo().baseName());
        iconList.append(logo);
    }
}


void QtHelpPlugin::loadQtHelpProvider(const QStringList& pathList, const QStringList& nameList, const QStringList& iconList)
{
    QList<QtHelpProvider*> oldList(m_qtHelpProviders);
    m_qtHelpProviders.clear();
    if (qEnvironmentVariableIsSet("KDEV_NO_QT_DOCUMENTATION")) {
        return;
    }
    for(int i=0; i < pathList.length(); i++) {
        // check if provider already exist
        QString fileName = pathList.at(i);
        QString name = nameList.at(i);
        QString iconName = iconList.at(i);
        QString nameSpace = QHelpEngineCore::namespaceName(fileName);
        if(!nameSpace.isEmpty()){
            QtHelpProvider *provider = nullptr;
            foreach(QtHelpProvider* oldProvider, oldList){
                if(QHelpEngineCore::namespaceName(oldProvider->fileName()) == nameSpace){
                    provider = oldProvider;
                    oldList.removeAll(provider);
                    break;
                }
            }
            if(!provider){
                provider = new QtHelpProvider(this, fileName, name, iconName, QVariantList());
            }else{
                provider->setName(name);
                provider->setIconName(iconName);
            }
            const QString error = provider->engine()->error();
            if (!error.isEmpty() && !error.endsWith(QStringLiteral("already exists."))) {
                qCCritical(QTHELP) << "QtHelp provider error for" << fileName << error;
                goto bail;
            }

            bool exist = false;
            foreach(QtHelpProvider* existingProvider, m_qtHelpProviders){
                if(QHelpEngineCore::namespaceName(existingProvider->fileName()) ==  nameSpace){
                    exist = true;
                    break;
                }
            }

            if(!exist){
                m_qtHelpProviders.append(provider);
            }
        }
    }
bail:;
    // delete unused providers
    qDeleteAll(oldList);
}

QList<KDevelop::IDocumentationProvider*> QtHelpPlugin::providers()
{
    QList<KDevelop::IDocumentationProvider*> list;
    if (!qEnvironmentVariableIsSet("KDEV_NO_QT_DOCUMENTATION")) {
        list.reserve(m_qtHelpProviders.size() + (m_loadSystemQtDoc?1:0));
        foreach(QtHelpProvider* provider, m_qtHelpProviders) {
            list.append(provider);
        }
        if(m_loadSystemQtDoc){
            list.append(m_qtDoc);
        }
    }
    return list;
}

QList<QtHelpProvider*> QtHelpPlugin::qtHelpProviderLoaded()
{
    return m_qtHelpProviders;
}

bool QtHelpPlugin::isQtHelpQtDocLoaded() const
{
    return m_loadSystemQtDoc;
}

bool QtHelpPlugin::useExternalViewer() const
{
    return m_useExternalViewer;
}

bool QtHelpPlugin::isQtHelpAvailable() const
{
    return !m_qtDoc->qchFiles().isEmpty();
}

KDevelop::ConfigPage* QtHelpPlugin::configPage(int number, QWidget* parent)
{
    if (number == 0) {
        return new QtHelpConfig(this, parent);
    }
    return nullptr;
}

int QtHelpPlugin::configPages() const
{
    return 1;
}

#include "qthelpplugin.moc"
