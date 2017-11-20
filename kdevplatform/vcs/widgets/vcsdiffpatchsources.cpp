/*
    Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "vcsdiffpatchsources.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QTemporaryFile>

#include <KComboBox>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KTextEdit>

#include <interfaces/ibasicversioncontrol.h>
#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/iruncontroller.h>
#include <interfaces/isession.h>
#include "vcsdiff.h"
#include "vcsjob.h"
#include "debug.h"


using namespace KDevelop;

class VCSCommitMessageEditor : public KTextEdit {
    Q_OBJECT
public:
    VCSCommitMessageEditor()
        : m_minWidth(KTextEdit::minimumSizeHint().width())
    {}
    void setMinWidth(int w)
    {
        m_minWidth = w;
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    QSize minimumSizeHint() const override
    {
        return QSize(m_minWidth, KTextEdit::minimumSizeHint().height());
    }
protected:
    int m_minWidth;
};

VCSCommitDiffPatchSource::VCSCommitDiffPatchSource(VCSDiffUpdater* updater)
    : VCSDiffPatchSource(updater), m_vcs(updater->vcs())
{
    Q_ASSERT(m_vcs);
    m_commitMessageWidget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(m_commitMessageWidget.data());
    layout->setMargin(0);

    VCSCommitMessageEditor *editor = new VCSCommitMessageEditor;
    m_commitMessageEdit = editor;
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setLineWrapMode(QTextEdit::NoWrap);
    // set the message editor to be 72 characters wide.
    // Given the widget margins that requires 74 actual characters.
    editor->setMinWidth(editor->fontMetrics().width(QString(74, 'm')));
    m_vcs->setupCommitMessageEditor(updater->url(), editor);

    QHBoxLayout* titleLayout = new QHBoxLayout;
    QLabel *label = new QLabel(i18n("Commit Message:"));
    // Copy the message editor tooltip to the label to increase its chances of being seen
    label->setToolTip(editor->toolTip());
    titleLayout->addWidget(label);

    m_oldMessages = new KComboBox(editor);

    m_oldMessages->addItem(i18n("Old Messages"));
    foreach(const QString& message, oldMessages())
        m_oldMessages->addItem(message, message);
    m_oldMessages->setMaximumWidth(200);

    connect(m_oldMessages, static_cast<void(KComboBox::*)(const QString&)>(&KComboBox::currentIndexChanged), this, &VCSCommitDiffPatchSource::oldMessageChanged);

    titleLayout->addWidget(m_oldMessages);

    layout->addLayout(titleLayout);
    layout->addWidget(editor);
    connect(this, &VCSCommitDiffPatchSource::reviewCancelled, this, &VCSCommitDiffPatchSource::addMessageToHistory);
    connect(this, &VCSCommitDiffPatchSource::reviewFinished, this, &VCSCommitDiffPatchSource::addMessageToHistory);
}

QStringList VCSCommitDiffPatchSource::oldMessages() const
{
    KConfigGroup vcsGroup(ICore::self()->activeSession()->config(), "VCS");
    return vcsGroup.readEntry("OldCommitMessages", QStringList());
}

void VCSCommitDiffPatchSource::addMessageToHistory(const QString& message)
{
    if(ICore::self()->shuttingDown())
        return;

    KConfigGroup vcsGroup(ICore::self()->activeSession()->config(), "VCS");

    const int maxMessages = 10;
    QStringList oldMessages = vcsGroup.readEntry("OldCommitMessages", QStringList());

    oldMessages.removeAll(message);
    oldMessages.push_front(message);
    oldMessages = oldMessages.mid(0, maxMessages);

    vcsGroup.writeEntry("OldCommitMessages", oldMessages);
}

void VCSCommitDiffPatchSource::oldMessageChanged(const QString& text)
{
    if(m_oldMessages->currentIndex() != 0)
    {
        m_oldMessages->setCurrentIndex(0);
        m_commitMessageEdit.data()->setText(text);
    }
}

void VCSCommitDiffPatchSource::jobFinished(KJob *job)
{
    if (!job || job->error() != 0 )
    {
        QString details = job ? job->errorText() : QString();
        if (details.isEmpty()) {    //errorText may be empty
            details = i18n("For more detailed information please see the Version Control toolview");
        }
        KMessageBox::detailedError(nullptr, i18n("Unable to commit"), details, i18n("Commit unsuccessful"));
    }

    deleteLater();
}

VCSDiffPatchSource::VCSDiffPatchSource(VCSDiffUpdater* updater)
    : m_updater(updater)
{
    update();
    KDevelop::IBasicVersionControl* vcs = m_updater->vcs();
    QUrl url = m_updater->url();

    QScopedPointer<VcsJob> statusJob(vcs->status(QList<QUrl>() << url));
    QVariant varlist;

    if( statusJob->exec() && statusJob->status() == VcsJob::JobSucceeded )
    {
        varlist = statusJob->fetchResults();

        foreach( const QVariant &var, varlist.toList() )
        {
            VcsStatusInfo info = var.value<KDevelop::VcsStatusInfo>();

            m_infos += info;
            if(info.state()!=VcsStatusInfo::ItemUpToDate)
                m_selectable[info.url()] = info.state();
        }
    }
    else
        qCDebug(VCS) << "Couldn't get status for urls: " << url;
}

VCSDiffPatchSource::VCSDiffPatchSource(const KDevelop::VcsDiff& diff)
    : m_updater(nullptr)
{
    updateFromDiff(diff);
}

VCSDiffPatchSource::~VCSDiffPatchSource()
{
    QFile::remove(m_file.toLocalFile());
    delete m_updater;
    qDebug() << "~VCSDiffPatchSource()" << this;
}

QUrl VCSDiffPatchSource::baseDir() const {
    return m_base;
}

QUrl VCSDiffPatchSource::file() const {
    return m_file;
}

QString VCSDiffPatchSource::name() const {
    return m_name;
}

uint VCSDiffPatchSource::depth() const {
    return m_depth;
}

void VCSDiffPatchSource::updateFromDiff(const VcsDiff& vcsdiff)
{
    if(!m_file.isValid())
    {
        QTemporaryFile temp2(QDir::tempPath() + QLatin1String("/kdevelop_XXXXXX.patch"));
        temp2.setAutoRemove(false);
        temp2.open();
        QTextStream t2(&temp2);
        t2 << vcsdiff.diff();
        qCDebug(VCS) << "filename:" << temp2.fileName();
        m_file = QUrl::fromLocalFile(temp2.fileName());
        temp2.close();
    }else{
        QFile file(m_file.path());
        file.open(QIODevice::WriteOnly);
        QTextStream t2(&file);
        t2 << vcsdiff.diff();
    }

    qCDebug(VCS) << "using file" << m_file << vcsdiff.diff() << "base" << vcsdiff.baseDiff();

    m_name = QStringLiteral("VCS Diff");
    m_base = vcsdiff.baseDiff();
    m_depth = vcsdiff.depth();

    emit patchChanged();
}

void VCSDiffPatchSource::update() {
    if(!m_updater)
        return;
    m_updater->setContextLines(m_contextLines == 0? INT_MAX : m_contextLines);
    updateFromDiff(m_updater->update());
}

VCSCommitDiffPatchSource::~VCSCommitDiffPatchSource() {
    delete m_commitMessageWidget.data();
//     if (m_commitMessageWidget.data()) {
//         m_commitMessageWidget.data()->deleteLater();
//     }
    qDebug() << "~VCSCommitDiffPatchSource()" << this;
}

bool VCSCommitDiffPatchSource::canSelectFiles() const {
    return true;
}

QMap< QUrl, KDevelop::VcsStatusInfo::State> VCSDiffPatchSource::additionalSelectableFiles() const {
    return m_selectable;
}

QWidget* VCSCommitDiffPatchSource::customWidget() const {
    return m_commitMessageWidget.data();
}

QString VCSCommitDiffPatchSource::finishReviewCustomText() const {
    return i18nc("@action:button To make a commit", "Commit");
}

bool VCSCommitDiffPatchSource::canCancel() const {
    return true;
}

void VCSCommitDiffPatchSource::cancelReview() {

    QString message;

    if (m_commitMessageEdit)
        message = m_commitMessageEdit.data()->toPlainText();

    emit reviewCancelled(message);

    deleteLater();
}

bool VCSCommitDiffPatchSource::finishReview(QList< QUrl > selection) {

    QString message;

    if (m_commitMessageEdit)
        message = m_commitMessageEdit.data()->toPlainText();

    qCDebug(VCS) << "Finishing with selection" << selection;
    QString files;
    foreach(const QUrl& url, selection)
        files += "<li>"+ICore::self()->projectController()->prettyFileName(url, KDevelop::IProjectController::FormatPlain) + "</li>";

    QString text = i18n("<qt>Files will be committed:\n<ul>%1</ul>\nWith message:\n <pre>%2</pre></qt>", files, message);

    int res = KMessageBox::warningContinueCancel(nullptr, text, i18n("About to commit to repository"),
                                                 KStandardGuiItem::cont(), KStandardGuiItem::cancel(),
                                                 QStringLiteral("ShouldAskConfirmCommit"));
    if (res != KMessageBox::Continue) {
        return false;
    }

    emit reviewFinished(message, selection);

    VcsJob* job = m_vcs->commit(message, selection, KDevelop::IBasicVersionControl::NonRecursive);
    if (!job) {
        return false;
    }

    connect (job, &VcsJob::finished,
             this, &VCSCommitDiffPatchSource::jobFinished);
    ICore::self()->runController()->registerJob(job);
    return true;
}

bool showVcsDiff(IPatchSource* vcsDiff)
{
    KDevelop::IPatchReview* patchReview = ICore::self()->pluginController()->extensionForPlugin<IPatchReview>(QStringLiteral("org.kdevelop.IPatchReview"));

    if( patchReview ) {
        patchReview->startReview(vcsDiff);
        return true;
    } else {
        qCWarning(VCS) << "Patch review plugin not found";
        return false;
    }
}

VcsDiff VCSStandardDiffUpdater::update() const
{
    m_vcs->setDiffContextLines(m_contextLines);
    QScopedPointer<VcsJob> diffJob(m_vcs->diff(m_url,
                                   KDevelop::VcsRevision::createSpecialRevision(KDevelop::VcsRevision::Base),
                                   KDevelop::VcsRevision::createSpecialRevision(KDevelop::VcsRevision::Working)));
    const bool success = diffJob ? diffJob->exec() : false;
    if (!success) {
        KMessageBox::error(nullptr, i18n("Could not create a patch for the current version."));
        return {};
    }

    return diffJob->fetchResults().value<VcsDiff>();
}

VCSStandardDiffUpdater::VCSStandardDiffUpdater(IBasicVersionControl* vcs, QUrl url) : m_vcs(vcs), m_url(url) {
}

VCSStandardDiffUpdater::~VCSStandardDiffUpdater() {
}

VCSDiffUpdater::~VCSDiffUpdater() {
}

#include "vcsdiffpatchsources.moc"
