/* This file is part of KDevelop
   Copyright 2013 Christoph Thielecke <crissi99@gmx.de>
   Copyright 2016-2017 Anton Anikin <anton.anikin@htower.ru>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "plugin.h"

#include "config/globalconfigpage.h"
#include "config/projectconfigpage.h"
#include "globalsettings.h"

#include "debug.h"
#include "job.h"
#include "problemmodel.h"

#include <interfaces/contextmenuextension.h>
#include <interfaces/icore.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iruncontroller.h>
#include <interfaces/iuicontroller.h>
#include <kactioncollection.h>
#include <kpluginfactory.h>
#include <language/interfaces/editorcontext.h>
#include <project/projectconfigpage.h>
#include <project/projectmodel.h>
#include <util/jobstatus.h>

#include <QAction>
#include <QMimeDatabase>

K_PLUGIN_FACTORY_WITH_JSON(CppcheckFactory, "kdevcppcheck.json", registerPlugin<cppcheck::Plugin>();)

namespace cppcheck
{

Plugin::Plugin(QObject* parent, const QVariantList&)
    : IPlugin(QStringLiteral("kdevcppcheck"), parent)
    , m_job(nullptr)
    , m_currentProject(nullptr)
    , m_model(new ProblemModel(this))
{
    qCDebug(KDEV_CPPCHECK) << "setting cppcheck rc file";
    setXMLFile(QStringLiteral("kdevcppcheck.rc"));

    QIcon cppcheckIcon = QIcon::fromTheme(QStringLiteral("cppcheck"));

    m_menuActionFile = new QAction(cppcheckIcon, i18n("Analyze Current File with Cppcheck"), this);
    connect(m_menuActionFile, &QAction::triggered, this, [this](){
        runCppcheck(false);
    });
    actionCollection()->addAction(QStringLiteral("cppcheck_file"), m_menuActionFile);

    m_contextActionFile = new QAction(cppcheckIcon, i18n("Cppcheck"), this);
    connect(m_contextActionFile, &QAction::triggered, this, [this]() {
        runCppcheck(false);
    });

    m_menuActionProject = new QAction(cppcheckIcon, i18n("Analyze Current Project with Cppcheck"), this);
    connect(m_menuActionProject, &QAction::triggered, this, [this](){
        runCppcheck(true);
    });
    actionCollection()->addAction(QStringLiteral("cppcheck_project"), m_menuActionProject);

    m_contextActionProject = new QAction(cppcheckIcon, i18n("Cppcheck"), this);
    connect(m_contextActionProject, &QAction::triggered, this, [this]() {
        runCppcheck(true);
    });

    m_contextActionProjectItem = new QAction(cppcheckIcon, i18n("Cppcheck"), this);

    connect(core()->documentController(), &KDevelop::IDocumentController::documentClosed,
            this, &Plugin::updateActions);
    connect(core()->documentController(), &KDevelop::IDocumentController::documentActivated,
            this, &Plugin::updateActions);

    connect(core()->projectController(), &KDevelop::IProjectController::projectOpened,
            this, &Plugin::updateActions);
    connect(core()->projectController(), &KDevelop::IProjectController::projectClosed,
            this, &Plugin::projectClosed);

    updateActions();
}

Plugin::~Plugin()
{
    killCppcheck();
}

bool Plugin::isRunning()
{
    return m_job;
}

void Plugin::killCppcheck()
{
    if (m_job) {
        m_job->kill(KJob::EmitResult);
    }
}

void Plugin::raiseProblemsView()
{
    m_model->show();
}

void Plugin::raiseOutputView()
{
    core()->uiController()->findToolView(
        i18nc("@title:window", "Test"),
        nullptr,
        KDevelop::IUiController::FindFlags::Raise);
}

void Plugin::updateActions()
{
    m_currentProject = nullptr;

    m_menuActionFile->setEnabled(false);
    m_menuActionProject->setEnabled(false);

    if (isRunning()) {
        return;
    }

    KDevelop::IDocument* activeDocument = core()->documentController()->activeDocument();
    if (!activeDocument) {
        return;
    }

    QUrl url = activeDocument->url();

    m_currentProject = core()->projectController()->findProjectForUrl(url);
    if (!m_currentProject) {
        return;
    }

    m_menuActionFile->setEnabled(true);
    m_menuActionProject->setEnabled(true);
}

void Plugin::projectClosed(KDevelop::IProject* project)
{
    if (project != m_model->project()) {
        return;
    }

    killCppcheck();
    m_model->reset();
}

void Plugin::runCppcheck(bool checkProject)
{
    KDevelop::IDocument* doc = core()->documentController()->activeDocument();
    Q_ASSERT(doc);

    if (checkProject) {
        runCppcheck(m_currentProject, m_currentProject->path().toUrl().toLocalFile());
    } else {
        runCppcheck(m_currentProject, doc->url().toLocalFile());
    }
}

void Plugin::runCppcheck(KDevelop::IProject* project, const QString& path)
{
    m_model->reset(project, path);

    Parameters params(project);
    params.checkPath = path;

    m_job = new Job(params);

    connect(m_job, &Job::problemsDetected, m_model.data(), &ProblemModel::addProblems);
    connect(m_job, &Job::finished, this, &Plugin::result);

    core()->uiController()->registerStatus(new KDevelop::JobStatus(m_job, QStringLiteral("Cppcheck")));
    core()->runController()->registerJob(m_job);

    if (params.hideOutputView) {
        raiseProblemsView();
    } else {
        raiseOutputView();
    }

    updateActions();
}

void Plugin::result(KJob*)
{
    if (!core()->projectController()->projects().contains(m_model->project())) {
        m_model->reset();
    } else {
        m_model->setProblems();

        if (m_job->status() == KDevelop::OutputExecuteJob::JobStatus::JobSucceeded ||
            m_job->status() == KDevelop::OutputExecuteJob::JobStatus::JobCanceled) {
            raiseProblemsView();
        } else {
            raiseOutputView();
        }
    }

    m_job = nullptr; // job is automatically deleted later

    updateActions();
}

static
bool isSupportedMimeType(const QMimeType& mimeType)
{
    const QString mimeName = mimeType.name();
    return (mimeName == QLatin1String("text/x-c++src") ||
            mimeName == QLatin1String("text/x-c++hdr") ||
            mimeName == QLatin1String("text/x-chdr")   ||
            mimeName == QLatin1String("text/x-csrc"));
}

KDevelop::ContextMenuExtension Plugin::contextMenuExtension(KDevelop::Context* context, QWidget* parent)
{
    KDevelop::ContextMenuExtension extension = KDevelop::IPlugin::contextMenuExtension(context, parent);

    if (context->hasType(KDevelop::Context::EditorContext) && m_currentProject && !isRunning()) {
        auto eContext = static_cast<KDevelop::EditorContext*>(context);
        QMimeDatabase db;
        const auto mime = db.mimeTypeForUrl(eContext->url());

        if (isSupportedMimeType(mime)) {
            extension.addAction(KDevelop::ContextMenuExtension::AnalyzeFileGroup, m_contextActionFile);
            extension.addAction(KDevelop::ContextMenuExtension::AnalyzeProjectGroup, m_contextActionProject);
        }
    }

    if (context->hasType(KDevelop::Context::ProjectItemContext) && !isRunning()) {
        auto pContext = static_cast<KDevelop::ProjectItemContext*>(context);
        if (pContext->items().size() != 1) {
            return extension;
        }

        auto item = pContext->items().first();

        switch (item->type()) {
            case KDevelop::ProjectBaseItem::File: {
                const QMimeType mimetype = QMimeDatabase().mimeTypeForUrl(item->path().toUrl());
                if (!isSupportedMimeType(mimetype)) {
                    return extension;
                }
                break;
            }
            case KDevelop::ProjectBaseItem::Folder:
            case KDevelop::ProjectBaseItem::BuildFolder:
                break;

            default:
                return extension;
        }

        m_contextActionProjectItem->disconnect();
        connect(m_contextActionProjectItem, &QAction::triggered, this, [this, item](){
            runCppcheck(item->project(), item->path().toLocalFile());
        });

        extension.addAction(KDevelop::ContextMenuExtension::AnalyzeProjectGroup, m_contextActionProjectItem);
    }

    return extension;
}

KDevelop::ConfigPage* Plugin::perProjectConfigPage(int number, const KDevelop::ProjectConfigOptions& options, QWidget* parent)
{
    return number ? nullptr : new ProjectConfigPage(this, options.project, parent);
}

KDevelop::ConfigPage* Plugin::configPage(int number, QWidget* parent)
{
    return number ? nullptr : new GlobalConfigPage(this, parent);
}

}

#include "plugin.moc"
