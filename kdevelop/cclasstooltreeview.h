/***************************************************************************
               cclasstooltreeview.h  -  description

                             -------------------

    begin                : Fri May 23 1999

    copyright            : (C) 1999 by Jonas Nordin
    email                : jonas.nordin@syncom.se

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef _CCLASSTOOLTREEVIEW_H_INCLUDED
#define _CCLASSTOOLTREEVIEW_H_INCLUDED

#include "ctreeview.h"

//#include "cproject.h"
class  CProject; 

class CClassToolTreeView : public CTreeView
{
  Q_OBJECT

public: // Constructor and Destructor

  CClassToolTreeView(QWidget* parent = 0,const char* name = 0);
  ~CClassToolTreeView() {}

protected: // Implementations of virtual methods.
 
  /** Initialize popupmenus. */
  void initPopups();
 
  /** Get the current popupmenu. */
  KPopupMenu *getCurrentPopup();                                               

  /** Refresh this view using the current project. */
  void refresh( CProject *proj ) {}

private: // Popupmenus
 
  /** Popupmenu for classes. */
  KPopupMenu classPopup;
 
  /** Popupmenu for methods. */
  KPopupMenu methodPopup;
 
  /** Popupmenu for attributes. */
  KPopupMenu attributePopup;                                                   

protected slots:
  void slotViewDefinition();
  void slotViewDeclaration();

signals: // Signals

  /** This signal is emitted when a user wants to view a declaration. */
  void signalViewDeclaration( const char *, const char *, THType, THType );

  /** This signal is emitted when a user wants to view a definition. */
  void signalViewDefinition( const char *, const char *, THType, THType );

};

#endif
