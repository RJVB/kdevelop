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

#include "kwidgetstyleselector.h"

#ifdef Q_OS_WIN
#include <QSysInfo>
#endif
#include <QString>
#include <QAction>
#include <QIcon>
#include <QStyle>
#include <QStyleFactory>
#include <QApplication>
#include <QDebug>

#include <kactionmenu.h>
#include <ksharedconfig.h>
#include <kconfiggroup.h>
#include <klocalizedstring.h>

static QString getDefaultStyle(const char *fallback=Q_NULLPTR)
{
    KSharedConfigPtr kdeGlobals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::NoGlobals);
    KConfigGroup cg(kdeGlobals, "KDE");
    if (!fallback) {
#ifdef Q_OS_OSX
        fallback = "Macintosh";
#elif defined(Q_OS_WIN)
        // taken from QWindowsTheme::styleNames()
        if (QSysInfo::windowsVersion() >= QSysInfo::WV_VISTA) {
            fallback = "WindowsVista";
        } else if (QSysInfo::windowsVersion() >= QSysInfo::WV_XP) {
            fallback = "WindowsXP";
        } else {
            fallback = "Windows";
        }
#else
        fallback = "Breeze";
#endif
    }
    return cg.readEntry("widgetStyle", fallback);
}

KWidgetStyleSelector::KWidgetStyleSelector(QObject *parent)
    : QObject(parent)
    , m_widgetStyle(QString())
    , m_parent(parent)
{
}

KWidgetStyleSelector::~KWidgetStyleSelector()
{
}

KActionMenu *KWidgetStyleSelector::createStyleSelectionMenu(const QIcon &icon, const QString &text,
                                                            const QString &selectedStyleName, QObject *parent)
{
    // Taken from Kdenlive:
    // Widget themes for non KDE users
    if (!parent) {
        parent = m_parent;
    }
    KActionMenu *stylesAction= new KActionMenu(icon, text, parent);
    QActionGroup *stylesGroup = new QActionGroup(stylesAction);

    QStringList availableStyles = QStyleFactory::keys();
    QString desktopStyle = QApplication::style()->objectName();
    QString defaultStyle = getDefaultStyle();

    // Add default style action
    QAction *defaultStyleAction = new QAction(i18n("Default"), stylesGroup);
    defaultStyleAction->setCheckable(true);
    stylesAction->addAction(defaultStyleAction);
    m_widgetStyle = selectedStyleName;
    bool setStyle = false;
    if (m_widgetStyle.isEmpty()) {
        if (desktopStyle.compare(defaultStyle, Qt::CaseInsensitive) == 0) {
            defaultStyleAction->setChecked(true);
            m_widgetStyle = defaultStyleAction->text();
        } else {
            m_widgetStyle = desktopStyle;
        }
    } else if (selectedStyleName.compare(desktopStyle, Qt::CaseInsensitive)) {
        setStyle = true;
    }

    foreach(const QString &style, availableStyles) {
        QAction *a = new QAction(style, stylesGroup);
        a->setCheckable(true);
        a->setData(style);
        if (style.compare(defaultStyle, Qt::CaseInsensitive) == 0) {
            QFont defFont = a->font();
            defFont.setBold(true);
            a->setFont(defFont);
        }
        if (m_widgetStyle.compare(style, Qt::CaseInsensitive) == 0) {
            a->setChecked(true);
            if (setStyle) {
                // selectedStyleName was not empty and the
                // the style exists: activate it.
                activateStyle(style);
            }
        }
        stylesAction->addAction(a);
    }
    connect(stylesGroup, &QActionGroup::triggered, this, [&](QAction *a) {
        activateStyle(a->data().toString());
    });
    return stylesAction;
}

KActionMenu *KWidgetStyleSelector::createStyleSelectionMenu(const QString &text,
                                                            const QString &selectedStyleName, QObject *parent)
{
    return createStyleSelectionMenu(QIcon(), text, selectedStyleName, parent);
}

KActionMenu *KWidgetStyleSelector::createStyleSelectionMenu(const QString &selectedStyleName, QObject *parent)
{
    return createStyleSelectionMenu(QIcon(), i18n("Style"), selectedStyleName, parent);
}

QString KWidgetStyleSelector::currentStyle() const
{
    if (m_widgetStyle.isEmpty() || m_widgetStyle == QStringLiteral("Default")) {
        return getDefaultStyle();
    }
    return m_widgetStyle;
}

void KWidgetStyleSelector::activateStyle(const QString &styleName)
{
    m_widgetStyle = styleName;
    QApplication::setStyle(QStyleFactory::create(currentStyle()));
}
