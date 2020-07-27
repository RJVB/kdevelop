/***************************************************************************
 *   Copyright 2007 Alexander Dymo <adymo@kdevelop.org>             *
 *   Copyright 2007 Kris Wong <kris.p.wong@gmail.com>               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/
#include "core.h"
#include "core_p.h"

#include <QApplication>
#include <QSocketNotifier>
#include <signal.h>

#include <KLocalizedString>

#include <language/backgroundparser/backgroundparser.h>
#include <language/duchain/duchain.h>

#include "mainwindow.h"
#include "sessioncontroller.h"
#include "uicontroller.h"
#include "plugincontroller.h"
#include "projectcontroller.h"
#include "partcontroller.h"
#include "languagecontroller.h"
#include "documentcontroller.h"
#include "runcontroller.h"
#include "session.h"
#include "documentationcontroller.h"
#include "sourceformattercontroller.h"
#include "progresswidget/progressmanager.h"
#include "selectioncontroller.h"
#include "debugcontroller.h"
#include "kdevplatform_version.h"
#include "workingsetcontroller.h"
#include "testcontroller.h"
#include "runtimecontroller.h"
#include "debug.h"

#include <csignal>

namespace {
static KDevelop::CorePrivate *corePrivateInstance = nullptr;
static int signalPipeRead = -1;
static int signalPipeWrite = -1;
static QSocketNotifier* signalNotifier = nullptr;

void shutdownGracefully()
{
    // we can use std::atomic<bool> here even if it is not lock_free
    // because we are not running as a signal handler in the strict
    // sense of the term.
    static std::atomic<bool> handlingSignal(false);

    // Get the signal number that was written to the pipe in the actual
    // signal handler. A bit awkward but we have to flush the read side
    // of the pipe anyway.
    // Failure to flush can lead to repetitive signals from QSocketNotifier
    // as if it reacts to the presence of unread data rather than atomic
    // writes to the pipe (observed on Linux 4.14 with Qt 5.9.6).
    int sig;
    read(signalPipeRead, &sig, sizeof(sig));

    // exit as quickly as possible when a SIGHUP is received.
    if (!handlingSignal.exchange(true)
#ifdef SIGHUP
       && sig != SIGHUP
#endif
    ) {
        // the first time we see a signal we attempt to exit
        // as if the user initiated the procedure via the GUI.
        qCWarning(SHELL) << "Going down on signal" << sig;
        QCoreApplication* app = QCoreApplication::instance();
        if (auto* guiApp = qobject_cast<QApplication*>(app)) {
            guiApp->closeAllWindows();
        }
        app->quit();
        return;
    }
    qCWarning(SHELL) << "Going down harder on signal" << sig;

    // we come here if a 2nd signal was received before the full exit
    // procedure completed, for instance because it blocked.
    std::signal(sig, SIG_DFL);

    if (corePrivateInstance->m_core) {
       // shutdown core functionality, in particular the DUChain subsystem
       // in an effort to prevent cache corruption. It's only cache, but
       // regenerating it can be very time-consuming.
       corePrivateInstance->m_core->shutdown();
    }

    signalNotifier->setEnabled(false);

    // re-raise signal with default handler and trigger program termination
    std::raise(sig);
}

void signalHandler(int sig)
{
    if (signalPipeWrite == -1 || write(signalPipeWrite, &sig, sizeof(sig)) == -1) {
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
}

void installSignalHandler()
{
   std::signal(SIGHUP, signalHandler);
   std::signal(SIGINT, signalHandler);
   std::signal(SIGTERM, signalHandler);
   signalNotifier->setEnabled(true);
}
}

namespace KDevelop {

Core *Core::m_self = nullptr;

CorePrivate::CorePrivate(Core *core)
    : QObject(nullptr)
    , m_core(core)
    , m_cleanedUp(false)
    , m_shuttingDown(false)
{
}

bool CorePrivate::initialize(Core::Setup mode, const QString& session )
{
    m_mode=mode;

    qCDebug(SHELL) << "Creating controllers";
#if defined(Q_OS_UNIX)
    auto tmpLocation = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString tmpName(QStringLiteral("/kdevelop-tmp-%1-").arg(getuid()));
    const auto pos = tmpLocation.lastIndexOf(tmpName);
    if (pos >= 0) {
        // we must have been started by another KDevelop session,
        // restore the default TempLocation
        tmpLocation = tmpLocation.left(pos);
    }
    tmpLocation += tmpName + session;
    m_tmpDir = new QDir(tmpLocation);
    if (m_tmpDir->exists()) {
        qCDebug(SHELL) << "Removing existing session temp dir" << tmpLocation;
        m_tmpDir->removeRecursively();
    }
    m_tmpDir->mkpath(tmpLocation);
    if (m_tmpDir->exists()) {
        // make the directory exclusive to us. 
        QFile::setPermissions(tmpLocation, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("TMPDIR", tmpLocation.toLatin1());
    }
#endif

    if( !sessionController )
    {
        sessionController = new SessionController(m_core);
    }
    if( !workingSetController && !(mode & Core::NoUi) )
    {
        workingSetController = new WorkingSetController();
    }
    qCDebug(SHELL) << "Creating ui controller";
    if( !uiController )
    {
        uiController = new UiController(m_core);
    }
    qCDebug(SHELL) << "Creating plugin controller";

    if( !pluginController )
    {
        pluginController = new PluginController(m_core);
        const auto pluginInfos = pluginController->allPluginInfos();
        if (pluginInfos.isEmpty()) {
            QMessageBox::critical(nullptr,
                                  i18n("Could not find any plugins"),
                                  i18n("<p>Could not find any plugins during startup.<br/>"
                                  "Please make sure QT_PLUGIN_PATH is set correctly.</p>"
                                  "Refer to <a href=\"https://community.kde.org/Guidelines_and_HOWTOs/Build_from_source#Set_up_the_runtime_environment\">this article</a> for more information."),
                                  QMessageBox::Abort, QMessageBox::Abort);
            qCWarning(SHELL) << "Could not find any plugins, aborting";
            return false;
        }
    }
    if( !partController && !(mode & Core::NoUi))
    {
        partController = new PartController(m_core, uiController->defaultMainWindow());
    }

    if( !projectController )
    {
        projectController = new ProjectController(m_core);
    }

    if( !documentController )
    {
        documentController = new DocumentController(m_core);
    }

    if( !languageController )
    {
        // Must be initialized after documentController, because the background parser depends
        // on the document controller.
        languageController = new LanguageController(m_core);
    }

    if( !runController )
    {
        runController = new RunController(m_core);
    }

    if( !sourceFormatterController )
    {
        sourceFormatterController = new SourceFormatterController(m_core);
    }

    if ( !progressController)
    {
        progressController = ProgressManager::instance();
    }

    if( !selectionController )
    {
        selectionController = new SelectionController(m_core);
    }

    if( !documentationController && !(mode & Core::NoUi) )
    {
        documentationController = new DocumentationController(m_core);
    }

    if( !runtimeController )
    {
        runtimeController = new RuntimeController(m_core);
    }

    if( !debugController )
    {
        debugController = new DebugController(m_core);
    }

    if( !testController )
    {
        testController = new TestController(m_core);
    }

    qCDebug(SHELL) << "Done creating controllers";

    qCDebug(SHELL) << "Initializing controllers";

    sessionController->initialize( session );
    if( !sessionController->activeSessionLock() ) {
        return false;
    }

    // TODO: Is this early enough, or should we put the loading of the session into
    // the controller construct
    DUChain::initialize();

    if (!(mode & Core::NoUi)) {
        uiController->initialize();
    }
    languageController->initialize();
    languageController->backgroundParser()->suspend();
    // eventually resume the background parser once the project controller
    // has been initialized. At that point we know whether there are projects loading
    // which the background parser is handling internally to defer parse jobs
    QObject::connect(projectController.data(), &ProjectController::initialized,
                     m_core, [this]() {
                         languageController->backgroundParser()->resume();
                     });

    if (partController) {
        partController->initialize();
    }
    projectController->initialize();
    documentController->initialize();

    /* This is somewhat messy.  We want to load the areas before
        loading the plugins, so that when each plugin is loaded we
        know if an area wants some of the tool view from that plugin.
        OTOH, loading of areas creates documents, and some documents
        might require that a plugin is already loaded.
        Probably, the best approach would be to plugins to just add
        tool views to a list of available tool view, and then grab
        those tool views when loading an area.  */

    qCDebug(SHELL) << "Initializing plugin controller (loading session plugins)";
    pluginController->initialize();

    qCDebug(SHELL) << "Initializing working set controller";
    if(!(mode & Core::NoUi))
    {
        workingSetController->initialize();
        /* Need to do this after everything else is loaded.  It's too
            hard to restore position of views, and toolbars, and whatever
            that are not created yet.  */
        uiController->loadAllAreas(KSharedConfig::openConfig());
        uiController->defaultMainWindow()->show();
    }

    qCDebug(SHELL) << "Initializing remaining controllers";
    runController->initialize();
    sourceFormatterController->initialize();
    selectionController->initialize();
    if (documentationController) {
        documentationController->initialize();
    }
    debugController->initialize();
    testController->initialize();
    runtimeController->initialize();

//     A "proper" exit-on-signal approach:
//     Open a pipe or an eventfd, then install your signal handler. In that signal
//     handler, write anything to the writing end or write uint64_t(1) the eventfd.
//     Create a QSocketNotifier on the reading end of the pipe or on the eventfd,
//     connect its activation signal to a slot that does what you want.

#ifdef Q_OS_UNIX
    if (!corePrivateInstance) {
        int pp[2];
        if (pipe(pp)) {
            qCWarning(SHELL) << "Error opening signal handler pipe" << strerror(errno);
        } else {
            signalPipeRead = pp[0];
            signalPipeWrite = pp[1];
            signalNotifier = new QSocketNotifier(signalPipeRead, QSocketNotifier::Read, this);
            connect(signalNotifier, &QSocketNotifier::activated, this, &CorePrivate::shutdownGracefully, Qt::DirectConnection);
            installSignalHandler();
        }
        corePrivateInstance = this;
    }
#endif

    qCDebug(SHELL) << "Done initializing controllers";

    return true;
}
CorePrivate::~CorePrivate()
{
    delete selectionController.data();
    delete projectController.data();
    delete languageController.data();
    delete pluginController.data();
    delete uiController.data();
    delete partController.data();
    delete documentController.data();
    delete runController.data();
    delete sessionController.data();
    delete sourceFormatterController.data();
    delete documentationController.data();
    delete debugController.data();
    delete workingSetController.data();
    delete testController.data();
    delete runtimeController.data();

    if (m_tmpDir->exists()) {
        m_tmpDir->removeRecursively();
        delete m_tmpDir;
    }

    selectionController.clear();
    projectController.clear();
    languageController.clear();
    pluginController.clear();
    uiController.clear();
    partController.clear();
    documentController.clear();
    runController.clear();
    sessionController.clear();
    sourceFormatterController.clear();
    documentationController.clear();
    debugController.clear();
    workingSetController.clear();
    testController.clear();
    runtimeController.clear();

    if (signalPipeWrite != -1) {
        close(signalPipeWrite);
        signalPipeWrite = -1;
    }
    if (signalPipeRead != -1) {
        close(signalPipeRead);
    }
}

void CorePrivate::shutdownGracefully()
{
   ::shutdownGracefully();
}


bool Core::initialize(Setup mode, const QString& session)
{
    if (m_self)
        return true;

    m_self = new Core();
    bool ret = m_self->d->initialize(mode, session);

    if(ret)
        emit m_self->initializationCompleted();

    return ret;
}

Core *KDevelop::Core::self()
{
    return m_self;
}

#include <sys/resource.h>
#ifdef Q_OS_LINUX
#include <linux/fs.h>
#define OPEN_MAX INR_OPEN_MAX
#elif defined(Q_OS_MACOS)
#include <sys/syslimits.h>
#endif

Core::Core(QObject *parent)
    : ICore(parent)
{
#ifdef OPEN_MAX
    // set the maximum number of files
    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur = qMin(rlim_t(OPEN_MAX), rlim.rlim_max);
    setrlimit(RLIMIT_NOFILE, &rlim);
#endif

    d = new CorePrivate(this);

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &Core::shutdown);
}

Core::Core(CorePrivate* dd, QObject* parent)
: ICore(parent), d(dd)
{
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &Core::shutdown);
}

Core::~Core()
{
    qCDebug(SHELL);

    //Cleanup already called before mass destruction of GUI
    delete d;
    m_self = nullptr;
}

Core::Setup Core::setupFlags() const
{
    return d->m_mode;
}

void Core::shutdown()
{
    qCDebug(SHELL);

    if (!d->m_shuttingDown) {
        cleanup();
        deleteLater();
    }

    qCDebug(SHELL) << "Shutdown done";
}

bool Core::shuttingDown() const
{
    return d->m_shuttingDown;
}

void handleALRM(int sig)
{
    fprintf(stderr, "Final shutdown taking longer than a minute: bailing\n");
    std::raise(SIGHUP);
}

void Core::cleanup()
{
    qCDebug(SHELL);

    d->m_shuttingDown = true;
    emit aboutToShutdown();

    if (!d->m_cleanedUp) {
        // first of all: request stop of all background parser jobs
        d->languageController->backgroundParser()->abortAllJobs();
        d->languageController->backgroundParser()->suspend();

        d->debugController->cleanup();
        d->selectionController->cleanup();
        // Save the layout of the ui here, so run it first
        d->uiController->cleanup();

        if (d->workingSetController)
            d->workingSetController->cleanup();

        /* Must be called before projectController->cleanup(). */
        // Closes all documents (discards, as already saved if the user wished earlier)
        d->documentController->cleanup();
        d->runController->cleanup();
        if (d->partController) {
            d->partController->cleanup();
        }
        d->projectController->cleanup();
        d->sourceFormatterController->cleanup();

        // before unloading language plugins, we need to make sure all parse jobs are done
        d->languageController->backgroundParser()->waitForIdle();

        DUChain::self()->shutdown();
        qCInfo(SHELL) << "DUChain shut down";

        // Only unload plugins after the DUChain shutdown to prevent issues with non-loaded factories for types
        // See: https://bugs.kde.org/show_bug.cgi?id=379669
        d->pluginController->cleanup();

        d->sessionController->cleanup();
        qCInfo(SHELL) << "sessionController cleaned up";

        d->testController->cleanup();

        //Disable the functionality of the language controller
        d->languageController->cleanup();
        qCInfo(SHELL) << "languageController cleaned up";

        // let's give us 1 minute to exit completely
        static struct itimerval rtt;
        rtt.it_value.tv_sec= (unsigned long) 60, rtt.it_value.tv_usec= (unsigned long) 0;
        rtt.it_interval.tv_sec= 0, rtt.it_interval.tv_usec= 0;
        signal(SIGALRM, handleALRM);
        setitimer( ITIMER_REAL, &rtt, nullptr );
    }

    d->m_cleanedUp = true;
    emit shutdownCompleted();
}

IUiController *Core::uiController()
{
    return d->uiController.data();
}

ISession* Core::activeSession()
{
    return sessionController()->activeSession();
}

ISessionLock::Ptr Core::activeSessionLock()
{
    return sessionController()->activeSessionLock();
}

SessionController *Core::sessionController()
{
    return d->sessionController.data();
}

UiController *Core::uiControllerInternal()
{
    return d->uiController.data();
}

IPluginController *Core::pluginController()
{
    return d->pluginController.data();
}

PluginController *Core::pluginControllerInternal()
{
    return d->pluginController.data();
}

IProjectController *Core::projectController()
{
    return d->projectController.data();
}

ProjectController *Core::projectControllerInternal()
{
    return d->projectController.data();
}

IPartController *Core::partController()
{
    return d->partController.data();
}

PartController *Core::partControllerInternal()
{
    return d->partController.data();
}

ILanguageController *Core::languageController()
{
    return d->languageController.data();
}

LanguageController *Core::languageControllerInternal()
{
    return d->languageController.data();
}

IDocumentController *Core::documentController()
{
    return d->documentController.data();
}

DocumentController *Core::documentControllerInternal()
{
    return d->documentController.data();
}

IRunController *Core::runController()
{
    return d->runController.data();
}

RunController *Core::runControllerInternal()
{
    return d->runController.data();
}

ISourceFormatterController* Core::sourceFormatterController()
{
    return d->sourceFormatterController.data();
}

SourceFormatterController* Core::sourceFormatterControllerInternal()
{
    return d->sourceFormatterController.data();
}


ProgressManager *Core::progressController()
{
    return d->progressController.data();
}

ISelectionController* Core::selectionController()
{
    return d->selectionController.data();
}

IDocumentationController* Core::documentationController()
{
    return d->documentationController.data();
}

DocumentationController* Core::documentationControllerInternal()
{
    return d->documentationController.data();
}

IRuntimeController* Core::runtimeController()
{
    return d->runtimeController.data();
}

RuntimeController* Core::runtimeControllerInternal()
{
    return d->runtimeController.data();
}

IDebugController* Core::debugController()
{
    return d->debugController.data();
}

DebugController* Core::debugControllerInternal()
{
    return d->debugController.data();
}

ITestController* Core::testController()
{
    return d->testController.data();
}

TestController* Core::testControllerInternal()
{
    return d->testController.data();
}

WorkingSetController* Core::workingSetControllerInternal()
{
    return d->workingSetController.data();
}

QString Core::version()
{
    return QStringLiteral(KDEVPLATFORM_VERSION_STRING);
}

}

