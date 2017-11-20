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

#ifndef IDEALDOCKWIDGET_H
#define IDEALDOCKWIDGET_H

#include <QDockWidget>
#include "idealcontroller.h"
#include "sublimeexport.h"

namespace Sublime {
class KDEVPLATFORMSUBLIME_EXPORT IdealDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    IdealDockWidget(IdealController *controller, Sublime::MainWindow *parent);
    ~IdealDockWidget() override;

    Area *area() const;
    void setArea(Area *area);

    View *view() const;
    void setView(View *view);

    Qt::DockWidgetArea dockWidgetArea() const;
    void setDockWidgetArea(Qt::DockWidgetArea dockingArea);

    /**
     * @brief overloads QDockWidget::setFloating to provide support
     * for floating windows that behave like regular standalone windows.
     */
    void setFloating(bool floating);
    /**
     * @brief controls whether dock widgets are detached (floated)
     * as regular @p standalone windows or as tool windows (Qt::Tool)
     */
    void setFloatsAsStandalone(bool standalone);
    /**
     * @brief returns true if floating (detached) windows behave as standalone
     * (regular) windows instead of Qt's standard tool windows
     */
    bool floatsAsStandalone();

public Q_SLOTS:
    /// The IdealToolButton also connects to this slot to show the same context menu.
    void contextMenuRequested(const QPoint &point);

Q_SIGNALS:
    void closeRequested();

private Q_SLOTS:
    void slotRemove();
    /**
     * @brief re-attaches views that have been set to float as standalone windows
     * when the application is about to shutdown.
     */
    void aboutToShutdown();

private:
    Q_INVOKABLE void makeStandaloneWindow();
    void reDockWidget(bool signalClose);

    Qt::Orientation m_orientation;
    Area *m_area;
    View *m_view;
    Qt::DockWidgetArea m_docking_area;
    IdealController *m_controller;
    QWidget *m_floatingWidget;
    bool m_floatsAsStandalone;
};

}

#endif // IDEALDOCKWIDGET_H
