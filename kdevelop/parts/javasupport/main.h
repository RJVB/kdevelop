/***************************************************************************
 *   Copyright (C) 2000 by Bernd Gehrmann                                  *
 *   bernd@kdevelop.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef _JAVASUPPORTFACTORY_H_
#define _JAVASUPPORTFACTORY_H_

#include <klibloader.h>


class JavaSupportFactory : public KLibFactory
{
    Q_OBJECT

public:
    JavaSupportFactory( QObject *parent=0, const char *name=0 );
    ~JavaSupportFactory();

    virtual QObject* create(QObject *parent, const char *name,
                            const char *classname, const QStringList &args);
    static KInstance *instance();

private:
    static KInstance *s_instance;
};

#endif
