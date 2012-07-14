/*
 * This file is part of KDevelop
 * Copyright 2012 Miha Čančula <miha@noughmad.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
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

#ifndef TESTNAMEPAGE_H
#define TESTNAMEPAGE_H

#include <QWidget>
#include <QMetaType>
#include <QHash>
#include <KUrl>
#include <KUrlRequester>

class QFormLayout;

namespace Ui
{
class TestOutputPage;
}

class TestOutputPage : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(UrlHash fileUrls READ fileUrls)

public:
    typedef QHash<QString, KUrl> UrlHash;
    
    TestOutputPage (QWidget* parent);
    virtual ~TestOutputPage();

    UrlHash fileUrls() const;
    void setFileUrls(const TestOutputPage::UrlHash& urls, const QHash< QString, QString >& labels);

private:
    QFormLayout* m_layout;
    QHash<QString,KUrlRequester*> m_requesters;
};

Q_DECLARE_METATYPE(TestOutputPage::UrlHash)

#endif // TESTNAMEPAGE_H
