/***************************************************************************
 *   Copyright (C) 2008 by Andreas Pakulat <apaku@gmx.de                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "openprojectdialog.h"
#include "openprojectpage.h"
#include "projectinfopage.h"

#include <QPushButton>
#include <QFileInfo>
#include <QFileDialog>
#include <QRegularExpression>

#include <KColorScheme>
#include <KIO/StatJob>
#include <KIO/ListJob>
#include <KJobWidgets>
#include <KLocalizedString>

#include "core.h"
#include "uicontroller.h"
#include "plugincontroller.h"
#include "mainwindow.h"
#include "shellextension.h"
#include "projectsourcepage.h"
#include <debug.h>
#include <interfaces/iprojectcontroller.h>

namespace
{
struct URLInfo
{
    bool isValid;
    bool isDir;
    QString extension;
};

URLInfo urlInfo(const QUrl& url)
{
    URLInfo ret;
    ret.isValid = false;

    if (url.isLocalFile()) {
        QFileInfo info(url.toLocalFile());
        ret.isValid = info.exists();
        if (ret.isValid) {
            ret.isDir = info.isDir();
            ret.extension = info.suffix();
        }
    } else if (url.isValid()) {
        KIO::StatJob* statJob = KIO::stat(url, KIO::HideProgressInfo);
        KJobWidgets::setWindow(statJob, KDevelop::Core::self()->uiControllerInternal()->defaultMainWindow());
        ret.isValid = statJob->exec(); // TODO: do this asynchronously so that the user isn't blocked while typing every letter of the hostname in sftp://hostname
        if (ret.isValid) {
            KIO::UDSEntry entry = statJob->statResult();
            ret.isDir = entry.isDir();
            ret.extension = QFileInfo(entry.stringValue(KIO::UDSEntry::UDS_NAME)).suffix();
        }
    }
    return ret;
}
}

namespace KDevelop
{

OpenProjectDialog::OpenProjectDialog(bool fetch, const QUrl& startUrl,
                                     const QUrl& repoUrl, IPlugin* vcsOrProviderPlugin,
                                     QWidget* parent)
    : KAssistantDialog( parent )
    , m_urlIsDirectory(false)
    , sourcePage(nullptr)
    , openPage(nullptr)
    , projectInfoPage(nullptr)
{
    resize(QSize(700, 500));

    // KAssistantDialog creates a help button by default, no option to prevent that
    auto helpButton = button(QDialogButtonBox::Help);
    if (helpButton) {
        buttonBox()->removeButton(helpButton);
        delete helpButton;
    }

#ifndef KDEV_USE_NATIVE_DIALOGS
    // the user selected KDE file dialogs via the CMake option
    const bool useKdeFileDialog = true;
#else
    // the user selected native file dialogs via the CMake option
    const bool useKdeFileDialog = false;
#endif
    QStringList filters, allEntry;
    QString filterFormat = useKdeFileDialog
                         ? QStringLiteral("%1|%2 (%1)")
                         : QStringLiteral("%2 (%1)");
    allEntry << QLatin1String("*.") + ShellExtension::getInstance()->projectFileExtension();
    filters << filterFormat.arg(QLatin1String("*.") + ShellExtension::getInstance()->projectFileExtension(), ShellExtension::getInstance()->projectFileDescription());
    const QVector<KPluginMetaData> plugins = ICore::self()->pluginController()->queryExtensionPlugins(QStringLiteral("org.kdevelop.IProjectFileManager"));
    for (const KPluginMetaData& info : plugins) {
        m_projectPlugins.insert(info.name(), info);

        QStringList filter = KPluginMetaData::readStringList(info.rawData(), QStringLiteral("X-KDevelop-ProjectFilesFilter"));

        // some project file manager plugins like KDevGenericManager have no file filter set
        if (filter.isEmpty()) {
            m_genericProjectPlugins << info.name();
            continue;
        }
        QString desc = info.value(QStringLiteral("X-KDevelop-ProjectFilesFilterDescription"));

        m_projectFilters.insert(info.name(), filter);
        allEntry += filter;
        filters << filterFormat.arg(filter.join(QLatin1Char(' ')), desc);
    }

    if (useKdeFileDialog)
        filters.prepend(i18n("%1|All Project Files (%1)", allEntry.join(QLatin1Char(' '))));

    QUrl start = startUrl.isValid() ? startUrl : Core::self()->projectController()->projectsBaseDirectory();
    start = start.adjusted(QUrl::NormalizePathSegments);
    KPageWidgetItem* currentPage = nullptr;

    if( fetch ) {
        sourcePageWidget = new ProjectSourcePage(start, repoUrl, vcsOrProviderPlugin, this);
        connect( sourcePageWidget, &ProjectSourcePage::isCorrect, this, &OpenProjectDialog::validateSourcePage );
        sourcePage = addPage( sourcePageWidget, i18n("Select Source") );
        currentPage = sourcePage;
    }

    if (useKdeFileDialog) {
        openPageWidget = new OpenProjectPage( start, filters, this );
        connect( openPageWidget, &OpenProjectPage::urlSelected, this, &OpenProjectDialog::validateOpenUrl );
        connect( openPageWidget, &OpenProjectPage::accepted, this, &OpenProjectDialog::openPageAccepted );
        openPage = addPage( openPageWidget, i18n("Select a build system setup file, existing KDevelop project, "
                                                 "or any folder to open as a project") );

        if (!currentPage) {
            currentPage = openPage;
        }
    } else {
        nativeDialog = new QFileDialog(this, i18n("Open Project"));
        nativeDialog->setDirectoryUrl(start);
        nativeDialog->setFileMode(QFileDialog::Directory);
    }

    ProjectInfoPage* page = new ProjectInfoPage( this );
    connect( page, &ProjectInfoPage::projectNameChanged, this, &OpenProjectDialog::validateProjectName );
    connect( page, &ProjectInfoPage::projectManagerChanged, this, &OpenProjectDialog::validateProjectManager );
    projectInfoPage = addPage( page, i18n("Project Information") );

    if (!currentPage) {
        currentPage = projectInfoPage;
    }

    setValid( sourcePage, false );
    setValid( openPage, false );
    setValid( projectInfoPage, false);
    setAppropriate( projectInfoPage, false );

    setCurrentPage( currentPage );
    setWindowTitle(i18n("Open Project"));
}

bool OpenProjectDialog::execNativeDialog()
{
    while (true)
    {
        if (nativeDialog->exec()) {
            QUrl selectedUrl = nativeDialog->selectedUrls().at(0);
            if (urlInfo(selectedUrl).isValid) {
                // validate directory first to populate m_projectName and m_projectManager
                validateOpenUrl(selectedUrl.adjusted(QUrl::RemoveFilename));
                validateOpenUrl(selectedUrl);
                return true;
            }
        }
        else {
            return false;
        }
    }
}

int OpenProjectDialog::exec()
{
    if (nativeDialog && !execNativeDialog()) {
        reject();
        return QDialog::Rejected;
    }
    return KAssistantDialog::exec();
}

void OpenProjectDialog::validateSourcePage(bool valid)
{
    setValid(sourcePage, valid);
    if (!nativeDialog) {
        openPageWidget->setUrl(sourcePageWidget->workingDir());
    }
}

void OpenProjectDialog::validateOpenUrl( const QUrl& url_ )
{
    URLInfo urlInfo = ::urlInfo(url_);

    const QUrl url = url_.adjusted(QUrl::StripTrailingSlash);

    // openPage is used only in KDE
    if (openPage) {
        if ( urlInfo.isValid ) {
            // reset header
            openPage->setHeader(i18n("Open \"%1\" as project", url.fileName()));
        } else {
            // report error
            KColorScheme scheme(palette().currentColorGroup());
            const QString errorMsg = i18n("Selected URL is invalid");
            openPage->setHeader(QStringLiteral("<font color='%1'>%2</font>")
                .arg(scheme.foreground(KColorScheme::NegativeText).color().name(), errorMsg)
            );
            setAppropriate( projectInfoPage, false );
            setAppropriate( openPage, true );
            setValid( openPage, false );
            return;
        }
    }

    m_selected = url;

    if( urlInfo.isDir || urlInfo.extension != ShellExtension::getInstance()->projectFileExtension() )
    {
        m_urlIsDirectory = urlInfo.isDir;
        setAppropriate( projectInfoPage, true );
        m_url = url;
        if( !urlInfo.isDir ) {
            m_url = m_url.adjusted(QUrl::StripTrailingSlash | QUrl::RemoveFilename);
        }
        ProjectInfoPage* page = qobject_cast<ProjectInfoPage*>( projectInfoPage->widget() );
        if( page )
        {
            page->setProjectName( m_url.fileName() );
            // clear the filelist
            m_fileList.clear();

            if( urlInfo.isDir ) {
                // If a dir was selected fetch all files in it
                KIO::ListJob* job = KIO::listDir( m_url );
                connect( job, &KIO::ListJob::entries,
                                this, &OpenProjectDialog::storeFileList);
                KJobWidgets::setWindow(job, Core::self()->uiController()->activeMainWindow());
                job->exec();
            } else {
                // Else we'll just take the given file
                m_fileList << url.fileName();
            }
            // Now find a manager for the file(s) in our filelist.
            QVector<ProjectFileChoice> choices;
            Q_FOREACH ( const auto& file, m_fileList ) {
                auto plugins = projectManagerForFile(file);
                if ( plugins.contains(QStringLiteral("<built-in>")) ) {
                    plugins.removeAll(QStringLiteral("<built-in>"));
                    choices.append({i18n("Open existing file \"%1\"", file), QStringLiteral("<built-in>"), QString()});
                }
                choices.reserve(choices.size() + plugins.size());
                Q_FOREACH ( const auto& plugin, plugins ) {
                    auto meta = m_projectPlugins.value(plugin);
                    choices.append({file + QLatin1String(" (") + plugin + QLatin1Char(')'), meta.pluginId(), meta.iconName(), file});
                }
            }
            // add managers that work in any case (e.g. KDevGenericManager)
                choices.reserve(choices.size() + m_genericProjectPlugins.size());
            Q_FOREACH ( const auto& plugin, m_genericProjectPlugins ) {
                qCDebug(SHELL) << plugin;
                auto meta = m_projectPlugins.value(plugin);
                choices.append({plugin, meta.pluginId(), meta.iconName()});
            }
            page->populateProjectFileCombo(choices);
        }
        // Turn m_url into the full path to the project filename (default: /path/to/projectSrc/projectSrc.kdev4).
        // This is done only when m_url doesn't already point to such a file, typically because the user selected
        // one in the project open dialog. Omitting this check could lead to project files of the form
        // /path/to/projectSrc/SourceProject.kdev4/projectSrc.kdev4 .
        if (!m_url.toLocalFile().endsWith(QLatin1Char('.') + ShellExtension::getInstance()->projectFileExtension())) {
            m_url.setPath( m_url.path() + QLatin1Char('/') + m_url.fileName() + QLatin1Char('.') + ShellExtension::getInstance()->projectFileExtension() );
        }
    } else {
        setAppropriate( projectInfoPage, false );
        m_url = url;
        m_urlIsDirectory = false;
    }
    validateProjectInfo();
    setValid( openPage, true );
}

QStringList OpenProjectDialog::projectManagerForFile(const QString& file) const
{
    QStringList ret;
    foreach( const QString& manager, m_projectFilters.keys() )
    {
        foreach( const QString& filterexp, m_projectFilters.value(manager) )
        {
            QRegExp exp( filterexp, Qt::CaseSensitive, QRegExp::Wildcard );
            if ( exp.exactMatch(file) ) {
                ret.append(manager);
            }
        }
    }
    if ( file.endsWith(ShellExtension::getInstance()->projectFileExtension()) ) {
        ret.append(QStringLiteral("<built-in>"));
    }
    return ret;
}

void OpenProjectDialog::openPageAccepted()
{
    if ( isValid( openPage ) ) {
        next();
    }
}

void OpenProjectDialog::validateProjectName( const QString& name )
{
    if (name != m_projectName) {
        m_projectName = name;
        bool isEnteringProjectName = (currentPage() == projectInfoPage);
        // don't interfere with m_url when validateOpenUrl() is also likely to change it
        if (isEnteringProjectName) {
            if (m_projectDirUrl.isEmpty()) {
                // cache the selected project directory
                const auto urlInfo = ::urlInfo(m_url);
                if (urlInfo.isValid && urlInfo.isDir) {
                    m_projectDirUrl = m_url.adjusted(QUrl::StripTrailingSlash);
                } else {
                    // if !urlInfo.isValid the url almost certainly refers to a file that doesn't exist (yet)
                    // With the Generic Makefile proj.manager it can be the project file url, for instance.
                    m_projectDirUrl = m_url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
                }
            }

            const QUrl url(m_projectDirUrl);
            // construct a version of the project name that is safe for use as a filename, i.e.
            // a version that does not contain characters that are illegal or are best avoided:
            // replace square braces and dir separator-like characters with a '@' placeholder
            // replace colons with '=' and whitespace with underscores.
            QString safeName = m_projectName;
            safeName.replace(QRegularExpression(QStringLiteral("[\\\\/]")), QStringLiteral("@"));
            safeName = safeName.replace(QLatin1Char(':'), QLatin1Char('=')) \
                .replace(QRegExp(QStringLiteral("\\s")), QStringLiteral("_"));
            safeName += QLatin1Char('.') + ShellExtension::getInstance()->projectFileExtension();

            m_url.setPath(url.path() + QLatin1Char('/') + safeName);
            m_urlIsDirectory = false;
            qCDebug(SHELL) << "project name:" << m_projectName << "file name:" << safeName << "in" << url.path();
        }
    }
    validateProjectInfo();
}

void OpenProjectDialog::validateProjectInfo()
{
    setValid( projectInfoPage, (!projectName().isEmpty() && !projectManager().isEmpty()) );
}

void OpenProjectDialog::validateProjectManager( const QString& manager, const QString & fileName )
{
    m_projectManager = manager;
    
    if ( m_urlIsDirectory )
    {
        m_selected = m_url.resolved( QUrl(QLatin1String("./") + fileName) );
    }
    
    validateProjectInfo();
}

QUrl OpenProjectDialog::projectFileUrl() const
{
    return m_url;
}

QUrl OpenProjectDialog::selectedUrl() const
{
    return m_selected;
}

QString OpenProjectDialog::projectName() const
{
    return m_projectName;
}

QString OpenProjectDialog::projectManager() const
{
    return m_projectManager;
}

void OpenProjectDialog::storeFileList(KIO::Job*, const KIO::UDSEntryList& list)
{
    for (const KIO::UDSEntry& entry : list) {
        QString name = entry.stringValue( KIO::UDSEntry::UDS_NAME );
        if( name != QLatin1String(".") && name != QLatin1String("..") && !entry.isDir() )
        {
            m_fileList << name;
        }
    }
}

}


