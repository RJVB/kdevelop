/***************************************************************************
 *   Copyright (C) 2001 by Bernd Gehrmann                                  *
 *   bernd@kdevelop.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef _APPWIZARDPART_H_
#define _APPWIZARDPART_H_

#include <qguardedptr.h>
#include "kdevplugin.h"
#include <qstring.h>
#include <qstringlist.h>

class AppWizardDialog;


class AppWizardPart : public KDevPlugin
{
    Q_OBJECT

public:
    AppWizardPart( QObject *parent, const char *name, const QStringList & );
    ~AppWizardPart();

private slots:
    void slotNewProject();
    void slotImportProject();
    void slotCommandFinished(const QString &command);

private:
    //! opens all files specified in the template-information file with "ShowFilesAfterGeneration="
    void openSpecifiedFiles();
    AppWizardDialog *m_dialog;
    QStringList m_openFilesAfterGeneration;
    QString m_creationCommand;
    QString m_projectFileName;
    QString m_projectLocation;
};

#endif
