/***************************************************************************
 *   Copyright 2007 Alexander Dymo <adymo@kdevelop.org>                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "projectselectionpage.h"

#include <QDir>

#include "ui_projectselectionpage.h"
#include "projecttemplatesmodel.h"

ProjectSelectionPage::ProjectSelectionPage(ProjectTemplatesModel *templatesModel, QWidget *parent)
    :QWidget(parent), m_templatesModel(templatesModel)
{
    ui = new Ui::ProjectSelectionPage();
    ui->setupUi(this);
    ui->templateView->setModel(templatesModel);

    ui->locationUrl->setPath(QDir::homePath());
    ui->locationUrl->setMode(KFile::Directory | KFile::ExistingOnly | KFile::LocalOnly );
}

ProjectSelectionPage::~ProjectSelectionPage()
{
    delete ui;
}

QString ProjectSelectionPage::selectedTemplate()
{
    QStandardItem *item = m_templatesModel->itemFromIndex(ui->templateView->currentIndex());
    if (item)
        return item->data().toString();
    else
        return "";
}

QString ProjectSelectionPage::location()
{
    KUrl url = ui->locationUrl->url();
    url.addPath(ui->appNameEdit->text());
    return url.toLocalFile();
}

QString ProjectSelectionPage::appName()
{
    return ui->appNameEdit->text();
}

// kate: indent-width 4; replace-tabs on; tab-width 4; space-indent on;
