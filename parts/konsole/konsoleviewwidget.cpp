/***************************************************************************
 *   Copyright (C) 2003 by KDevelop Authors                                *
 *   kdevelop-devel@kde.org                                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qlayout.h>
#include <qframe.h>
#include <qdir.h>

#include <kparts/part.h>
#include <klibloader.h>
#include <kurl.h>
#include <kdebug.h>

#include "kdevcore.h"
#include "kdevproject.h"
#include "konsoleviewpart.h"
#include "kdevpartcontroller.h"

#include "konsoleviewpart.h"
#include "konsoleviewwidget.h"

KonsoleViewWidget::KonsoleViewWidget(KonsoleViewPart *part)
 : QWidget(0, "konsole widget"), part(0), owner( part )
{
    connect(part->partController(), SIGNAL(activePartChanged(KParts::Part*)), this, SLOT(activePartChanged(KParts::Part*)));
    vbox = new QVBoxLayout(this);

    activate();
}

KonsoleViewWidget::~KonsoleViewWidget()
{
}

void KonsoleViewWidget::activate()
{
    if (part)
        return;

    KLibFactory *factory = KLibLoader::self()->factory("libkonsolepart");
    if (!factory)
        return;

    part = (KParts::ReadOnlyPart *) factory->create(this);
    if (!part)
        return;

    part->widget()->setFocusPolicy(QWidget::WheelFocus);
    setFocusProxy(part->widget());
    part->widget()->setFocus();

    if (part->widget()->inherits("QFrame"))
        ((QFrame*)part->widget())->setFrameStyle(QFrame::Panel|QFrame::Sunken);

    vbox->addWidget(part->widget());

//    this->activePartChanged( owner->partController()->activePart() );
    part->widget()->show();

    connect(part, SIGNAL(destroyed()), this, SLOT(partDestroyed()));
}


void KonsoleViewWidget::activePartChanged(KParts::Part *activatedPart)
{
    kdDebug() << "KonsoleViewWidget::activePartChanged()" << endl;
    KParts::ReadOnlyPart *ro_part = dynamic_cast<KParts::ReadOnlyPart*>(activatedPart);

    if (ro_part && !ro_part->url().isLocalFile())
    {
        kdDebug() << " ===> Hmmm ... part is null or not local ... :-/" << endl;
        return;
    }
    QString dir;
    if (ro_part)
        dir = ro_part->url().directory();
    else if (owner->project())
        dir = owner->project()->projectDirectory();

    kdDebug() << " ===> Changing dir to " << dir << endl;
    if (dir.isEmpty())
        return;
    setDirectory( KURL(dir) );
}

void KonsoleViewWidget::setDirectory(const KURL &dirUrl)
{
    kdDebug() << "KonsoleViewWidget::setDirectory()" << endl;
    kdDebug() << " ===> part is " << (long)part << endl;

    if (part && dirUrl != part->url())
    {
        kdDebug() << " ===> Changing dirUrl.path() == " << dirUrl.path() << endl;
        kdDebug() << " ===> Changing part->url.path() == " << part->url().path() << endl;

        part->openURL( dirUrl );
    }
}

void KonsoleViewWidget::partDestroyed()
{
    part = 0;
	activate();
}


#include "konsoleviewwidget.moc"
