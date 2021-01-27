/*
 * This file is part of KDevelop
 *
 * Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>
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

#ifndef KDEVPLATFORM_PLUGIN_QUICKOPENPLUGIN_H
#define KDEVPLATFORM_PLUGIN_QUICKOPENPLUGIN_H

#include <QVariant>
#include <QPointer>

#include <interfaces/iplugin.h>

#include <language/interfaces/iquickopen.h>
#include <language/interfaces/quickopendataprovider.h>

class QAction;

namespace KTextEditor {
class Cursor;
}

class QuickOpenModel;
class QuickOpenWidget;
class QuickOpenLineEdit;

class QuickOpenPlugin
    : public KDevelop::IPlugin
    , public KDevelop::IQuickOpen
{
    Q_OBJECT
    Q_INTERFACES(KDevelop::IQuickOpen)
public:
    explicit QuickOpenPlugin(QObject* parent, const QVariantList& = QVariantList());
    ~QuickOpenPlugin() override;

    static QuickOpenPlugin* self();

    // KDevelop::Plugin methods
    void unload() override;

    KDevelop::ContextMenuExtension contextMenuExtension(KDevelop::Context* context, QWidget* parent) override;

    enum ModelTypes {
        Files = 1,
        Functions = 2,
        Classes = 4,
        OpenFiles = 8,
        All = Files + Functions + Classes + OpenFiles
    };

    /**
     * Shows the quickopen dialog with the specified Model-types
     * @param modes A combination of ModelTypes
     * */
    void showQuickOpen(ModelTypes modes = All);
    void showQuickOpen(const QStringList& items) override;

    void registerProvider(const QStringList& scope, const QStringList& type, KDevelop::QuickOpenDataProviderBase* provider) override;

    bool removeProvider(KDevelop::QuickOpenDataProviderBase* provider) override;

    QSet<KDevelop::IndexedString> fileSet() const override;

    //Frees the model by closing active quickopen dialoags, and retuns whether successful.
    bool freeModel();

    void createActionsForMainWindow(Sublime::MainWindow* window, QString& xmlFile, KActionCollection& actions) override;

    QuickOpenLineEdit* createQuickOpenLineWidget();

    QLineEdit* createQuickOpenLine(const QStringList& scopes, const QStringList& type, QuickOpenType kind) override;

public Q_SLOTS:
    void quickOpen();
    void quickOpenFile();
    void quickOpenFunction();
    void quickOpenClass();
    void quickOpenDeclaration();
    void quickOpenOpenFile();
    void quickOpenDefinition();
    void quickOpenNavigateFunctions();
    void quickOpenDocumentation();
    void quickOpenActions();

    void previousFunction();
    void nextFunction();
private Q_SLOTS:
    void storeScopes(const QStringList&);
    void storeItems(const QStringList&);
private:
    friend class QuickOpenLineEdit;
    friend class StandardQuickOpenWidgetCreator;
    QuickOpenLineEdit* quickOpenLine(const QString& name = QStringLiteral( "Quickopen" ));

    enum FunctionJumpDirection { NextFunction, PreviousFunction };
    void jumpToNearestFunction(FunctionJumpDirection direction);

    QPair<QUrl, KTextEditor::Cursor> specialObjectJumpPosition() const;
    QWidget* specialObjectNavigationWidget() const;
    bool jumpToSpecialObject();
    void showQuickOpenWidget(const QStringList& items, const QStringList& scopes, bool preselectText);

    QuickOpenModel* m_model;
    class ProjectFileDataProvider* m_projectFileData;
    class ProjectItemDataProvider* m_projectItemData;
    class OpenFilesDataProvider* m_openFilesData;
    class DocumentationQuickOpenProvider* m_documentationItemData;
    class ActionsQuickOpenProvider* m_actionsItemData;
    QStringList lastUsedScopes;
    QStringList lastUsedItems;

    //We can only have one widget at a time, because we manipulate the model.
    QPointer<QObject> m_currentWidgetHandler;
    QAction* m_quickOpenDeclaration;
    QAction* m_quickOpenDefinition;
};

class QuickOpenWidgetCreator;

class QuickOpenLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit QuickOpenLineEdit(QuickOpenWidgetCreator* creator);
    ~QuickOpenLineEdit() override;

    bool insideThis(QObject* object);
    void showWithWidget(QuickOpenWidget* widget);

private Q_SLOTS:
    void activate();
    void deactivate();
    void checkFocus();
    void widgetDestroyed(QObject*);
private:
    void focusInEvent(QFocusEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    void hideEvent(QHideEvent*) override;

    QPointer<QuickOpenWidget> m_widget;
    bool m_forceUpdate;
    int m_newlyOpened;
    QuickOpenWidgetCreator* m_widgetCreator;
};

#endif // KDEVPLATFORM_PLUGIN_QUICKOPENPLUGIN_H
