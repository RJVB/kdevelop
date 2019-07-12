/************************************************************************
 *                                                                      *
 * Copyright 2010 Andreas Pakulat <apaku@gmx.de>                        *
 *                                                                      *
 * This program is free software; you can redistribute it and/or modify *
 * it under the terms of the GNU General Public License as published by *
 * the Free Software Foundation; either version 2 or version 3 of the License, or    *
 * (at your option) any later version.                                  *
 *                                                                      *
 * This program is distributed in the hope that it will be useful, but  *
 * WITHOUT ANY WARRANTY; without even the implied warranty of           *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU     *
 * General Public License for more details.                             *
 *                                                                      *
 * You should have received a copy of the GNU General Public License    *
 * along with this program; if not, see <http://www.gnu.org/licenses/>. *
 ************************************************************************/

#ifndef KDEVELOP_PROJECTMANAGERS_CUSTOM_BUILDSYSTEM_DEFINESWIDGET_H
#define KDEVELOP_PROJECTMANAGERS_CUSTOM_BUILDSYSTEM_DEFINESWIDGET_H

#include <QWidget>

#include "idefinesandincludesmanager.h"

namespace Ui
{
class DefinesWidget;
}

namespace KDevelop
{
    class IProject;
}

class DefinesModel;

class DefinesWidget : public QWidget
{
Q_OBJECT
public:
    explicit DefinesWidget( QWidget* parent = nullptr );
    ~DefinesWidget() override;

    void setDefines( const KDevelop::Defines& defines );
    void clear();
Q_SIGNALS:
    void definesChanged( const KDevelop::Defines& defines );
private Q_SLOTS:
    // Forward defines model changes
    void definesChanged();

    // Handle Del key in defines list
    void deleteDefine();
private:
    QScopedPointer<Ui::DefinesWidget> ui;
    DefinesModel* definesModel;
};

#endif
