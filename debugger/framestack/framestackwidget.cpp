/*
 * This file is part of KDevelop
 *
 * Copyright 1999 John Birch <jbb@kdevelop.org>
 * Copyright 2007 Hamish Rodda <rodda@kde.org>
 * Copyright 2009 Niko Sams <niko.sams@gmail.com>
 * Copyright 2009 Aleix Pol <aleixpol@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
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

#include "framestackwidget.h"

#include <QHeaderView>
#include <QScrollBar>
#include <QListView>
#include <QItemDelegate>
#include <QVBoxLayout>
#include <QLabel>
#include <QTreeView>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QIcon>
#include <QResizeEvent>
#include <QAction>

#include <KStandardAction>
#include <KLocalizedString>
#include <KTextEditor/Cursor>

#include <interfaces/icore.h>
#include <interfaces/idebugcontroller.h>
#include <interfaces/idocumentcontroller.h>
#include "util/debug.h"
#include "framestackmodel.h"

namespace KDevelop {

class FrameStackItemDelegate : public QItemDelegate
{
    Q_OBJECT

public:
    using QItemDelegate::QItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

void FrameStackItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const
{
    QStyleOptionViewItem newOption(option);
    newOption.textElideMode = index.column() == 2 ? Qt::ElideMiddle : Qt::ElideRight;

    QItemDelegate::paint(painter, newOption, index);
}

FramestackWidget::FramestackWidget(IDebugController* controller, QWidget* parent)
    : AutoOrientedSplitter(Qt::Horizontal, parent), m_session(nullptr)
{
    connect(controller,
            &IDebugController::currentSessionChanged,
            this, &FramestackWidget::currentSessionChanged);

    //TODO: shouldn't this signal be in IDebugController? Otherwise we are effectively depending on it being a DebugController here
    connect(controller, SIGNAL(raiseFramestackViews()), SIGNAL(requestRaise()));

    setWhatsThis(i18n("<b>Frame stack</b>"
                      "Often referred to as the \"call stack\", "
                      "this is a list showing which function is "
                      "currently active, and what called each "
                      "function to get to this point in your "
                      "program. By clicking on an item you "
                      "can see the values in any of the "
                      "previous calling functions."));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("view-list-text"), windowIcon()));
    m_threadsWidget = new QWidget(this);
    m_threadsListView = new QListView(m_threadsWidget);
    m_framesTreeView = new QTreeView(this);
    m_framesTreeView->setRootIsDecorated(false);
    m_framesTreeView->setItemDelegate(new FrameStackItemDelegate(this));
    m_framesTreeView->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_framesTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_framesTreeView->setAllColumnsShowFocus(true);
    m_framesTreeView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_framesContextMenu = new QMenu(m_framesTreeView);

    QAction* selectAllAction = KStandardAction::selectAll(m_framesTreeView);
    selectAllAction->setShortcut(QKeySequence()); //FIXME: why does CTRL-A conflict with Katepart (while CTRL-Cbelow doesn't) ?
    selectAllAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllAction, &QAction::triggered, this, &FramestackWidget::selectAll);
    m_framesContextMenu->addAction(selectAllAction);

    QAction* copyAction = KStandardAction::copy(m_framesTreeView);
    copyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copyAction, &QAction::triggered, this, &FramestackWidget::copySelection);
    m_framesContextMenu->addAction(copyAction);
    addAction(copyAction);

    connect(m_framesTreeView, &QTreeView::customContextMenuRequested, this, &FramestackWidget::frameContextMenuRequested);

    m_threadsWidget->setLayout(new QVBoxLayout());
    m_threadsWidget->layout()->addWidget(new QLabel(i18n("Threads:")));
    m_threadsWidget->layout()->addWidget(m_threadsListView);
    addWidget(m_threadsWidget);
    addWidget(m_framesTreeView);

    setStretchFactor(1, 3);
    connect(m_framesTreeView->verticalScrollBar(), &QScrollBar::valueChanged, this, &FramestackWidget::checkFetchMoreFrames);

    // Show the selected frame when clicked, even if it has previously been selected
    connect(m_framesTreeView, &QTreeView::clicked, this, &FramestackWidget::frameSelectionChanged);

    currentSessionChanged(controller->currentSession());
}

FramestackWidget::~FramestackWidget()
{
}

void FramestackWidget::currentSessionChanged(KDevelop::IDebugSession* session)
{
    m_session = session;

    m_threadsListView->setModel(session ? session->frameStackModel() : nullptr);
    m_framesTreeView->setModel(session ? session->frameStackModel() : nullptr);

    if (session) {
        connect(session->frameStackModel(), &IFrameStackModel::dataChanged,
                this, &FramestackWidget::checkFetchMoreFrames);
        connect(session->frameStackModel(), &IFrameStackModel::currentThreadChanged,
                this, &FramestackWidget::currentThreadChanged);
        currentThreadChanged(session->frameStackModel()->currentThread());
        connect(session->frameStackModel(), &IFrameStackModel::currentFrameChanged,
                this, &FramestackWidget::currentFrameChanged);
        currentFrameChanged(session->frameStackModel()->currentFrame());
        connect(session, &IDebugSession::stateChanged,
                this, &FramestackWidget::sessionStateChanged);

        connect(m_threadsListView->selectionModel(), &QItemSelectionModel::currentChanged,
                this, &FramestackWidget::setThreadShown);

        // Show the selected frame, independent of the means by which it has been selected
        connect(m_framesTreeView->selectionModel(), &QItemSelectionModel::currentChanged,
                this, &FramestackWidget::frameSelectionChanged);

        sessionStateChanged(session->state());
    }
}

void FramestackWidget::setThreadShown(const QModelIndex& current)
{
    if (!current.isValid())
        return;
    m_session->frameStackModel()->setCurrentThread(current);
}

void FramestackWidget::checkFetchMoreFrames()
{
    int val = m_framesTreeView->verticalScrollBar()->value();
    int max = m_framesTreeView->verticalScrollBar()->maximum();
    const int offset = 20;

    if (val + offset > max && m_session) {
        m_session->frameStackModel()->fetchMoreFrames();
    }
}

void FramestackWidget::currentThreadChanged(int thread)
{
    if (thread != -1) {
        IFrameStackModel* model = m_session->frameStackModel();
        QModelIndex idx = model->currentThreadIndex();
        m_threadsListView->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_threadsWidget->setVisible(model->rowCount() > 1);
        m_framesTreeView->setRootIndex(idx);
        m_framesTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    } else {
        m_threadsWidget->hide();
        m_threadsListView->selectionModel()->clear();
        m_framesTreeView->setRootIndex(QModelIndex());
    }
}

void FramestackWidget::currentFrameChanged(int frame)
{
    if (frame != -1) {
        IFrameStackModel* model = m_session->frameStackModel();
        QModelIndex idx = model->currentFrameIndex();
        m_framesTreeView->selectionModel()->select(
            idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    } else {
        m_framesTreeView->selectionModel()->clear();
    }
}

void FramestackWidget::frameSelectionChanged(const QModelIndex& current /* previous */)
{
    if (!current.isValid())
        return;
    IFrameStackModel::FrameItem f = m_session->frameStackModel()->frame(current);
    /* If line is -1, then it's not a source file at all.  */
    if (f.line != -1) {
        QPair<QUrl, int> file = m_session->convertToLocalUrl(qMakePair(f.file, f.line));
        ICore::self()->documentController()->openDocument(file.first, KTextEditor::Cursor(file.second, 0), IDocumentController::DoNotFocus);
    }

    m_session->frameStackModel()->setCurrentFrame(f.nr);
}

void FramestackWidget::frameContextMenuRequested(const QPoint& pos)
{
    m_framesContextMenu->popup( m_framesTreeView->mapToGlobal(pos) + QPoint(0, m_framesTreeView->header()->height()) );
}

void FramestackWidget::copySelection()
{
    QClipboard *cb = QApplication::clipboard();
    QModelIndexList indexes = m_framesTreeView->selectionModel()->selectedRows();
    QString content;
    Q_FOREACH(const QModelIndex& index, indexes) {
        IFrameStackModel::FrameItem frame = m_session->frameStackModel()->frame(index);
        if (frame.line == -1) {
            content += i18nc("#frame function() at file", "#%1 %2() at %3\n",
                frame.nr, frame.name, frame.file.url(QUrl::PreferLocalFile | QUrl::StripTrailingSlash));
        } else {
            content += i18nc("#frame function() at file:line", "#%1 %2() at %3:%4\n",
                frame.nr, frame.name, frame.file.url(QUrl::PreferLocalFile | QUrl::StripTrailingSlash), frame.line+1);
        }
    }
    cb->setText(content);
}

void FramestackWidget::selectAll()
{
    m_framesTreeView->selectAll();
}

void FramestackWidget::sessionStateChanged(KDevelop::IDebugSession::DebuggerState state)
{
    bool enable = state == IDebugSession::PausedState || state == IDebugSession::StoppedState;
    setEnabled(enable);
}

}

#include "framestackwidget.moc"
