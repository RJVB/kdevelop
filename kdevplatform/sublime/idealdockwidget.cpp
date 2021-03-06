/*
  Copyright 2007 Roberto Raggi <roberto@kdevelop.org>
  Copyright 2007 Hamish Rodda <rodda@kde.org>
  Copyright 2011 Alexander Dymo <adymo@kdevelop.org>

  Permission to use, copy, modify, distribute, and sell this software and its
  documentation for any purpose is hereby granted without fee, provided that
  the above copyright notice appear in all copies and that both that
  copyright notice and this permission notice appear in supporting
  documentation.

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  KDEVELOP TEAM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
  AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "idealdockwidget.h"
#include "mainwindow.h"
#include "area.h"
#include "document.h"
#include "view.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KShortcutWidget>

#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QToolBar>
#include <QVBoxLayout>

#include "debug.h"
#include <QCoreApplication>

using namespace Sublime;

IdealDockWidget::IdealDockWidget(IdealController *controller, Sublime::MainWindow *parent)
    : QDockWidget(parent),
      m_area(nullptr),
      m_view(nullptr),
      m_docking_area(Qt::NoDockWidgetArea),
      m_controller(controller),
      m_floatingWidget(nullptr),
      m_floatsAsStandalone(false)
{
    setAutoFillBackground(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &IdealDockWidget::customContextMenuRequested,
            this, &IdealDockWidget::contextMenuRequested);

    QAbstractButton *closeButton = findChild<QAbstractButton *>(QStringLiteral("qt_dockwidget_closebutton"));
    if (closeButton) {
    disconnect(closeButton, &QAbstractButton::clicked, nullptr, nullptr);

    connect(closeButton, &QAbstractButton::clicked, this, &IdealDockWidget::closeRequested);
    }

    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    // do not allow to move docks to the top dock area (no buttonbar there in our current UI)
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    connect(this, &QDockWidget::topLevelChanged, this, [this] (bool floating) {
            if (floating && isVisible() && floatsAsStandalone()) {
                QMetaObject::invokeMethod(this, "makeStandaloneWindow", Qt::QueuedConnection);
            }
        } );
}

IdealDockWidget::~IdealDockWidget()
{
}

Area *IdealDockWidget::area() const
{ return m_area; }

void IdealDockWidget::setArea(Area *area)
{ m_area = area; }

View *IdealDockWidget::view() const
{ return m_view; }

void IdealDockWidget::setView(View *view)
{ m_view = view; }

Qt::DockWidgetArea IdealDockWidget::dockWidgetArea() const
{ return m_docking_area; }

void IdealDockWidget::setDockWidgetArea(Qt::DockWidgetArea dockingArea)
{ m_docking_area = dockingArea; }

void IdealDockWidget::setFloating(bool floating)
{
    if (!m_floatsAsStandalone) {
        QDockWidget::setFloating(floating);
    } else {
        if (floating) {
            makeStandaloneWindow();
        } else {
            reDockWidget(false);
        }
    }
}

void IdealDockWidget::setFloatsAsStandalone(bool standalone)
{
    m_floatsAsStandalone = standalone;
}

bool IdealDockWidget::floatsAsStandalone()
{
    return m_floatsAsStandalone;
}

void IdealDockWidget::slotRemove()
{
    if (m_floatingWidget) {
        setWidget(m_floatingWidget);
        m_floatingWidget = nullptr;
    }
    if (m_area) {
        m_area->removeToolView(m_view);
    }
}

void IdealDockWidget::makeStandaloneWindow()
{
    if (!m_floatingWidget) {
        if (!isFloating()) {
            QDockWidget::setFloating(true);
            m_area->raiseToolView(m_view);
        }
        if (auto w = widget()) {
            // turn into top-level window
            qCDebug(SUBLIME) << "reparenting" << this << "widget" << w << "away from" << w->parent();
            m_floatingWidget = w;
            w->setParent(nullptr);
            w->setWindowFlags(Qt::Window);
            w->show();
            connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &IdealDockWidget::aboutToShutdown );
            close();
        }
    }
}

void IdealDockWidget::reDockWidget(bool signalClose)
{
    if (m_floatingWidget) {
        setWidget(m_floatingWidget);
        m_floatingWidget = nullptr;
    }
    QDockWidget::setFloating(false);
    if (signalClose) {
        emit closeRequested();
    }
}

void IdealDockWidget::aboutToShutdown()
{
    qCDebug(SUBLIME) << "Re-docking" << this << "before exit";
    if (m_floatingWidget) {
        reDockWidget(true);
    }
}

void IdealDockWidget::contextMenuRequested(const QPoint &point)
{
    QWidget* senderWidget = qobject_cast<QWidget*>(sender());
    Q_ASSERT(senderWidget);

    QMenu menu;
    menu.addSection(windowIcon(), m_view->document()->title());

    const QList<QAction*> viewActions = m_view->contextMenuActions();
    if(!viewActions.isEmpty()) {
        // add the view's actions to the context menu,
        // checking each if it can be represented
        for (const auto action : viewActions) {
            if (!action->text().isEmpty() && !action->iconText().isEmpty()) {
                // avoid adding empty menu items
                menu.addAction(action);
            }
        }
        menu.addSeparator();
    }

    ///TODO: can this be cleaned up?
    if(QToolBar* toolBar = widget()->findChild<QToolBar*>()) {
        menu.addAction(toolBar->toggleViewAction());
        menu.addSeparator();
    }

    /// start position menu
    QMenu* positionMenu = menu.addMenu(i18n("Tool View Position"));

    QActionGroup* g = new QActionGroup(positionMenu);

    QAction *left = new QAction(i18nc("tool view position", "Left"), g);
    QAction *bottom = new QAction(i18nc("tool view position", "Bottom"), g);
    QAction *right = new QAction(i18nc("tool view position", "Right"), g);
    QAction *detach = nullptr;
    if (!floatsAsStandalone()) {
        detach = new QAction(i18nc("toolview position", "Detached as floating window"), g);
    }
    QAction *standalone = new QAction(i18nc("toolview position", "Detached as standalone window"), g);

    for (auto action : {left, bottom, right, detach, standalone}) {
        if (action) {
            positionMenu->addAction(action);
            action->setCheckable(true);
        }
    }
    if (m_floatingWidget) {
        standalone->setChecked(true);
    } else if (isFloating() && detach) {
        detach->setChecked(true);
    } else if (m_docking_area == Qt::BottomDockWidgetArea)
        bottom->setChecked(true);
    else if (m_docking_area == Qt::LeftDockWidgetArea)
        left->setChecked(true);
    else if (m_docking_area == Qt::RightDockWidgetArea)
        right->setChecked(true);
    /// end position menu

    menu.addSeparator();

    QAction *setShortcut = menu.addAction(QIcon::fromTheme(QStringLiteral("configure-shortcuts")), i18n("Assign Shortcut..."));
    setShortcut->setToolTip(i18n("Use this shortcut to trigger visibility of the tool view."));

    menu.addSeparator();
    QAction* remove = menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-close")), i18n("Remove Tool View"));

    QAction* triggered = menu.exec(senderWidget->mapToGlobal(point));

    if (triggered)
    {
        if ( triggered == remove ) {
            slotRemove();
            return;
        } else if ( triggered == setShortcut ) {
            QDialog* dialog(new QDialog(this));
            dialog->setWindowTitle(i18n("Assign Shortcut For '%1' Tool View", m_view->document()->title()));
            KShortcutWidget *w = new KShortcutWidget(dialog);
            w->setShortcut(m_controller->actionForView(m_view)->shortcuts());
            QVBoxLayout* dialogLayout = new QVBoxLayout(dialog);
            dialogLayout->addWidget(w);
            QDialogButtonBox* buttonBox = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
            dialogLayout->addWidget(buttonBox);
            connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
            connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

            if (dialog->exec() == QDialog::Accepted) {
                m_controller->actionForView(m_view)->setShortcuts(w->shortcut());

                //save shortcut config
                KConfigGroup config = KSharedConfig::openConfig()->group("UI");
                const QStringList shortcuts{
                    w->shortcut().value(0).toString(),
                    w->shortcut().value(1).toString(),
                };
                config.writeEntry(QStringLiteral("Shortcut for %1").arg(m_view->document()->title()), shortcuts);
                config.sync();
            }
            delete dialog;

            return;
        } else if ( triggered == detach ) {
            if (!m_floatingWidget && !isFloating()) {
                setFloating(true);
                m_area->raiseToolView(m_view);
            }
            return;
        } else if ( triggered == standalone ) {
            // this will invalidate m_area
            makeStandaloneWindow();
            return;
        }

        if (isFloating()) {
            reDockWidget(false);
        }

        Sublime::Position pos;
        if (triggered == left)
            pos = Sublime::Left;
        else if (triggered == bottom)
            pos = Sublime::Bottom;
        else if (triggered == right)
            pos = Sublime::Right;
        else
            return;

        if (m_area) {
            Area *area = m_area;
            View *view = m_view;
            /* This call will delete *this, so we no longer
               can access member variables. */
            m_area->moveToolView(m_view, pos);
            area->raiseToolView(view);
        }
    }
}
