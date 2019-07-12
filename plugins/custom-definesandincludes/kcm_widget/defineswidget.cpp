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

#include "defineswidget.h"

#include <KLocalizedString>
#include <QAction>

#include "../ui_defineswidget.h"
#include "definesmodel.h"
#include <debug.h>

using namespace KDevelop;

DefinesWidget::DefinesWidget( QWidget* parent )
    : QWidget ( parent ), ui( new Ui::DefinesWidget )
    , definesModel( new DefinesModel( this ) )
{
    ui->setupUi( this );
    ui->defines->setModel( definesModel );
    ui->defines->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    connect( definesModel, &DefinesModel::dataChanged, this, static_cast<void(DefinesWidget::*)()>(&DefinesWidget::definesChanged) );
    connect( definesModel, &DefinesModel::rowsInserted, this, static_cast<void(DefinesWidget::*)()>(&DefinesWidget::definesChanged) );
    connect( definesModel, &DefinesModel::rowsRemoved, this, static_cast<void(DefinesWidget::*)()>(&DefinesWidget::definesChanged) );

    QAction* delDefAction = new QAction( i18n("Delete Define"), this );
    delDefAction->setShortcut( QKeySequence(Qt::Key_Delete) );
    delDefAction->setShortcutContext( Qt::WidgetWithChildrenShortcut );
    delDefAction->setIcon( QIcon::fromTheme(QStringLiteral("edit-delete")) );
    ui->defines->addAction( delDefAction );
    ui->defines->setContextMenuPolicy( Qt::ActionsContextMenu );
    connect( delDefAction, &QAction::triggered, this, &DefinesWidget::deleteDefine );
}

DefinesWidget::~DefinesWidget()
{
}

void DefinesWidget::setDefines( const Defines& defines )
{
    bool b = blockSignals( true );
    clear();
    definesModel->setDefines( defines );
    blockSignals( b );
}

void DefinesWidget::definesChanged()
{
    qCDebug(DEFINESANDINCLUDES) << "defines changed";
    emit definesChanged( definesModel->defines() );
}

void DefinesWidget::clear()
{
    definesModel->setDefines( {} );
}

void DefinesWidget::deleteDefine()
{
    qCDebug(DEFINESANDINCLUDES) << "Deleting defines";
    const QModelIndexList selection = ui->defines->selectionModel()->selectedRows();
    for (const QModelIndex& row : selection) {
        definesModel->removeRow( row.row() );
    }
}

