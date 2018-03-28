/*
 * GDB Debugger Support
 *
 * Copyright 2003 John Birch <jbb@kdevelop.org>
 * Copyright 2007 Hamish Rodda <rodda@kde.org>
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

#ifndef _GDBOUTPUTWIDGET_H_
#define _GDBOUTPUTWIDGET_H_

#include "dbgglobal.h"

#include <QPlainTextEdit>
#include <QTimer>
#include <QStringList>

namespace KDevelop {
class IDebugSession;
}

class KHistoryComboBox;
class QToolButton;

namespace KDevMI
{
namespace GDB
{

class GDBController;
class CppDebuggerPlugin;

class GDBOutputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GDBOutputWidget(CppDebuggerPlugin* plugin, QWidget *parent=nullptr );
    ~GDBOutputWidget() override;

    void savePartialProjectSession();
    void restorePartialProjectSession();

    bool showInternalCommands() const;

public Q_SLOTS:
    void clear();

    void slotInternalCommandStdout(const QString& line);
    void slotUserCommandStdout(const QString& line);
    void slotReceivedStderr(const char* line);
    void slotStateChanged(DBGStateFlags oldStatus, DBGStateFlags newStatus);

    void slotGDBCmd();

    void flushPending();

    void copyAll();
    void toggleShowInternalCommands();

private Q_SLOTS:
    void currentSessionChanged(KDevelop::IDebugSession *session);
    void updateColors();

protected:
    void focusInEvent(QFocusEvent *e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

Q_SIGNALS:
    void requestRaise();
    void userGDBCmd(const QString &cmd);
    void breakInto();

private:
    void newStdoutLine(const QString& line, bool internal);

    /** Arranges for 'line' to be shown to the user.
        Adds 'line' to pendingOutput_ and makes sure
        updateTimer_ is running. */
    void showLine(const QString& line);

    /** Makes 'l' no longer than 'max_size' by
        removing excessive elements from the top.
    */
    void trimList(QStringList& l, int max_size);

    GDBController* m_controller;
    KHistoryComboBox*  m_userGDBCmdEditor;
    QToolButton*    m_Interrupt;
    QPlainTextEdit*      m_gdbView;

    bool m_cmdEditorHadFocus;

    void setShowInternalCommands(bool);
    friend class OutputText;

    /** The output from user commands only and from
        all commands. We keep it here so that if we switch
        "Show internal commands" on, we can show previous 
        internal commands. 
    */
    QStringList m_userCommands_, m_allCommands;
    /** Same output, without any fancy formatting.  Keeping it
        here because I can't find any way to extract raw text,
        without formatting, out of QTextEdit except for
        selecting everything and calling 'copy()'. The latter
        approach is just ugly.  */
    QStringList m_userCommandsRaw, m_allCommandsRaw;


    /** For performance reasons, we don't immediately add new text
        to QTExtEdit. Instead we add it to pendingOutput_ and 
        flush it on timer.
    */
    QString m_pendingOutput;
    QTimer m_updateTimer;

    bool m_showInternalCommands;

    int m_maxLines;

    QColor m_gdbColor;
    QColor m_errorColor;
};

class OutputTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit OutputTextEdit(GDBOutputWidget* parent);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
};

} // end of namespace GDB
} // end of namespace KDevMI

#endif
