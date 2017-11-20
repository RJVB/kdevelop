/*  This file is part of KDevelop
    Copyright 2009 Aleix Pol <aleixpol@kde.org>
    Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>
    Copyright 2010 Benjamin Port <port.benjamin@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "qthelpdocumentation.h"

#include <QLabel>
#include <QUrl>
#include <QTreeView>
#include <QHelpContentModel>
#include <QHeaderView>
#include <QMenu>
#include <QTemporaryFile>
#include <QRegularExpression>

#ifdef USE_QTEXTBROWSER
#include <QDesktopServices>
#include <QApplication>
#include <QClipboard>
#endif
#include <QTextBrowser>

#include <KLocalizedString>

#include <interfaces/icore.h>
#include <interfaces/idocumentationcontroller.h>
#include <documentation/standarddocumentationview.h>
#include "qthelpnetwork.h"
#include "qthelpproviderabstract.h"
#include "qthelpexternalassistant.h"

using namespace KDevelop;

namespace {
#if QT_VERSION >= 0x050500
int indexOf(const QString& str, const QRegularExpression& re, int from, QRegularExpressionMatch* rmatch)
{
    return str.indexOf(re, from, rmatch);
}

int lastIndexOf(const QString& str, const QRegularExpression& re, int from, QRegularExpressionMatch* rmatch)
{
    return str.lastIndexOf(re, from, rmatch);
}
#else
int indexOf(const QString& str, const QRegularExpression& re, int from, QRegularExpressionMatch* rmatch)
{
    if (!re.isValid()) {
        qCWarning(QTHELP) << "QString::indexOf: invalid QRegularExpression object";
        return -1;
    }

    QRegularExpressionMatch match = re.match(str, from);
    if (match.hasMatch()) {
        const int ret = match.capturedStart();
        if (rmatch)
            *rmatch = qMove(match);
        return ret;
    }

    return -1;
}

int lastIndexOf(const QString &str, const QRegularExpression &re, int from, QRegularExpressionMatch *rmatch)
{
    if (!re.isValid()) {
        qCWarning(QTHELP) << "QString::lastIndexOf: invalid QRegularExpression object";
        return -1;
    }

    int endpos = (from < 0) ? (str.size() + from + 1) : (from + 1);
    QRegularExpressionMatchIterator iterator = re.globalMatch(str);
    int lastIndex = -1;
    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        int start = match.capturedStart();
        if (start < endpos) {
            lastIndex = start;
            if (rmatch)
                *rmatch = qMove(match);
        } else {
            break;
        }
    }

    return lastIndex;
}
#endif

}

class QtHelpDocumentationView : public StandardDocumentationView
{
public:
    explicit QtHelpDocumentationView(DocumentationFindWidget* findWidget, QtHelpDocumentation* owner, QWidget* parent = nullptr )
        : StandardDocumentationView(findWidget, parent)
        , m_owner(owner)
        , lastAnchor(QString())
    {
        m_browser = qobject_cast<const QTextBrowser*>(view());
    }
    bool loadResource(int type, QUrl& url, QVariant& content) override;
    QMenu* createStandardContextMenu(const QPoint& pos = QPoint()) override;

#ifdef USE_QTEXTBROWSER
    // features that could certainly be implemented for QtWebKit and/or QtWebEngine
    // but that would require exporting the browser type and the accompanying
    // headerfiles.
    // I think it would be better to make QTextBrowser the main (or even only)
    // backend of the embedded documentation browser (since it requires no additional
    // dependencies at all), and concentrate on adding support for using a more
    // capable external documentation viewer.
    bool hasAnchorAt(const QPoint& pos)
    {
        if (!m_browser) {
            // !defined(USE_QTEXTBROWSER)
            return false;
        }

        lastAnchor = m_browser->anchorAt(pos);
        const QUrl last = QUrl(lastAnchor);
        if (lastAnchor.isEmpty() || !last.isValid())
            return false;

        lastAnchor = m_browser->source().resolved(last).toString();
        if (lastAnchor.at(0) == QLatin1Char('#')) {
            QString src = m_browser->source().toString();
            int hsh = src.indexOf(QLatin1Char('#'));
            lastAnchor = (hsh >= 0 ? src.left(hsh) : src) + lastAnchor;
        }
        return true;
    }

    void openLink()
    {
        if (!lastAnchor.isEmpty()) {
            m_owner->jumpedTo(QUrl(lastAnchor));
            lastAnchor.clear();
        }
    }

    QAction* addCopyLinkAction(QMenu* menu, const QString& title, const QUrl& link)
    {
        QAction* copyLinkAction = menu->addAction(title);
        copyLinkAction->setData(link.toString());
        connect(copyLinkAction, &QAction::triggered, this, [copyLinkAction] () {
                QApplication::clipboard()->setText(copyLinkAction->data().toString()); } );
        return copyLinkAction;
    }

    QAction* addExternalViewerAction(QMenu* menu, const QString& title, const QUrl& link)
    {
        QAction* externalOpenAction = nullptr;
// this is how opening links in an external viewer (e.g. Qt's Assistant) could be implemented
        if (QtHelpExternalAssistant::self()->hasExternalViewer()) {
            externalOpenAction = menu->addAction(title);
            externalOpenAction->setData(link.toString());
            connect(externalOpenAction, &QAction::triggered, this, [this, externalOpenAction] () {
                    QtHelpExternalAssistant::openUrl(QUrl(externalOpenAction->data().toString()));
                } );
        }
        return externalOpenAction;
    }
#endif

    QtHelpDocumentation* m_owner;
    QString lastAnchor;
    const QTextBrowser* m_browser;
};

QtHelpProviderAbstract* QtHelpDocumentation::s_provider=nullptr;

QtHelpDocumentation::QtHelpDocumentation(const QString& name, const QMap<QString, QUrl>& info)
    : m_provider(s_provider), m_name(name), m_info(info), m_current(info.constBegin()), lastView(nullptr)
{}

QtHelpDocumentation::QtHelpDocumentation(const QString& name, const QMap<QString, QUrl>& info, const QString& key)
    : m_provider(s_provider), m_name(name), m_info(info), m_current(m_info.find(key)), lastView(nullptr)
{ Q_ASSERT(m_current!=m_info.constEnd()); }

QtHelpDocumentation::~QtHelpDocumentation()
{
#ifndef USE_QTEXTBROWSER
    delete m_lastStyleSheet.data();
#endif
}

QString QtHelpDocumentation::description() const
{
    const QUrl url(m_current.value());
    //Extract a short description from the html data
    const QString dataString = QString::fromLatin1(m_provider->engine()->fileData(url)); ///@todo encoding

    const QString fragment = url.fragment();
    const QString p = QStringLiteral("((\\\")|(\\\'))");
    const QString optionalSpace = QStringLiteral(" *");
    const QString exp = QString(QStringLiteral("< a name = ") + p + fragment + p + QStringLiteral(" > < / a >")).replace(' ', optionalSpace);

    const QRegularExpression findFragment(exp);
    QRegularExpressionMatch findFragmentMatch;
    int pos = indexOf(dataString, findFragment, 0, &findFragmentMatch);

    if(fragment.isEmpty()) {
        pos = 0;
    } else {

        //Check if there is a title opening-tag right before the fragment, and if yes add it, so we have a nicely formatted caption
        const QString titleRegExp = QStringLiteral("< h\\d class = \".*\" >").replace(' ', optionalSpace);
        const QRegularExpression findTitle(titleRegExp);
        const QRegularExpressionMatch match = findTitle.match(dataString, pos);
        const int titleStart = match.capturedStart();
        const int titleEnd = titleStart + match.capturedEnd();
        if(titleStart != -1) {
            const QStringRef between = dataString.midRef(titleEnd, pos-titleEnd).trimmed();
            if(between.isEmpty())
                pos = titleStart;
        }
    }

    if(pos != -1) {
        const QString exp = QString(QStringLiteral("< a name = ") + p + QStringLiteral("((\\S)*)") + p + QStringLiteral(" > < / a >")).replace(' ', optionalSpace);
        const QRegularExpression nextFragmentExpression(exp);
        int endPos = dataString.indexOf(nextFragmentExpression, pos+(fragment.size() ? findFragmentMatch.capturedLength() : 0));
        if(endPos == -1) {
            endPos = dataString.size();
        }

        {
            //Find the end of the last paragraph or newline, so we don't add prefixes of the following fragment
            const QString newLineRegExp = QStringLiteral ("< br / > | < / p >").replace(' ', optionalSpace);
            const QRegularExpression lastNewLine(newLineRegExp);
            QRegularExpressionMatch match;
            const int newEnd = lastIndexOf(dataString, lastNewLine, endPos, &match);
            if(match.isValid() && newEnd > pos)
                endPos = newEnd + match.capturedLength();
        }

        {
            //Find the title, and start from there
            const QString titleRegExp = QStringLiteral("< h\\d class = \"title\" >").replace(' ', optionalSpace);
            const QRegularExpression findTitle(titleRegExp);
            const QRegularExpressionMatch match = findTitle.match(dataString);
            if (match.isValid())
                pos = qBound(pos, match.capturedStart(), endPos);
        }


        QString thisFragment = dataString.mid(pos, endPos - pos);

        {
            //Completely remove the first large header found, since we don't need a header
            const QString headerRegExp = QStringLiteral("< h\\d.*>.*?< / h\\d >").replace(' ', optionalSpace);
            const QRegularExpression findHeader(headerRegExp);
            const QRegularExpressionMatch match = findHeader.match(thisFragment);
            if(match.isValid()) {
                thisFragment.remove(match.capturedStart(), match.capturedLength());
            }
        }

        {
            //Replace all gigantic header-font sizes with <big>
            {
                const QString sizeRegExp = QStringLiteral("< h\\d ").replace(' ', optionalSpace);
                const QRegularExpression findSize(sizeRegExp);
                thisFragment.replace(findSize, QStringLiteral("<big "));
            }
            {
                const QString sizeCloseRegExp = QStringLiteral("< / h\\d >").replace(' ', optionalSpace);
                const QRegularExpression closeSize(sizeCloseRegExp);
                thisFragment.replace(closeSize, QStringLiteral("</big><br />"));
            }
        }

        {
            //Replace paragraphs by newlines
            const QString begin = QStringLiteral("< p >").replace(' ', optionalSpace);
            const QRegularExpression findBegin(begin);
            thisFragment.replace(findBegin, {});

            const QString end = QStringLiteral("< /p >").replace(' ', optionalSpace);
            const QRegularExpression findEnd(end);
            thisFragment.replace(findEnd, QStringLiteral("<br />"));
        }

        {
            //Remove links, because they won't work
            const QString link = QString(QStringLiteral("< a href = ") + p + QStringLiteral(".*?") + p).replace(' ', optionalSpace);
            const QRegularExpression exp(link, QRegularExpression::CaseInsensitiveOption);
            thisFragment.replace(exp, QStringLiteral("<a "));
        }

        return thisFragment;
    }

    return QStringList(m_info.keys()).join(QStringLiteral(", "));
}

void QtHelpDocumentation::setUserStyleSheet(StandardDocumentationView* view, const QUrl& url)
{
#ifdef USE_QTEXTBROWSER
    QString css;
    QTextStream ts(&css);
#else
    QTemporaryFile* file = new QTemporaryFile(view);
    file->open();

    QTextStream ts(file);
#endif

    ts << "html { background: white !important; }\n";
    if (url.scheme() == QLatin1String("qthelp") && url.host().startsWith(QLatin1String("com.trolltech.qt."))) {
       ts << ".content .toc + .title + p { clear:left; }\n"
          << "#qtdocheader .qtref { position: absolute !important; top: 5px !important; right: 0 !important; }\n";
    }
#ifdef USE_QTEXTBROWSER
    view->setHtml(css);
#else
    file->close();
    view->setOverrideCss(QUrl::fromLocalFile(file->fileName()));

    delete m_lastStyleSheet.data();
    m_lastStyleSheet = file;
#endif
}

// adapted from Qt's Assistant
bool QtHelpDocumentationView::loadResource(int type, QUrl& url, QVariant& content)
{
    if (type < 4 && StandardDocumentationView::isUrlSchemeSupported(url)) {
        QByteArray ba;
        const auto resolvedUrl = m_owner->m_provider->engine()->findFile(url);
        if (resolvedUrl.isEmpty() || !StandardDocumentationView::isUrlSchemeSupported(resolvedUrl)) {
            qCWarning(QTHELP) << "loadResource ignoring type" << type << "url" << url << "resolved=" << resolvedUrl;
            return false;
        }
        qCDebug(QTHELP) << "loadResource type" << type << "url" << url << "resolved=" << resolvedUrl;
        // update the return URL
        url = resolvedUrl;
        ba = m_owner->m_provider->engine()->fileData(url);
        bool ret = true;
        if (url.toString().endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
            QImage image;
            image.loadFromData(ba, "svg");
            if (!image.isNull()) {
                content = ba;
            } else {
                ret = false;
            }
        }
        if (!content.isValid()) {
            content = ba;
        }
        return content.isValid();
    }
    return false;
}

QMenu* QtHelpDocumentationView::createStandardContextMenu(const QPoint& pos)
{
#ifdef USE_QTEXTBROWSER
    // we roll our own, inspired by Qt Assistant's context menu
    QMenu* menu = new QMenu(QString(), this);
    QAction* copyLinkAction = nullptr;
    if (hasAnchorAt(pos)) {
        // hovering over a link?
        QUrl link = QUrl(lastAnchor);
        menu->addAction(i18n("&Open link"), this, &QtHelpDocumentationView::openLink);

        if (!link.isEmpty() && link.isValid()) {
            copyLinkAction = addCopyLinkAction(menu, i18n("Copy &link location"), link);
            addExternalViewerAction(menu, i18n("Open link in &external viewer"), link);
        }
    } else if (!m_browser->textCursor().selectedText().isEmpty()) {
        menu->addAction(i18n("&Copy"), m_browser, &QTextBrowser::copy);
    }
    if (!copyLinkAction) {
        copyLinkAction = addCopyLinkAction(menu, i18n("Copy document &link"), m_browser->source());
        addExternalViewerAction(menu, i18n("Open document in &external viewer"), m_browser->source());
    }
    menu->addAction(i18n("&Reload"), m_browser, SLOT(reload()));
    return menu;
#else
    return StandardDocumentationView::createStandardContextMenu(pos);
#endif
}


QWidget* QtHelpDocumentation::documentationWidget(DocumentationFindWidget* findWidget, QWidget* parent)
{
    if(m_info.isEmpty()) { //QtHelp sometimes has empty info maps. e.g. availableaudioeffects i 4.5.2
        return new QLabel(i18n("Could not find any documentation for '%1'", m_name), parent);
    } else {
        QtHelpDocumentationView* view = new QtHelpDocumentationView(findWidget, this, parent);
        view->initZoom(m_provider->name());
        view->setDelegateLinks(true);
        view->setNetworkAccessManager(m_provider->networkAccess());
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &StandardDocumentationView::customContextMenuRequested, this, &QtHelpDocumentation::viewContextMenuRequested);

        setUserStyleSheet(view, m_current.value());
#ifdef USE_QTEXTBROWSER
        const auto url = m_current.value();
        if (view->isUrlSchemeSupported(url)) {
            view->load(url, m_provider->engine()->fileData(url));
        } else {
            // external link
            qCWarning(QTHELP) << "Opening url" << url << "in the registered web browser";
            QDesktopServices::openUrl(url);
        }
#else
        view->load(m_current.value());
#endif
        lastView = view;
        // jumpedTo can only be called safely now.
        QObject::connect(view, &StandardDocumentationView::linkClicked, this, &QtHelpDocumentation::jumpedTo);
        return view;
    }
}

void QtHelpDocumentation::viewContextMenuRequested(const QPoint& pos)
{
    StandardDocumentationView* view = qobject_cast<StandardDocumentationView*>(sender());
    if (!view)
        return;

    auto menu = view->createStandardContextMenu(pos);

    if (m_info.count() > 1) {
        if (!menu->isEmpty()) {
            menu->addSeparator();
        }

        QActionGroup* actionGroup = new QActionGroup(menu);
        foreach(const QString& name, m_info.keys()) {
            QtHelpAlternativeLink* act=new QtHelpAlternativeLink(name, this, actionGroup);
            act->setCheckable(true);
            act->setChecked(name==m_current.key());
            menu->addAction(act);
        }
    }

    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->exec(view->mapToGlobal(pos));
}


void QtHelpDocumentation::jumpedTo(const QUrl& newUrl)
{
    Q_ASSERT(lastView);
    m_provider->jumpedTo(newUrl);
    setUserStyleSheet(lastView, newUrl);
#ifdef USE_QTEXTBROWSER
    if (lastView->isUrlSchemeSupported(newUrl)) {
        QByteArray content = m_provider->engine()->fileData(newUrl);
        if (!content.isEmpty()) {
            lastView->load(newUrl, content);
            return;
        } else {
            qCWarning(QTHELP) << "cannot determine the content of the new url" << newUrl;
        }
    } else {
        // external link, use the user's webbrowser
        qCWarning(QTHELP) << "Opening new url" << newUrl << "in the registered web browser";
        QDesktopServices::openUrl(newUrl);
    }
    // restore the current "internal" doc view. If we fail to do this
    // the next link we click that isn't fully specified will be completed
    // using <newUrl>.
    lastView->restore();
#else
    lastView->load(newUrl);
#endif
}

IDocumentationProvider* QtHelpDocumentation::provider() const
{
    return m_provider;
}

bool QtHelpDocumentation::viewInExternalBrowser()
{
    if (QtHelpExternalAssistant::self()->useExternalViewer()) {
        return QtHelpExternalAssistant::openUrl(m_current.value());
    }
    return false;
}

QtHelpAlternativeLink::QtHelpAlternativeLink(const QString& name, const QtHelpDocumentation* doc, QObject* parent)
    : QAction(name, parent), mDoc(doc), mName(name)
{
    connect(this, &QtHelpAlternativeLink::triggered, this, &QtHelpAlternativeLink::showUrl);
}

void QtHelpAlternativeLink::showUrl()
{
    IDocumentation::Ptr newDoc(new QtHelpDocumentation(mName, mDoc->info(), mName));
    // probe, not to be committed.
    qInfo() << Q_FUNC_INFO << "name" << mName << ":" << mDoc->info();
    ICore::self()->documentationController()->showDocumentation(newDoc);
}

HomeDocumentation::HomeDocumentation() : m_provider(QtHelpDocumentation::s_provider)
{
}

QWidget* HomeDocumentation::documentationWidget(DocumentationFindWidget*, QWidget* parent)
{
    QTreeView* w=new QTreeView(parent);
    w->header()->setVisible(false);
    w->setModel(m_provider->engine()->contentModel());

    connect(w, &QTreeView::clicked, this, &HomeDocumentation::clicked);
    return w;
}

void HomeDocumentation::clicked(const QModelIndex& idx)
{
    QHelpContentModel* model = m_provider->engine()->contentModel();
    QHelpContentItem* it=model->contentItemAt(idx);
    QMap<QString, QUrl> info;
    info.insert(it->title(), it->url());

    IDocumentation::Ptr newDoc(new QtHelpDocumentation(it->title(), info));
    ICore::self()->documentationController()->showDocumentation(newDoc);
}

QString HomeDocumentation::name() const
{
    return i18n("QtHelp Home Page");
}

IDocumentationProvider* HomeDocumentation::provider() const
{
    return m_provider;
}
