/***************************************************************************
 *   Copyright 1999-2001 Bernd Gehrmann and the KDevelop Team              *
 *   bernd@kdevelop.org                                                    *
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "grepoutputmodel.h"
#include "grepviewpart.h"
#include <QModelIndex>
#include <kcolorscheme.h>
#include <ktexteditor/cursor.h>
#include <icore.h>
#include <idocumentcontroller.h>

GrepOutputItem::GrepOutputItem(const QString &fileName, const QString &lineNumber,
                   const QString &text, bool showFilename)
    : QStandardItem(), m_fileName(fileName), m_lineNumber(lineNumber)
    , m_text(text), m_showFilename( showFilename )
{
    if( !showFilename )
    {
        QString formattedTxt = lineNumber + ": " + text;
        setText( formattedTxt );
        setForeground(KColorScheme().foreground(KColorScheme::LinkText));
    }
    else
    {
        setText( fileName );
        setForeground(KColorScheme().foreground(KColorScheme::InactiveText));
    }
}

GrepOutputItem::~GrepOutputItem()
{}

///////////////////////////////////////////////////////////////

GrepOutputModel::GrepOutputModel( GrepViewPart *part )
    : QStandardItemModel( part )
    , m_part(part), _lastfilename(QString())
{}
GrepOutputModel::~GrepOutputModel()
{}

void GrepOutputModel::activate( const QModelIndex &idx )
{
    QStandardItem *stditem = itemFromIndex(idx);
    GrepOutputItem *grepitem = dynamic_cast<GrepOutputItem*>(stditem);
    if( !grepitem )
        return;

    KTextEditor::Cursor range( grepitem->m_lineNumber.toInt() - 1, 0 );
    m_part->core()->documentController()->openDocument( KUrl(grepitem->m_fileName), range );
}

QModelIndex GrepOutputModel::nextHighlightIndex( const QModelIndex& currentIndex )
{
    return QModelIndex();
}

QModelIndex GrepOutputModel::previousHighlightIndex( const QModelIndex& currentIndex )
{
    return QModelIndex();
}

void GrepOutputModel::appendOutputs( const QStringList &lines )
{
    foreach( QString line, lines )
    {
        int pos;
        QString filename, linenumber, rest;

        QString str = line;
        if ( (pos = str.indexOf(':')) != -1)
        {
            filename = str.left(pos);
            str.remove( 0, pos+1 );
            if ( ( pos = str.indexOf(':') ) != -1)
            {
                linenumber = str.left(pos);
                str.remove( 0, pos+1 );
                // filename will be displayed only once
                // selecting filename will display line 1 of file,
                // otherwise, line of requested search
                if ( _lastfilename != filename )
                {
                    _lastfilename = filename;
                    appendRow(new GrepOutputItem(filename, "0", filename, true));
                    appendRow(new GrepOutputItem(filename, linenumber, str, false));
                }
                else
                {
                    appendRow(new GrepOutputItem(filename, linenumber, str, false));
                }
//                 maybeScrollToBottom();
            }
            else
            {
                appendRow( new QStandardItem(line) );
            }
//             m_matchCount++;
        }
    }
}

void GrepOutputModel::appendErrors( const QStringList &lines )
{
    foreach( QString line, lines )
        appendRow( new QStandardItem(line) );
}

void GrepOutputModel::slotCompleted()
{
    appendRow( new QStandardItem( "Completed" ) );
}
void GrepOutputModel::slotFailed()
{
    appendRow( new QStandardItem( "Failed" ) );
}

#include "grepoutputmodel.moc"

// kate: space-indent on; indent-width 4; tab-width: 4; replace-tabs on; auto-insert-doxygen on
