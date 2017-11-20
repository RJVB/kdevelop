/* This file is part of the KDE project
 * Copyright (C) 2016 René J.V. Bertin <rjvbertin@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef KWIDGETSTYLESELECTOR_H

// #include <kconfigwidgets_export.h>

#include <QObject>

class QString;
class QIcon;
class QAction;
class KActionMenu;

class /*KCONFIGWIDGETS_EXPORT*/ KWidgetStyleSelector : public QObject
{
    Q_OBJECT
public:
    explicit KWidgetStyleSelector(QObject *parent = 0);
    virtual ~KWidgetStyleSelector();

    KActionMenu *createStyleSelectionMenu(const QIcon &icon, const QString &text, const QString &selectedStyleName=QString(), QObject *parent=0);
    KActionMenu *createStyleSelectionMenu(const QString &text, const QString &selectedStyleName=QString(), QObject *parent=0);
    KActionMenu *createStyleSelectionMenu(const QString &selectedStyleName=QString(), QObject *parent=0);

    QString currentStyle() const;

public Q_SLOTS:
    void activateStyle(const QString &styleName);

private:
    QString m_widgetStyle;
    QObject *m_parent;
};

#define KWIDGETSTYLESELECTOR_H
#endif
