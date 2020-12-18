/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwebenginepage.h"
#include "qwebenginepage_p.h"

#include "qwebenginedownloadrequest_p.h"
#include "authentication_dialog_controller.h"
#include "profile_adapter.h"
#include "color_chooser_controller.h"
#include "find_text_helper.h"
#include "file_picker_controller.h"
#include "javascript_dialog_controller.h"
#include "qwebenginecertificateerror.h"
#include "qwebenginefindtextresult.h"
#include "qwebenginefullscreenrequest.h"
#include "qwebenginehistory.h"
#include "qwebenginehistory_p.h"
#include "qwebenginenavigationrequest.h"
#include "qwebenginenewwindowrequest.h"
#include "qwebenginenotification.h"
#include "qwebengineprofile.h"
#include "qwebengineprofile_p.h"
#include "qwebenginequotarequest.h"
#include "qwebengineregisterprotocolhandlerrequest.h"
#include "qwebenginescriptcollection_p.h"
#include "qwebenginesettings.h"
#include "user_notification_controller.h"
#include "render_widget_host_view_qt_delegate.h"
#include "web_contents_adapter.h"
#include "web_engine_settings.h"
#include "qwebenginescript.h"
#include "render_view_context_menu_qt.h"
#include "render_widget_host_view_qt_delegate_client.h"
#include <QAction>
#include <QGuiApplication>
#include <QAuthenticator>
#include <QClipboard>
#include <QKeyEvent>
#include <QIcon>
#include <QLoggingCategory>
#include <QMimeData>
#include <QTimer>
#include <QUrl>


QT_BEGIN_NAMESPACE

using namespace QtWebEngineCore;

static const int MaxTooltipLength = 1024;

// add temporary dummy code to cover the case when page is loading and there is no view
class DummyDelegate : public QObject, public QtWebEngineCore::RenderWidgetHostViewQtDelegate
{
public:
    DummyDelegate(RenderWidgetHostViewQtDelegateClient *client) : m_delegateClient(client) {};
    ~DummyDelegate() = default;
    void initAsPopup(const QRect &) override { Q_UNREACHABLE(); }
    QRectF viewGeometry() const override { return QRectF(m_pos, m_size); }
    void setKeyboardFocus() override { }
    bool hasKeyboardFocus() override { return false; }
    void lockMouse() override { Q_UNREACHABLE(); }
    void unlockMouse() override { Q_UNREACHABLE(); }
    void show() override { m_delegateClient->notifyShown(); }
    void hide() override { m_delegateClient->notifyHidden(); }
    bool isVisible() const override { Q_UNREACHABLE(); }
    QWindow *window() const override { return nullptr; }
    void updateCursor(const QCursor &cursor) override
    { /*setCursor(cursor);*/
    }
    void resize(int width, int height) override
    {
        m_size = QSize(width, height);
        m_delegateClient->visualPropertiesChanged();
    }
    void move(const QPoint &) override { Q_UNREACHABLE(); }
    void inputMethodStateChanged(bool, bool) override { }
    void setInputMethodHints(Qt::InputMethodHints) override { }
    void setClearColor(const QColor &) override { }
    void adapterClientChanged(WebContentsAdapterClient *client) override { }
    bool copySurface(const QRect &rect, const QSize &size, QImage &image)
    {
        Q_UNREACHABLE();
        return false;
    }
    QRect windowGeometry() const override { return QRect(m_pos, m_size); }
    bool forwardEvent(QEvent *ev) { return m_delegateClient->forwardEvent(ev); }

private:
    RenderWidgetHostViewQtDelegateClient *m_delegateClient;
    QPoint m_pos;
    QSize m_size;
};

static QWebEnginePage::WebWindowType toWindowType(WebContentsAdapterClient::WindowOpenDisposition disposition)
{
    switch (disposition) {
    case WebContentsAdapterClient::NewForegroundTabDisposition:
        return QWebEnginePage::WebBrowserTab;
    case WebContentsAdapterClient::NewBackgroundTabDisposition:
        return QWebEnginePage::WebBrowserBackgroundTab;
    case WebContentsAdapterClient::NewPopupDisposition:
        return QWebEnginePage::WebDialog;
    case WebContentsAdapterClient::NewWindowDisposition:
        return QWebEnginePage::WebBrowserWindow;
    default:
        Q_UNREACHABLE();
    }
}

static QWebEngineNewWindowRequest::DestinationType toDestinationType(WebContentsAdapterClient::WindowOpenDisposition disposition)
{
    switch (disposition) {
    case WebContentsAdapterClient::NewForegroundTabDisposition:
        return QWebEngineNewWindowRequest::InNewTab;
    case WebContentsAdapterClient::NewBackgroundTabDisposition:
        return QWebEngineNewWindowRequest::InNewBackgroundTab;
    case WebContentsAdapterClient::NewPopupDisposition:
        return QWebEngineNewWindowRequest::InNewDialog;
    case WebContentsAdapterClient::NewWindowDisposition:
        return QWebEngineNewWindowRequest::InNewWindow;
    default:
        Q_UNREACHABLE();
    }
}

QWebEnginePagePrivate::QWebEnginePagePrivate(QWebEngineProfile *_profile)
    : adapter(QSharedPointer<WebContentsAdapter>::create())
    , history(new QWebEngineHistory(new QWebEngineHistoryPrivate(this)))
    , profile(_profile ? _profile : QWebEngineProfile::defaultProfile())
    , settings(new QWebEngineSettings(profile->settings()))
    , view(0)
    , isLoading(false)
    , scriptCollection(new QWebEngineScriptCollectionPrivate(profileAdapter()->userResourceController(), adapter))
    , m_isBeingAdopted(false)
    , m_backgroundColor(Qt::white)
    , fullscreenMode(false)
    , webChannel(nullptr)
    , webChannelWorldId(QWebEngineScript::MainWorld)
    , defaultAudioMuted(false)
    , defaultZoomFactor(1.0)
{
    memset(actions, 0, sizeof(actions));

    qRegisterMetaType<QWebEngineQuotaRequest>();
    qRegisterMetaType<QWebEngineRegisterProtocolHandlerRequest>();
    qRegisterMetaType<QWebEngineFindTextResult>();

    // See setVisible().
    wasShownTimer.setSingleShot(true);
    QObject::connect(&wasShownTimer, &QTimer::timeout, [this](){
        ensureInitialized();
    });

    profile->d_ptr->addWebContentsAdapterClient(this);
}

QWebEnginePagePrivate::~QWebEnginePagePrivate()
{
    delete history;
    delete settings;
    profile->d_ptr->removeWebContentsAdapterClient(this);
}

RenderWidgetHostViewQtDelegate *QWebEnginePagePrivate::CreateRenderWidgetHostViewQtDelegate(RenderWidgetHostViewQtDelegateClient *client)
{
    // Set the QWebEngineView as the parent for a popup delegate, so that the new popup window
    // responds properly to clicks in case the QWebEngineView is inside a modal QDialog. Setting the
    // parent essentially notifies the OS that the popup window is part of the modal session, and
    // should allow interaction.
    // The new delegate will not be deleted by the parent view though, because we unset the parent
    // when the parent is destroyed. The delegate will be destroyed by Chromium when the popup is
    // dismissed.
    return view ? view->CreateRenderWidgetHostViewQtDelegate(client) : new DummyDelegate(client);
}

void QWebEnginePagePrivate::initializationFinished()
{
    if (m_backgroundColor != Qt::white)
        adapter->setBackgroundColor(m_backgroundColor);
#if QT_CONFIG(webengine_webchannel)
    if (webChannel)
        adapter->setWebChannel(webChannel, webChannelWorldId);
#endif
    if (defaultAudioMuted != adapter->isAudioMuted())
        adapter->setAudioMuted(defaultAudioMuted);
    if (!qFuzzyCompare(adapter->currentZoomFactor(), defaultZoomFactor))
        adapter->setZoomFactor(defaultZoomFactor);
    if (view)
        adapter->setVisible(view->isVisible());

    scriptCollection.d->initializationFinished(adapter);

    m_isBeingAdopted = false;
}

void QWebEnginePagePrivate::titleChanged(const QString &title)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->titleChanged(title);
}

void QWebEnginePagePrivate::urlChanged()
{
    Q_Q(QWebEnginePage);
    QUrl qurl = adapter->activeUrl();
    if (url != qurl) {
        url = qurl;
        Q_EMIT q->urlChanged(qurl);
    }
}

void QWebEnginePagePrivate::iconChanged(const QUrl &url)
{
    Q_Q(QWebEnginePage);
    if (iconUrl == url)
        return;
    iconUrl = url;
    Q_EMIT q->iconUrlChanged(iconUrl);
    Q_EMIT q->iconChanged(iconUrl.isEmpty() ? QIcon() : adapter->icon());
}

void QWebEnginePagePrivate::loadProgressChanged(int progress)
{
    Q_Q(QWebEnginePage);
    QTimer::singleShot(0, q, [q, progress] () { Q_EMIT q->loadProgress(progress); });
}

void QWebEnginePagePrivate::didUpdateTargetURL(const QUrl &hoveredUrl)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->linkHovered(hoveredUrl.toString());
}

void QWebEnginePagePrivate::selectionChanged()
{
    Q_Q(QWebEnginePage);
    QTimer::singleShot(0, q, [this, q]() {
        updateEditActions();
        Q_EMIT q->selectionChanged();
    });
}

void QWebEnginePagePrivate::recentlyAudibleChanged(bool recentlyAudible)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->recentlyAudibleChanged(recentlyAudible);
}

void QWebEnginePagePrivate::renderProcessPidChanged(qint64 pid)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->renderProcessPidChanged(pid);
}

QRectF QWebEnginePagePrivate::viewportRect() const
{
    return view ? view->viewportRect() : QRectF();
}

QColor QWebEnginePagePrivate::backgroundColor() const
{
    return m_backgroundColor;
}

void QWebEnginePagePrivate::loadStarted(const QUrl &provisionalUrl, bool isErrorPage)
{
    Q_UNUSED(provisionalUrl);
    Q_Q(QWebEnginePage);

    if (isErrorPage)
        return;

    isLoading = true;

    QTimer::singleShot(0, q, &QWebEnginePage::loadStarted);
}

void QWebEnginePagePrivate::loadFinished(bool success, const QUrl &url, bool isErrorPage, int errorCode, const QString &errorDescription)
{
    Q_Q(QWebEnginePage);
    Q_UNUSED(url);
    Q_UNUSED(errorCode);
    Q_UNUSED(errorDescription);

    if (isErrorPage)
        return;

    isLoading = false;
    QTimer::singleShot(0, q, [q, success](){
        emit q->loadFinished(success);
    });
}

void QWebEnginePagePrivate::didPrintPageToPdf(const QString &filePath, bool success)
{
    if (view)
        view->didPrintPageToPdf(filePath, success);
}

void QWebEnginePagePrivate::focusContainer()
{
    if (view) {
        view->focusContainer();
    }
}

void QWebEnginePagePrivate::unhandledKeyEvent(QKeyEvent *event)
{
    if (view) {
        view->unhandledKeyEvent(event);
    }
}

QSharedPointer<WebContentsAdapter>
QWebEnginePagePrivate::adoptNewWindow(QSharedPointer<WebContentsAdapter> newWebContents,
                                      WindowOpenDisposition disposition, bool userGesture,
                                      const QRect &initialGeometry, const QUrl &targetUrl)
{
    Q_Q(QWebEnginePage);
    Q_ASSERT(newWebContents);
    QWebEnginePage *newPage = q->createWindow(toWindowType(disposition));
    if (newPage) {
        if (!newWebContents->webContents())
            return newPage->d_func()->adapter; // Reuse existing adapter

        // Mark the new page as being in the process of being adopted, so that a second mouse move event
        // sent by newWebContents->initialize() gets filtered in RenderWidgetHostViewQt::forwardEvent.
        // The first mouse move event is being sent by q->createWindow(). This is necessary because
        // Chromium does not get a mouse move acknowledgment message between the two events, and
        // InputRouterImpl::ProcessMouseAck is not executed, thus all subsequent mouse move events
        // get coalesced together, and don't get processed at all.
        // The mouse move events are actually sent as a result of show() being called on
        // RenderWidgetHostViewQtDelegateWidget, both when creating the window and when initialize is
        // called.
        newPage->d_func()->m_isBeingAdopted = true;

        // Overwrite the new page's WebContents with ours.
        newPage->d_func()->adapter = newWebContents;
        newWebContents->setClient(newPage->d_func());

        if (!initialGeometry.isEmpty())
            emit newPage->geometryChangeRequested(initialGeometry);

        return newWebContents;
    }

    QWebEngineNewWindowRequest request(toDestinationType(disposition), initialGeometry,
                                       targetUrl, userGesture, newWebContents);

    Q_EMIT q->newWindowRequested(request);

    if (request.isHandled())
        return newWebContents;
    return nullptr;
}

void QWebEnginePagePrivate::createNewWindow(WindowOpenDisposition disposition, bool userGesture, const QUrl &targetUrl)
{
    Q_Q(QWebEnginePage);
    QWebEnginePage *newPage = q->createWindow(toWindowType(disposition));
    if (newPage) {
        newPage->setUrl(targetUrl);
        return;
    }

    QWebEngineNewWindowRequest request(toDestinationType(disposition), QRect(),
                                       targetUrl, userGesture, nullptr);

    Q_EMIT q->newWindowRequested(request);
}

class WebContentsAdapterOwner : public QObject
{
public:
    typedef QSharedPointer<QtWebEngineCore::WebContentsAdapter> AdapterPtr;
    WebContentsAdapterOwner(const AdapterPtr &ptr)
        : adapter(ptr)
    {}

private:
    AdapterPtr adapter;
};

void QWebEnginePagePrivate::adoptWebContents(WebContentsAdapter *webContents)
{
    if (!webContents) {
        qWarning("Trying to open an empty request, it was either already used or was invalidated."
            "\nYou must complete the request synchronously within the newPageRequested signal handler."
            " If a view hasn't been adopted before returning, the request will be invalidated.");
        return;
    }

    if (webContents->profileAdapter() && profileAdapter() != webContents->profileAdapter()) {
        qWarning("Can not adopt content from a different WebEngineProfile.");
        return;
    }

    m_isBeingAdopted = true;

    // This throws away the WebContentsAdapter that has been used until now.
    // All its states, particularly the loading URL, are replaced by the adopted WebContentsAdapter.
    WebContentsAdapterOwner *adapterOwner = new WebContentsAdapterOwner(adapter->sharedFromThis());
    adapterOwner->deleteLater();

    adapter = webContents->sharedFromThis();
    adapter->setClient(this);
}

bool QWebEnginePagePrivate::isBeingAdopted()
{
    return m_isBeingAdopted;
}

void QWebEnginePagePrivate::close()
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->windowCloseRequested();
}

void QWebEnginePagePrivate::windowCloseRejected()
{
    // Do nothing for now.
}

void QWebEnginePagePrivate::didRunJavaScript(quint64 requestId, const QVariant& result)
{
    m_callbacks.invoke(requestId, result);
}

void QWebEnginePagePrivate::didFetchDocumentMarkup(quint64 requestId, const QString& result)
{
    m_callbacks.invoke(requestId, result);
}

void QWebEnginePagePrivate::didFetchDocumentInnerText(quint64 requestId, const QString& result)
{
    m_callbacks.invoke(requestId, result);
}

void QWebEnginePagePrivate::didPrintPage(quint64 requestId, QSharedPointer<QByteArray> result)
{
    if (view)
        view->didPrintPage(requestId, result);
}

bool QWebEnginePagePrivate::passOnFocus(bool reverse)
{
    return view ? view->passOnFocus(reverse) : false;
}

void QWebEnginePagePrivate::authenticationRequired(QSharedPointer<AuthenticationDialogController> controller)
{
    Q_Q(QWebEnginePage);
    QAuthenticator networkAuth;
    networkAuth.setRealm(controller->realm());

    if (controller->isProxy())
        Q_EMIT q->proxyAuthenticationRequired(controller->url(), &networkAuth, controller->host());
    else
        Q_EMIT q->authenticationRequired(controller->url(), &networkAuth);

    // Authentication has been cancelled
    if (networkAuth.isNull()) {
        controller->reject();
        return;
    }

    controller->accept(networkAuth.user(), networkAuth.password());
}

void QWebEnginePagePrivate::releaseProfile()
{
    qWarning("Release of profile requested but WebEnginePage still not deleted. Expect troubles !");
    // this is not the way to go, but might avoid the crash if user code does not make any calls to page.
    delete q_ptr->d_ptr.take();
}

void QWebEnginePagePrivate::showColorDialog(QSharedPointer<ColorChooserController> controller)
{
    if (view)
        view->showColorDialog(controller);
}

void QWebEnginePagePrivate::runMediaAccessPermissionRequest(const QUrl &securityOrigin, WebContentsAdapterClient::MediaRequestFlags requestFlags)
{
    Q_Q(QWebEnginePage);
    QWebEnginePage::Feature feature;
    if (requestFlags.testFlag(WebContentsAdapterClient::MediaAudioCapture) &&
        requestFlags.testFlag(WebContentsAdapterClient::MediaVideoCapture))
        feature = QWebEnginePage::MediaAudioVideoCapture;
    else if (requestFlags.testFlag(WebContentsAdapterClient::MediaAudioCapture))
        feature = QWebEnginePage::MediaAudioCapture;
    else if (requestFlags.testFlag(WebContentsAdapterClient::MediaVideoCapture))
        feature = QWebEnginePage::MediaVideoCapture;
    else if (requestFlags.testFlag(WebContentsAdapterClient::MediaDesktopAudioCapture) &&
             requestFlags.testFlag(WebContentsAdapterClient::MediaDesktopVideoCapture))
        feature = QWebEnginePage::DesktopAudioVideoCapture;
    else // if (requestFlags.testFlag(WebContentsAdapterClient::MediaDesktopVideoCapture))
        feature = QWebEnginePage::DesktopVideoCapture;
    Q_EMIT q->featurePermissionRequested(securityOrigin, feature);
}

static QWebEnginePage::Feature toFeature(QtWebEngineCore::ProfileAdapter::PermissionType type)
{
    switch (type) {
    case QtWebEngineCore::ProfileAdapter::NotificationPermission:
        return QWebEnginePage::Notifications;
    case QtWebEngineCore::ProfileAdapter::GeolocationPermission:
        return QWebEnginePage::Geolocation;
    default:
        break;
    }
    Q_UNREACHABLE();
    return QWebEnginePage::Feature(-1);
}

void QWebEnginePagePrivate::runFeaturePermissionRequest(QtWebEngineCore::ProfileAdapter::PermissionType permission, const QUrl &securityOrigin)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->featurePermissionRequested(securityOrigin, toFeature(permission));
}

void QWebEnginePagePrivate::runMouseLockPermissionRequest(const QUrl &securityOrigin)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->featurePermissionRequested(securityOrigin, QWebEnginePage::MouseLock);
}

void QWebEnginePagePrivate::runQuotaRequest(QWebEngineQuotaRequest request)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->quotaRequested(request);
}

void QWebEnginePagePrivate::runRegisterProtocolHandlerRequest(QWebEngineRegisterProtocolHandlerRequest request)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->registerProtocolHandlerRequested(request);
}

QObject *QWebEnginePagePrivate::accessibilityParentObject()
{
    return view ? view->accessibilityParentObject() : nullptr;
}

void QWebEnginePagePrivate::updateAction(QWebEnginePage::WebAction action) const
{
#ifdef QT_NO_ACTION
    Q_UNUSED(action);
#else
    QAction *a = actions[action];
    if (!a)
        return;

    bool enabled = true;

    switch (action) {
    case QWebEnginePage::Back:
        enabled = adapter->canGoToOffset(-1);
        break;
    case QWebEnginePage::Forward:
        enabled = adapter->canGoToOffset(1);
        break;
    case QWebEnginePage::Stop:
        enabled = isLoading;
        break;
    case QWebEnginePage::Reload:
    case QWebEnginePage::ReloadAndBypassCache:
        enabled = !isLoading;
        break;
    case QWebEnginePage::ViewSource:
        enabled = adapter->canViewSource();
        break;
    case QWebEnginePage::Cut:
    case QWebEnginePage::Copy:
    case QWebEnginePage::Unselect:
        enabled = adapter->hasFocusedFrame() && !adapter->selectedText().isEmpty();
        break;
    case QWebEnginePage::Paste:
    case QWebEnginePage::Undo:
    case QWebEnginePage::Redo:
    case QWebEnginePage::SelectAll:
    case QWebEnginePage::PasteAndMatchStyle:
        enabled = adapter->hasFocusedFrame();
        break;
    default:
        break;
    }

    a->setEnabled(enabled);
#endif // QT_NO_ACTION
}

void QWebEnginePagePrivate::updateNavigationActions()
{
    updateAction(QWebEnginePage::Back);
    updateAction(QWebEnginePage::Forward);
    updateAction(QWebEnginePage::Stop);
    updateAction(QWebEnginePage::Reload);
    updateAction(QWebEnginePage::ReloadAndBypassCache);
    updateAction(QWebEnginePage::ViewSource);
}

void QWebEnginePagePrivate::updateEditActions()
{
    updateAction(QWebEnginePage::Cut);
    updateAction(QWebEnginePage::Copy);
    updateAction(QWebEnginePage::Paste);
    updateAction(QWebEnginePage::Undo);
    updateAction(QWebEnginePage::Redo);
    updateAction(QWebEnginePage::SelectAll);
    updateAction(QWebEnginePage::PasteAndMatchStyle);
    updateAction(QWebEnginePage::Unselect);
}

#ifndef QT_NO_ACTION
void QWebEnginePagePrivate::_q_webActionTriggered(bool checked)
{
    Q_Q(QWebEnginePage);
    QAction *a = qobject_cast<QAction *>(q->sender());
    if (!a)
        return;
    QWebEnginePage::WebAction action = static_cast<QWebEnginePage::WebAction>(a->data().toInt());
    q->triggerAction(action, checked);
}
#endif // QT_NO_ACTION

void QWebEnginePagePrivate::recreateFromSerializedHistory(QDataStream &input)
{
    QSharedPointer<WebContentsAdapter> newWebContents = WebContentsAdapter::createFromSerializedNavigationHistory(input, this);
    if (newWebContents) {
        adapter = std::move(newWebContents);
        adapter->setClient(this);
        adapter->loadDefault();
    }
}

void QWebEnginePagePrivate::updateScrollPosition(const QPointF &position)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->scrollPositionChanged(position);
}

void QWebEnginePagePrivate::updateContentsSize(const QSizeF &size)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->contentsSizeChanged(size);
}

void QWebEnginePagePrivate::setFullScreenMode(bool fullscreen)
{
    if (fullscreenMode != fullscreen) {
        fullscreenMode = fullscreen;
        adapter->changedFullScreen();
    }
}

ProfileAdapter* QWebEnginePagePrivate::profileAdapter()
{
    return profile->d_ptr->profileAdapter();
}

WebContentsAdapter *QWebEnginePagePrivate::webContentsAdapter()
{
    ensureInitialized();
    return adapter.data();
}

const QObject *QWebEnginePagePrivate::holdingQObject() const
{
    Q_Q(const QWebEnginePage);
    return q;
}

void QWebEnginePagePrivate::findTextFinished(const QWebEngineFindTextResult &result)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->findTextFinished(result);
}

void QWebEnginePagePrivate::ensureInitialized() const
{
    if (!adapter->isInitialized())
        adapter->loadDefault();
}

QWebEnginePage::QWebEnginePage(QObject* parent)
    : QObject(parent)
    , d_ptr(new QWebEnginePagePrivate())
{
    Q_D(QWebEnginePage);
    d->q_ptr = this;
    d->adapter->setClient(d);
}

/*!
    \fn void QWebEnginePage::findTextFinished(const QWebEngineFindTextResult &result)
    \since 5.14

    This signal is emitted when a search string search on a page is completed. \a result is
    the result of the string search.

    \sa findText()
*/


/*!
    \enum QWebEnginePage::RenderProcessTerminationStatus
    \since 5.6

    This enum describes the status with which the render process terminated:

    \value  NormalTerminationStatus
            The render process terminated normally.
    \value  AbnormalTerminationStatus
            The render process terminated with with a non-zero exit status.
    \value  CrashedTerminationStatus
            The render process crashed, for example because of a segmentation fault.
    \value  KilledTerminationStatus
            The render process was killed, for example by \c SIGKILL or task manager kill.
*/

/*!
    \fn QWebEnginePage::renderProcessTerminated(RenderProcessTerminationStatus terminationStatus, int exitCode)
    \since 5.6

    This signal is emitted when the render process is terminated with a non-zero exit status.
    \a terminationStatus is the termination status of the process and \a exitCode is the status code
    with which the process terminated.
*/

/*!
    \fn QWebEnginePage::fullScreenRequested(QWebEngineFullScreenRequest fullScreenRequest)

    This signal is emitted when the web page issues the request to enter fullscreen mode for
    a web-element, usually a video element.

    The request object \a fullScreenRequest can be used to accept or reject the request.

    If the request is accepted the element requesting fullscreen will fill the viewport,
    but it is up to the application to make the view fullscreen or move the page to a view
    that is fullscreen.

    \sa QWebEngineSettings::FullScreenSupportEnabled
*/

/*!
    \fn QWebEnginePage::quotaRequested(QWebEngineQuotaRequest quotaRequest)
    \since 5.11

    This signal is emitted when the web page requests larger persistent storage
    than the application's current allocation in File System API. The default quota
    is 0 bytes.

    The request object \a quotaRequest can be used to accept or reject the request.
*/

/*!
    \fn QWebEnginePage::registerProtocolHandlerRequested(QWebEngineRegisterProtocolHandlerRequest
   request) \since 5.11

    This signal is emitted when the web page tries to register a custom protocol
    using the \l registerProtocolHandler API.

    The request object \a request can be used to accept or reject the request:

    \snippet webenginewidgets/simplebrowser/webview.cpp registerProtocolHandlerRequested
*/

/*!
    \property QWebEnginePage::scrollPosition
    \since 5.7

    \brief The scroll position of the page contents.
*/

/*!
    \property QWebEnginePage::contentsSize
    \since 5.7

    \brief The size of the page contents.
*/

/*!
    \fn void QWebEnginePage::audioMutedChanged(bool muted)
    \since 5.7

    This signal is emitted when the page's \a muted state changes.
    \note Not to be confused with a specific HTML5 audio or video element being muted.
*/

/*!
    \fn void QWebEnginePage::recentlyAudibleChanged(bool recentlyAudible);
    \since 5.7

    This signal is emitted when the page's audible state, \a recentlyAudible, changes, because
    the audio is played or stopped.

    \note The signal is also emitted when calling the setAudioMuted() method.
    Also, if the audio is paused, this signal is emitted with an approximate \b{two-second
    delay}, from the moment the audio is paused.
*/

/*!
  \fn void QWebEnginePage::renderProcessPidChanged(qint64 pid);
  \since 5.15

  This signal is emitted when the underlying render process PID, \a renderProcessPid, changes.
*/

/*!
    \fn void QWebEnginePage::iconUrlChanged(const QUrl &url)

    This signal is emitted when the URL of the icon ("favicon") associated with the
    page is changed. The new URL is specified by \a url.

    \sa iconUrl(), icon(), iconChanged()
*/

/*!
    \fn void QWebEnginePage::iconChanged(const QIcon &icon)
    \since 5.7

    This signal is emitted when the icon ("favicon") associated with the
    page is changed. The new icon is specified by \a icon.

    \sa icon(), iconUrl(), iconUrlChanged()
*/

/*!
    Constructs an empty web engine page in the web engine profile \a profile with the parent
    \a parent.

    If the profile is not the default profile, the caller must ensure that the profile stays alive
    for as long as the page does.

    \since 5.5
*/
QWebEnginePage::QWebEnginePage(QWebEngineProfile *profile, QObject* parent)
    : QObject(parent)
    , d_ptr(new QWebEnginePagePrivate(profile))
{
    Q_D(QWebEnginePage);
    d->q_ptr = this;
    d->adapter->setClient(d);
}

QWebEnginePage::~QWebEnginePage()
{
    if (d_ptr) {
        // d_ptr might be exceptionally null if profile adapter got deleted first
        setDevToolsPage(nullptr);
        emit _q_aboutToDelete();
    }
}

QWebEngineHistory *QWebEnginePage::history() const
{
    Q_D(const QWebEnginePage);
    return d->history;
}

QWebEngineSettings *QWebEnginePage::settings() const
{
    Q_D(const QWebEnginePage);
    return d->settings;
}

/*!
 * Returns a pointer to the web channel instance used by this page or a null pointer if none was set.
 * This channel automatically uses the internal web engine transport mechanism over Chromium IPC
 * that is exposed in the JavaScript context of this page as \c qt.webChannelTransport.
 *
 * \since 5.5
 * \sa setWebChannel()
 */
QWebChannel *QWebEnginePage::webChannel() const
{
#if QT_CONFIG(webengine_webchannel)
    Q_D(const QWebEnginePage);
    return d->webChannel;
#endif
    qWarning("WebEngine compiled without webchannel support");
    return nullptr;
}

/*!
 * \overload
 *
 * Sets the web channel instance to be used by this page to \a channel and installs
 * it in the main JavaScript world.
 *
 * With this method the web channel can be accessed by web page content. If the content
 * is not under your control and might be hostile, this could be a security issue and
 * you should consider installing it in a private JavaScript world.
 *
 * \since 5.5
 * \sa QWebEngineScript::MainWorld
 */

void QWebEnginePage::setWebChannel(QWebChannel *channel)
{
    setWebChannel(channel, QWebEngineScript::MainWorld);
}

/*!
 * Sets the web channel instance to be used by this page to \a channel and connects it to
 * web engine's transport using Chromium IPC messages. The transport is exposed in the JavaScript
 * world \a worldId as
 * \c qt.webChannelTransport, which should be used when using the \l{Qt WebChannel JavaScript API}.
 *
 * \note The page does not take ownership of the channel object.
 * \note Only one web channel can be installed per page, setting one even in another JavaScript
 *       world uninstalls any already installed web channel.
 *
 * \since 5.7
 * \sa QWebEngineScript::ScriptWorldId
 */
void QWebEnginePage::setWebChannel(QWebChannel *channel, uint worldId)
{
#if QT_CONFIG(webengine_webchannel)
    Q_D(QWebEnginePage);
    if (d->webChannel != channel || d->webChannelWorldId != worldId) {
        d->webChannel = channel;
        d->webChannelWorldId = worldId;
        d->adapter->setWebChannel(channel, worldId);
    }
#else
    Q_UNUSED(channel);
    Q_UNUSED(worldId);
    qWarning("WebEngine compiled without webchannel support");
#endif
}

/*!
    \property QWebEnginePage::backgroundColor
    \brief The page's background color behind the document's body.
    \since 5.6

    You can set the background color to Qt::transparent or to a translucent
    color to see through the document, or you can set it to match your
    web content in a hybrid application to prevent the white flashes that may appear
    during loading.

    The default value is white.
*/
QColor QWebEnginePage::backgroundColor() const
{
    Q_D(const QWebEnginePage);
    return d->m_backgroundColor;
}

void QWebEnginePage::setBackgroundColor(const QColor &color)
{
    Q_D(QWebEnginePage);
    if (d->m_backgroundColor == color)
        return;
    d->m_backgroundColor = color;
    d->adapter->setBackgroundColor(color);
}

/*!
 * Save the currently loaded web page to disk.
 *
 * The web page is saved to \a filePath in the specified \a{format}.
 *
 * This is a short cut for the following actions:
 * \list
 *   \li Trigger the Save web action.
 *   \li Accept the next download item and set the specified file path and save format.
 * \endlist
 *
 * This function issues an asynchronous download request for the web page and returns immediately.
 *
 * \sa QWebEngineDownloadRequest::SavePageFormat
 * \since 5.8
 */
void QWebEnginePage::save(const QString &filePath,
                          QWebEngineDownloadRequest::SavePageFormat format) const
{
    Q_D(const QWebEnginePage);
    d->ensureInitialized();
    d->adapter->save(filePath, format);
}

/*!
    \property QWebEnginePage::audioMuted
    \brief Whether the current page audio is muted.
    \since 5.7

    The default value is \c false.
    \sa recentlyAudible
*/
bool QWebEnginePage::isAudioMuted() const {
    Q_D(const QWebEnginePage);
    if (d->adapter->isInitialized())
        return d->adapter->isAudioMuted();
    return d->defaultAudioMuted;
}

void QWebEnginePage::setAudioMuted(bool muted) {
    Q_D(QWebEnginePage);
    bool wasAudioMuted = isAudioMuted();
    d->defaultAudioMuted = muted;
    d->adapter->setAudioMuted(muted);
    if (wasAudioMuted != isAudioMuted())
        Q_EMIT audioMutedChanged(muted);
}

/*!
    \property QWebEnginePage::recentlyAudible
    \brief The current page's \e {audible state}, that is, whether audio was recently played
    or not.
    \since 5.7

    The default value is \c false.
    \sa audioMuted
*/
bool QWebEnginePage::recentlyAudible() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->isInitialized() && d->adapter->recentlyAudible();
}

/*!
  \property QWebEnginePage::renderProcessPid
  \brief The process ID (PID) of the render process assigned to the current
  page's main frame.
  \since 5.15

  If no render process is available yet, \c 0 is returned.
*/
qint64 QWebEnginePage::renderProcessPid() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->renderProcessPid();
}

/*!
    Returns the web engine profile the page belongs to.
    \since 5.5
*/
QWebEngineProfile *QWebEnginePage::profile() const
{
    Q_D(const QWebEnginePage);
    return d->profile;
}

bool QWebEnginePage::hasSelection() const
{
    return !selectedText().isEmpty();
}

QString QWebEnginePage::selectedText() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->selectedText();
}

#ifndef QT_NO_ACTION
QAction *QWebEnginePage::action(WebAction action) const
{
    Q_D(const QWebEnginePage);
    if (action == QWebEnginePage::NoWebAction)
        return 0;
    if (d->actions[action])
        return d->actions[action];

    QString text;
    switch (action) {
    case Back:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Back);
        break;
    case Forward:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Forward);
        break;
    case Stop:
        text = tr("Stop");
        break;
    case Reload:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Reload);
        break;
    case ReloadAndBypassCache:
        text = tr("Reload and Bypass Cache");
        break;
    case Cut:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Cut);
        break;
    case Copy:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Copy);
        break;
    case Paste:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Paste);
        break;
    case Undo:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Undo);
        break;
    case Redo:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::Redo);
        break;
    case SelectAll:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::SelectAll);
        break;
    case PasteAndMatchStyle:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::PasteAndMatchStyle);
        break;
    case OpenLinkInThisWindow:
        text = tr("Open link in this window");
        break;
    case OpenLinkInNewWindow:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::OpenLinkInNewWindow);
        break;
    case OpenLinkInNewTab:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::OpenLinkInNewTab);
        break;
    case OpenLinkInNewBackgroundTab:
        text = tr("Open link in new background tab");
        break;
    case CopyLinkToClipboard:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::CopyLinkToClipboard);
        break;
    case DownloadLinkToDisk:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::DownloadLinkToDisk);
        break;
    case CopyImageToClipboard:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::CopyImageToClipboard);
        break;
    case CopyImageUrlToClipboard:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::CopyImageUrlToClipboard);
        break;
    case DownloadImageToDisk:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::DownloadImageToDisk);
        break;
    case CopyMediaUrlToClipboard:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::CopyMediaUrlToClipboard);
        break;
    case ToggleMediaControls:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::ToggleMediaControls);
        break;
    case ToggleMediaLoop:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::ToggleMediaLoop);
        break;
    case ToggleMediaPlayPause:
        text = tr("Toggle Play/Pause");
        break;
    case ToggleMediaMute:
        text = tr("Toggle Mute");
        break;
    case DownloadMediaToDisk:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::DownloadMediaToDisk);
        break;
    case InspectElement:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::InspectElement);
        break;
    case ExitFullScreen:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::ExitFullScreen);
        break;
    case RequestClose:
        text = tr("Close Page");
        break;
    case Unselect:
        text = tr("Unselect");
        break;
    case SavePage:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::SavePage);
        break;
    case ViewSource:
        text = RenderViewContextMenuQt::getMenuItemName(RenderViewContextMenuQt::ContextMenuItem::ViewSource);
        break;
    case ToggleBold:
        text = tr("&Bold");
        break;
    case ToggleItalic:
        text = tr("&Italic");
        break;
    case ToggleUnderline:
        text = tr("&Underline");
        break;
    case ToggleStrikethrough:
        text = tr("&Strikethrough");
        break;
    case AlignLeft:
        text = tr("Align &Left");
        break;
    case AlignCenter:
        text = tr("Align &Center");
        break;
    case AlignRight:
        text = tr("Align &Right");
        break;
    case AlignJustified:
        text = tr("Align &Justified");
        break;
    case Indent:
        text = tr("&Indent");
        break;
    case Outdent:
        text = tr("&Outdent");
        break;
    case InsertOrderedList:
        text = tr("Insert &Ordered List");
        break;
    case InsertUnorderedList:
        text = tr("Insert &Unordered List");
        break;
    case NoWebAction:
    case WebActionCount:
        Q_UNREACHABLE();
        break;
    }

    QAction *a = new QAction(const_cast<QWebEnginePage*>(this));
    a->setText(text);
    a->setData(action);

    connect(a, SIGNAL(triggered(bool)), this, SLOT(_q_webActionTriggered(bool)));

    d->actions[action] = a;
    d->updateAction(action);
    return a;
}
#endif // QT_NO_ACTION

void QWebEnginePage::triggerAction(WebAction action, bool)
{
    Q_D(QWebEnginePage);
    d->ensureInitialized();
    switch (action) {
    case Back:
        d->adapter->navigateBack();
        break;
    case Forward:
        d->adapter->navigateForward();
        break;
    case Stop:
        d->adapter->stop();
        break;
    case Reload:
        d->adapter->reload();
        break;
    case ReloadAndBypassCache:
        d->adapter->reloadAndBypassCache();
        break;
    case Cut:
        d->adapter->cut();
        break;
    case Copy:
        d->adapter->copy();
        break;
    case Paste:
        d->adapter->paste();
        break;
    case Undo:
        d->adapter->undo();
        break;
    case Redo:
        d->adapter->redo();
        break;
    case SelectAll:
        d->adapter->selectAll();
        break;
    case PasteAndMatchStyle:
        d->adapter->pasteAndMatchStyle();
        break;
    case Unselect:
        d->adapter->unselect();
        break;
    case OpenLinkInThisWindow:
        if (d->view && d->view->lastContextMenuRequest()->filteredLinkUrl().isValid())
            setUrl(d->view->lastContextMenuRequest()->filteredLinkUrl());
        break;
    case OpenLinkInNewWindow:
        if (d->view && d->view->lastContextMenuRequest()->filteredLinkUrl().isValid())
            d->createNewWindow(WebContentsAdapterClient::NewWindowDisposition, true,
                               d->view->lastContextMenuRequest()->filteredLinkUrl());
        break;
    case OpenLinkInNewTab:
        if (d->view && d->view->lastContextMenuRequest()->filteredLinkUrl().isValid())
            d->createNewWindow(WebContentsAdapterClient::NewForegroundTabDisposition, true,
                               d->view->lastContextMenuRequest()->filteredLinkUrl());
        break;
    case OpenLinkInNewBackgroundTab:
        if (d->view && d->view->lastContextMenuRequest()->filteredLinkUrl().isValid())
            d->createNewWindow(WebContentsAdapterClient::NewBackgroundTabDisposition, true,
                               d->view->lastContextMenuRequest()->filteredLinkUrl());
        break;
    case CopyLinkToClipboard:
        if (d->view && !d->view->lastContextMenuRequest()->linkUrl().isEmpty()) {
            QString urlString = d->view->lastContextMenuRequest()->linkUrl().toString(
                    QUrl::FullyEncoded);
            QString linkText = d->view->lastContextMenuRequest()->linkText().toHtmlEscaped();
            QString title = d->view->lastContextMenuRequest()->titleText();
            if (!title.isEmpty())
                title = QStringLiteral(" title=\"%1\"").arg(title.toHtmlEscaped());
            QMimeData *data = new QMimeData();
            data->setText(urlString);
            QString html = QStringLiteral("<a href=\"") + urlString + QStringLiteral("\"") + title + QStringLiteral(">")
                         + linkText + QStringLiteral("</a>");
            data->setHtml(html);
            data->setUrls(QList<QUrl>() << d->view->lastContextMenuRequest()->linkUrl());
            QGuiApplication::clipboard()->setMimeData(data);
        }
        break;
    case DownloadLinkToDisk:
        if (d->view && d->view->lastContextMenuRequest()->filteredLinkUrl().isValid())
            d->adapter->download(d->view->lastContextMenuRequest()->filteredLinkUrl(),
                                 d->view->lastContextMenuRequest()->suggestedFileName(),
                                 d->view->lastContextMenuRequest()->referrerUrl(),
                                 d->view->lastContextMenuRequest()->referrerPolicy());

        break;
    case CopyImageToClipboard:
        if (d->view && d->view->lastContextMenuRequest()->hasImageContent()
            && (d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeImage
                || d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeCanvas)) {
            d->adapter->copyImageAt(d->view->lastContextMenuRequest()->position());
        }
        break;
    case CopyImageUrlToClipboard:
        if (d->view && d->view->lastContextMenuRequest()->mediaUrl().isValid()
            && d->view->lastContextMenuRequest()->mediaType()
                    == QWebEngineContextMenuRequest::MediaTypeImage) {
            QString urlString =
                    d->view->lastContextMenuRequest()->mediaUrl().toString(QUrl::FullyEncoded);
            QString alt = d->view->lastContextMenuRequest()->altText();
            if (!alt.isEmpty())
                alt = QStringLiteral(" alt=\"%1\"").arg(alt.toHtmlEscaped());
            QString title = d->view->lastContextMenuRequest()->titleText();
            if (!title.isEmpty())
                title = QStringLiteral(" title=\"%1\"").arg(title.toHtmlEscaped());
            QMimeData *data = new QMimeData();
            data->setText(urlString);
            QString html = QStringLiteral("<img src=\"") + urlString + QStringLiteral("\"") + title + alt + QStringLiteral("></img>");
            data->setHtml(html);
            data->setUrls(QList<QUrl>() << d->view->lastContextMenuRequest()->mediaUrl());
            QGuiApplication::clipboard()->setMimeData(data);
        }
        break;
    case DownloadImageToDisk:
    case DownloadMediaToDisk:
        if (d->view && d->view->lastContextMenuRequest()->mediaUrl().isValid())
            d->adapter->download(d->view->lastContextMenuRequest()->mediaUrl(),
                                 d->view->lastContextMenuRequest()->suggestedFileName(),
                                 d->view->lastContextMenuRequest()->referrerUrl(),
                                 d->view->lastContextMenuRequest()->referrerPolicy());
        break;
    case CopyMediaUrlToClipboard:
        if (d->view && d->view->lastContextMenuRequest()->mediaUrl().isValid()
            && (d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeAudio
                || d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeVideo)) {
            QString urlString =
                    d->view->lastContextMenuRequest()->mediaUrl().toString(QUrl::FullyEncoded);
            QString title = d->view->lastContextMenuRequest()->titleText();
            if (!title.isEmpty())
                title = QStringLiteral(" title=\"%1\"").arg(title.toHtmlEscaped());
            QMimeData *data = new QMimeData();
            data->setText(urlString);
            if (d->view->lastContextMenuRequest()->mediaType()
                == QWebEngineContextMenuRequest::MediaTypeAudio)
                data->setHtml(QStringLiteral("<audio src=\"") + urlString + QStringLiteral("\"") + title +
                              QStringLiteral("></audio>"));
            else
                data->setHtml(QStringLiteral("<video src=\"") + urlString + QStringLiteral("\"") + title +
                              QStringLiteral("></video>"));
            data->setUrls(QList<QUrl>() << d->view->lastContextMenuRequest()->mediaUrl());
            QGuiApplication::clipboard()->setMimeData(data);
        }
        break;
    case ToggleMediaControls:
        if (d->view && d->view->lastContextMenuRequest()->mediaUrl().isValid()
            && d->view->lastContextMenuRequest()->mediaFlags()
                    & QWebEngineContextMenuRequest::MediaCanToggleControls) {
            bool enable = !(d->view->lastContextMenuRequest()->mediaFlags()
                            & QWebEngineContextMenuRequest::MediaControls);
            d->adapter->executeMediaPlayerActionAt(d->view->lastContextMenuRequest()->position(),
                                                   WebContentsAdapter::MediaPlayerControls, enable);
        }
        break;
    case ToggleMediaLoop:
        if (d->view && d->view->lastContextMenuRequest()->mediaUrl().isValid()
            && (d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeAudio
                || d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeVideo)) {
            bool enable = !(d->view->lastContextMenuRequest()->mediaFlags()
                            & QWebEngineContextMenuRequest::MediaLoop);
            d->adapter->executeMediaPlayerActionAt(d->view->lastContextMenuRequest()->position(),
                                                   WebContentsAdapter::MediaPlayerLoop, enable);
        }
        break;
    case ToggleMediaPlayPause:
        if (d->view && d->view->lastContextMenuRequest()->mediaUrl().isValid()
            && (d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeAudio
                || d->view->lastContextMenuRequest()->mediaType()
                        == QWebEngineContextMenuRequest::MediaTypeVideo)) {
            bool enable = (d->view->lastContextMenuRequest()->mediaFlags()
                           & QWebEngineContextMenuRequest::MediaPaused);
            d->adapter->executeMediaPlayerActionAt(d->view->lastContextMenuRequest()->position(),
                                                   WebContentsAdapter::MediaPlayerPlay, enable);
        }
        break;
    case ToggleMediaMute:
        if (d->view && d->view->lastContextMenuRequest()->mediaUrl().isValid()
            && d->view->lastContextMenuRequest()->mediaFlags()
                    & QWebEngineContextMenuRequest::MediaHasAudio) {
            // Make sure to negate the value, so that toggling actually works.
            bool enable = !(d->view->lastContextMenuRequest()->mediaFlags()
                            & QWebEngineContextMenuRequest::MediaMuted);
            d->adapter->executeMediaPlayerActionAt(d->view->lastContextMenuRequest()->position(),
                                                   WebContentsAdapter::MediaPlayerMute, enable);
        }
        break;
    case InspectElement:
        if (d->view)
            d->adapter->inspectElementAt(d->view->lastContextMenuRequest()->position());
        break;
    case ExitFullScreen:
        // See under ViewSource, anything that can trigger a delete of the current view is dangerous to call directly here.
        QTimer::singleShot(0, this, [d](){ d->adapter->exitFullScreen(); });
        break;
    case RequestClose:
        d->adapter->requestClose();
        break;
    case SavePage:
        d->adapter->save();
        break;
    case ViewSource:
        // This is a workaround to make the ViewSource action working in a context menu.
        // The WebContentsAdapter::viewSource() method deletes a
        // RenderWidgetHostViewQtDelegateWidget instance which passes the control to the event
        // loop. If the QMenu::aboutToHide() signal is connected to the QObject::deleteLater()
        // slot the QMenu is deleted by the event handler while the ViewSource action is still not
        // completed. This may lead to a crash. To avoid this the WebContentsAdapter::viewSource()
        // method is called indirectly via the QTimer::singleShot() function which schedules the
        // the viewSource() call after the QMenu's destruction.
        QTimer::singleShot(0, this, [d](){ d->adapter->viewSource(); });
        break;
    case ToggleBold:
        runJavaScript(QStringLiteral("document.execCommand('bold');"), QWebEngineScript::ApplicationWorld);
        break;
    case ToggleItalic:
        runJavaScript(QStringLiteral("document.execCommand('italic');"), QWebEngineScript::ApplicationWorld);
        break;
    case ToggleUnderline:
        runJavaScript(QStringLiteral("document.execCommand('underline');"), QWebEngineScript::ApplicationWorld);
        break;
    case ToggleStrikethrough:
        runJavaScript(QStringLiteral("document.execCommand('strikethrough');"), QWebEngineScript::ApplicationWorld);
        break;
    case AlignLeft:
        runJavaScript(QStringLiteral("document.execCommand('justifyLeft');"), QWebEngineScript::ApplicationWorld);
        break;
    case AlignCenter:
        runJavaScript(QStringLiteral("document.execCommand('justifyCenter');"), QWebEngineScript::ApplicationWorld);
        break;
    case AlignRight:
        runJavaScript(QStringLiteral("document.execCommand('justifyRight');"), QWebEngineScript::ApplicationWorld);
        break;
    case AlignJustified:
        runJavaScript(QStringLiteral("document.execCommand('justifyFull');"), QWebEngineScript::ApplicationWorld);
        break;
    case Indent:
        runJavaScript(QStringLiteral("document.execCommand('indent');"), QWebEngineScript::ApplicationWorld);
        break;
    case Outdent:
        runJavaScript(QStringLiteral("document.execCommand('outdent');"), QWebEngineScript::ApplicationWorld);
        break;
    case InsertOrderedList:
        runJavaScript(QStringLiteral("document.execCommand('insertOrderedList');"), QWebEngineScript::ApplicationWorld);
        break;
    case InsertUnorderedList:
        runJavaScript(QStringLiteral("document.execCommand('insertUnorderedList');"), QWebEngineScript::ApplicationWorld);
        break;
    case NoWebAction:
        break;
    case WebActionCount:
        Q_UNREACHABLE();
        break;
    }
}

/*!
 * \since 5.8
 * Replace the current misspelled word with \a replacement.
 *
 * The current misspelled word can be found in QWebEngineContextMenuRequest::misspelledWord(),
 * and suggested replacements in QWebEngineContextMenuRequest::spellCheckerSuggestions().
 *
 * \sa contextMenuData(),
 */

void QWebEnginePage::replaceMisspelledWord(const QString &replacement)
{
    Q_D(QWebEnginePage);
    d->adapter->replaceMisspelling(replacement);
}

void QWebEnginePage::findText(const QString &subString, FindFlags options, const QWebEngineCallback<bool> &resultCallback)
{
    Q_D(QWebEnginePage);
    if (!d->adapter->isInitialized()) {
        QtWebEngineCore::CallbackDirectory().invokeEmpty(resultCallback);
        return;
    }

    d->adapter->findTextHelper()->startFinding(subString, options & FindCaseSensitively, options & FindBackward, resultCallback);
}

/*!
 * \reimp
 */
bool QWebEnginePage::event(QEvent *e)
{
    return QObject::event(e);
}

void QWebEnginePagePrivate::contextMenuRequested(QWebEngineContextMenuRequest *data)
{
    if (view)
        view->contextMenuRequested(data);
}

/*!
    \fn void QWebEnginePage::navigationRequested(QWebEngineNavigationRequest &request)
    \since 6.2

    This signal is emitted on navigation together with the call the acceptNavigationRequest().
    It can be used to accept or ignore the request. The default is to accept.

    \sa acceptNavigationRequest()

*/

void QWebEnginePagePrivate::navigationRequested(int navigationType, const QUrl &url, int &navigationRequestAction, bool isMainFrame)
{
    Q_Q(QWebEnginePage);

    bool accepted = q->acceptNavigationRequest(url, static_cast<QWebEnginePage::NavigationType>(navigationType), isMainFrame);
    if (accepted) {
        QWebEngineNavigationRequest navigationRequest(url, static_cast<QWebEngineNavigationRequest::NavigationType>(navigationType), isMainFrame);
        Q_EMIT q->navigationRequested(navigationRequest);
        accepted = (navigationRequest.action() == QWebEngineNavigationRequest::AcceptRequest);
    }

    if (accepted && adapter->findTextHelper()->isFindTextInProgress())
        adapter->findTextHelper()->stopFinding();
    navigationRequestAction = accepted ? WebContentsAdapterClient::AcceptRequest : WebContentsAdapterClient::IgnoreRequest;
}

void QWebEnginePagePrivate::requestFullScreenMode(const QUrl &origin, bool fullscreen)
{
    Q_Q(QWebEnginePage);
    QWebEngineFullScreenRequest request(origin, fullscreen, [q = QPointer(q)] (bool toggleOn) { if (q) q->d_ptr->setFullScreenMode(toggleOn); });
    Q_EMIT q->fullScreenRequested(std::move(request));
}

bool QWebEnginePagePrivate::isFullScreenMode() const
{
    return fullscreenMode;
}

void QWebEnginePagePrivate::javascriptDialog(QSharedPointer<JavaScriptDialogController> controller)
{
    Q_Q(QWebEnginePage);
    bool accepted = false;
    QString promptResult;
    switch (controller->type()) {
    case AlertDialog:
        q->javaScriptAlert(controller->securityOrigin(), controller->message());
        accepted = true;
        break;
    case ConfirmDialog:
        accepted = q->javaScriptConfirm(controller->securityOrigin(), controller->message());
        break;
    case PromptDialog:
        accepted = q->javaScriptPrompt(controller->securityOrigin(), controller->message(), controller->defaultPrompt(), &promptResult);
        if (accepted)
            controller->textProvided(promptResult);
        break;
    case UnloadDialog:
        accepted = q->javaScriptConfirm(controller->securityOrigin(), QCoreApplication::translate("QWebEnginePage", "Are you sure you want to leave this page? Changes that you made may not be saved."));
        break;
    case InternalAuthorizationDialog:
        accepted = view ? view->showAuthorizationDialog(controller->title(), controller->message())
                        : false;
        break;
    }
    if (accepted)
        controller->accept();
    else
        controller->reject();
}

void QWebEnginePagePrivate::allowCertificateError(const QWebEngineCertificateError &error)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->certificateError(error);
}

void QWebEnginePagePrivate::selectClientCert(const QSharedPointer<ClientCertSelectController> &controller)
{
    Q_Q(QWebEnginePage);
    QWebEngineClientCertificateSelection certSelection(controller);

    Q_EMIT q->selectClientCertificate(certSelection);
}

/*!
    \fn void QWebEnginePage::selectClientCertificate(QWebEngineClientCertificateSelection clientCertificateSelection)
    \since 5.12

    This signal is emitted when a web site requests an SSL client certificate, and one or more were
    found in system's client certificate store.

    Handling the signal is asynchronous, and loading will be waiting until a certificate is selected,
    or the last copy of \a clientCertificateSelection is destroyed.

    If the signal is not handled, \a clientCertificateSelection is automatically destroyed, and loading
    will continue without a client certificate.

    \sa QWebEngineClientCertificateSelection
*/

void QWebEnginePagePrivate::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber, const QString &sourceID)
{
    Q_Q(QWebEnginePage);
    q->javaScriptConsoleMessage(static_cast<QWebEnginePage::JavaScriptConsoleMessageLevel>(level), message, lineNumber, sourceID);
}

void QWebEnginePagePrivate::renderProcessTerminated(RenderProcessTerminationStatus terminationStatus,
                                                int exitCode)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->renderProcessTerminated(static_cast<QWebEnginePage::RenderProcessTerminationStatus>(
                                      terminationStatus), exitCode);
}

void QWebEnginePagePrivate::requestGeometryChange(const QRect &geometry, const QRect &frameGeometry)
{
    Q_UNUSED(geometry);
    Q_Q(QWebEnginePage);
    Q_EMIT q->geometryChangeRequested(frameGeometry);
}

QObject *QWebEnginePagePrivate::dragSource() const
{
#if !QT_CONFIG(draganddrop)
    return view;
#else
    return nullptr;
#endif // QT_CONFIG(draganddrop)
}

bool QWebEnginePagePrivate::isEnabled() const
{
    if (view)
        return view->isEnabled();
    return true;
}

void QWebEnginePagePrivate::setToolTip(const QString &toolTipText)
{
    if (view)
        view->setToolTip(toolTipText);
}

void QWebEnginePagePrivate::printRequested()
{
    if (view)
        view->printRequested();
}

void QWebEnginePagePrivate::lifecycleStateChanged(LifecycleState state)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->lifecycleStateChanged(static_cast<QWebEnginePage::LifecycleState>(state));
}

void QWebEnginePagePrivate::recommendedStateChanged(LifecycleState state)
{
    Q_Q(QWebEnginePage);
    QTimer::singleShot(0, q, [q, state]() {
        Q_EMIT q->recommendedStateChanged(static_cast<QWebEnginePage::LifecycleState>(state));
    });
}

void QWebEnginePagePrivate::visibleChanged(bool visible)
{
    Q_Q(QWebEnginePage);
    Q_EMIT q->visibleChanged(visible);
}

/*!
    \since 5.13

    Registers the request interceptor \a interceptor to intercept URL requests.

    The page does not take ownership of the pointer. This interceptor is called
    after any interceptors on the profile, and unlike profile interceptors, only
    URL requests from this page are intercepted.

    To unset the request interceptor, set a \c nullptr.

    \sa QWebEngineUrlRequestInfo, QWebEngineProfile::setUrlRequestInterceptor()
*/

void QWebEnginePage::setUrlRequestInterceptor(QWebEngineUrlRequestInterceptor *interceptor)
{
    Q_D(QWebEnginePage);
    d->adapter->setRequestInterceptor(interceptor);
}

void QWebEnginePage::setFeaturePermission(const QUrl &securityOrigin, QWebEnginePage::Feature feature, QWebEnginePage::PermissionPolicy policy)
{
    Q_D(QWebEnginePage);
    if (policy == PermissionUnknown) {
        switch (feature) {
        case MediaAudioVideoCapture:
        case MediaAudioCapture:
        case MediaVideoCapture:
        case DesktopAudioVideoCapture:
        case DesktopVideoCapture:
        case MouseLock:
            break;
        case Geolocation:
            d->adapter->grantFeaturePermission(securityOrigin, ProfileAdapter::GeolocationPermission, ProfileAdapter::AskPermission);
            break;
        case Notifications:
            d->adapter->grantFeaturePermission(securityOrigin, ProfileAdapter::NotificationPermission, ProfileAdapter::AskPermission);
            break;
        }
        return;
    }

    const WebContentsAdapterClient::MediaRequestFlags audioVideoCaptureFlags(
        WebContentsAdapterClient::MediaVideoCapture |
        WebContentsAdapterClient::MediaAudioCapture);
    const WebContentsAdapterClient::MediaRequestFlags desktopAudioVideoCaptureFlags(
        WebContentsAdapterClient::MediaDesktopVideoCapture |
        WebContentsAdapterClient::MediaDesktopAudioCapture);

    if (policy == PermissionGrantedByUser) {
        switch (feature) {
        case MediaAudioVideoCapture:
            d->adapter->grantMediaAccessPermission(securityOrigin, audioVideoCaptureFlags);
            break;
        case MediaAudioCapture:
            d->adapter->grantMediaAccessPermission(securityOrigin, WebContentsAdapterClient::MediaAudioCapture);
            break;
        case MediaVideoCapture:
            d->adapter->grantMediaAccessPermission(securityOrigin, WebContentsAdapterClient::MediaVideoCapture);
            break;
        case DesktopAudioVideoCapture:
            d->adapter->grantMediaAccessPermission(securityOrigin, desktopAudioVideoCaptureFlags);
            break;
        case DesktopVideoCapture:
            d->adapter->grantMediaAccessPermission(securityOrigin, WebContentsAdapterClient::MediaDesktopVideoCapture);
            break;
        case MouseLock:
            d->adapter->grantMouseLockPermission(securityOrigin, true);
            break;
        case Geolocation:
            d->adapter->grantFeaturePermission(securityOrigin, ProfileAdapter::GeolocationPermission, ProfileAdapter::AllowedPermission);
            break;
        case Notifications:
            d->adapter->grantFeaturePermission(securityOrigin, ProfileAdapter::NotificationPermission, ProfileAdapter::AllowedPermission);
            break;
        }
    } else { // if (policy == PermissionDeniedByUser)
        switch (feature) {
        case MediaAudioVideoCapture:
        case MediaAudioCapture:
        case MediaVideoCapture:
        case DesktopAudioVideoCapture:
        case DesktopVideoCapture:
            d->adapter->grantMediaAccessPermission(securityOrigin, WebContentsAdapterClient::MediaNone);
            break;
        case Geolocation:
            d->adapter->grantFeaturePermission(securityOrigin, ProfileAdapter::GeolocationPermission, ProfileAdapter::DeniedPermission);
            break;
        case MouseLock:
            d->adapter->grantMouseLockPermission(securityOrigin, false);
            break;
        case Notifications:
            d->adapter->grantFeaturePermission(securityOrigin, ProfileAdapter::NotificationPermission, ProfileAdapter::DeniedPermission);
            break;
        }
    }
}

static inline QWebEnginePage::FileSelectionMode toPublic(FilePickerController::FileChooserMode mode)
{
    // Should the underlying values change, we'll need a switch here.
    return static_cast<QWebEnginePage::FileSelectionMode>(mode);
}

void QWebEnginePagePrivate::runFileChooser(QSharedPointer<FilePickerController> controller)
{
    Q_Q(QWebEnginePage);

    QStringList selectedFileNames = q->chooseFiles(toPublic(controller->mode()), (QStringList() << controller->defaultFileName()), controller->acceptedMimeTypes());

    if (!selectedFileNames.empty())
        controller->accepted(selectedFileNames);
    else
        controller->rejected();
}

QWebEngineSettings *QWebEnginePagePrivate::webEngineSettings() const
{
    return settings;
}

/*!
    \since 5.10
    Downloads the resource from the location given by \a url to a local file.

    If \a filename is given, it is used as the suggested file name.
    If it is relative, the file is saved in the standard download location with
    the given name.
    If it is a null or empty QString, the default file name is used.

    This will emit QWebEngineProfile::downloadRequested() after the download
    has started.
*/

void QWebEnginePage::download(const QUrl& url, const QString& filename)
{
    Q_D(QWebEnginePage);
    d->ensureInitialized();
    d->adapter->download(url, filename);
}

void QWebEnginePage::load(const QUrl& url)
{
    Q_D(QWebEnginePage);
    d->adapter->load(url);
}

/*!
    \since 5.9
    Issues the specified \a request and loads the response.

    \sa load(), setUrl(), url(), urlChanged(), QUrl::fromUserInput()
*/
void QWebEnginePage::load(const QWebEngineHttpRequest& request)
{
    Q_D(QWebEnginePage);
    d->adapter->load(request);
}

void QWebEnginePage::toHtml(const QWebEngineCallback<const QString &> &resultCallback) const
{
    Q_D(const QWebEnginePage);
    d->ensureInitialized();
    quint64 requestId = d->adapter->fetchDocumentMarkup();
    d->m_callbacks.registerCallback(requestId, resultCallback);
}

void QWebEnginePage::toPlainText(const QWebEngineCallback<const QString &> &resultCallback) const
{
    Q_D(const QWebEnginePage);
    d->ensureInitialized();
    quint64 requestId = d->adapter->fetchDocumentInnerText();
    d->m_callbacks.registerCallback(requestId, resultCallback);
}

void QWebEnginePage::setHtml(const QString &html, const QUrl &baseUrl)
{
    setContent(html.toUtf8(), QStringLiteral("text/html;charset=UTF-8"), baseUrl);
}

void QWebEnginePage::setContent(const QByteArray &data, const QString &mimeType, const QUrl &baseUrl)
{
    Q_D(QWebEnginePage);
    d->adapter->setContent(data, mimeType, baseUrl);
}

QString QWebEnginePage::title() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->pageTitle();
}

void QWebEnginePage::setUrl(const QUrl &url)
{
    Q_D(QWebEnginePage);
    if (d->url != url) {
        d->url = url;
        emit urlChanged(url);
    }
    load(url);
}

QUrl QWebEnginePage::url() const
{
    Q_D(const QWebEnginePage);
    return d->url;
}

QUrl QWebEnginePage::requestedUrl() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->requestedUrl();
}

/*!
    \property QWebEnginePage::iconUrl
    \brief The URL of the icon associated with the page currently viewed.

    By default, this property contains an empty URL.

    \sa iconUrlChanged(), icon(), iconChanged()
*/
QUrl QWebEnginePage::iconUrl() const
{
    Q_D(const QWebEnginePage);
    return d->iconUrl;
}

/*!
    \property QWebEnginePage::icon
    \brief The icon associated with the page currently viewed.
    \since 5.7

    By default, this property contains a null icon. If the web page specifies more than one icon,
    the \c{icon} property encapsulates the available candidate icons in a single,
    scalable \c{QIcon}.

    \sa iconChanged(), iconUrl(), iconUrlChanged()
*/
QIcon QWebEnginePage::icon() const
{
    Q_D(const QWebEnginePage);

    if (d->iconUrl.isEmpty() || !d->adapter->isInitialized())
        return QIcon();

    return d->adapter->icon();
}

qreal QWebEnginePage::zoomFactor() const
{
    Q_D(const QWebEnginePage);
    if (d->adapter->isInitialized())
        return d->adapter->currentZoomFactor();
    return d->defaultZoomFactor;
}

void QWebEnginePage::setZoomFactor(qreal factor)
{
    Q_D(QWebEnginePage);
    d->defaultZoomFactor = factor;
    if (d->adapter->isInitialized())
        d->adapter->setZoomFactor(factor);
}

void QWebEnginePage::runJavaScript(const QString &scriptSource)
{
    Q_D(QWebEnginePage);
    d->ensureInitialized();
    if (d->adapter->lifecycleState() == WebContentsAdapter::LifecycleState::Discarded) {
        qWarning("runJavaScript: disabled in Discarded state");
        return;
    }
    d->adapter->runJavaScript(scriptSource, QWebEngineScript::MainWorld);
}

void QWebEnginePage::runJavaScript(const QString& scriptSource, const QWebEngineCallback<const QVariant &> &resultCallback)
{
    Q_D(QWebEnginePage);
    d->ensureInitialized();
    if (d->adapter->lifecycleState() == WebContentsAdapter::LifecycleState::Discarded) {
        qWarning("runJavaScript: disabled in Discarded state");
        d->m_callbacks.invokeEmpty(resultCallback);
        return;
    }
    quint64 requestId = d->adapter->runJavaScriptCallbackResult(scriptSource, QWebEngineScript::MainWorld);
    d->m_callbacks.registerCallback(requestId, resultCallback);
}

void QWebEnginePage::runJavaScript(const QString &scriptSource, quint32 worldId)
{
    Q_D(QWebEnginePage);
    d->ensureInitialized();
    d->adapter->runJavaScript(scriptSource, worldId);
}

void QWebEnginePage::runJavaScript(const QString& scriptSource, quint32 worldId, const QWebEngineCallback<const QVariant &> &resultCallback)
{
    Q_D(QWebEnginePage);
    d->ensureInitialized();
    quint64 requestId = d->adapter->runJavaScriptCallbackResult(scriptSource, worldId);
    d->m_callbacks.registerCallback(requestId, resultCallback);
}

/*!
    Returns the collection of scripts that are injected into the page.

    In addition, a page might also execute scripts
    added through QWebEngineProfile::scripts().

    \sa QWebEngineScriptCollection, QWebEngineScript, {Script Injection}
*/

QWebEngineScriptCollection &QWebEnginePage::scripts()
{
    Q_D(QWebEnginePage);
    return d->scriptCollection;
}

QWebEnginePage *QWebEnginePage::createWindow(WebWindowType type)
{
    Q_D(QWebEnginePage);
    return d->view ? d->view->createPageForWindow(type) : nullptr;
}

/*!
    \since 5.11
    Returns the page this page is inspecting, if any.

    Returns \c nullptr if this page is not a developer tools page.

    \sa setInspectedPage(), devToolsPage()
*/

QWebEnginePage *QWebEnginePage::inspectedPage() const
{
    Q_D(const QWebEnginePage);
    return d->inspectedPage;
}

/*!
    \since 5.11
    Navigates this page to an internal URL that is the developer
    tools of \a page.

    This is the same as calling setDevToolsPage() on \a page
    with \c this as argument.

    \sa inspectedPage(), setDevToolsPage()
*/

void QWebEnginePage::setInspectedPage(QWebEnginePage *page)
{
    Q_D(QWebEnginePage);
    if (d->inspectedPage == page)
        return;
    QWebEnginePage *oldPage = d->inspectedPage;
    d->inspectedPage = nullptr;
    if (oldPage)
        oldPage->setDevToolsPage(nullptr);
    d->inspectedPage = page;
    if (page)
        page->setDevToolsPage(this);
}

/*!
    \since 5.11
    Returns the page that is hosting the developer tools
    of this page, if any.

    Returns \c nullptr if no developer tools page is set.

    \sa setDevToolsPage(), inspectedPage()
*/

QWebEnginePage *QWebEnginePage::devToolsPage() const
{
    Q_D(const QWebEnginePage);
    return d->devToolsPage;
}

/*!
    \since 5.11
    Binds \a devToolsPage to be the developer tools of this page.
    Triggers \a devToolsPage to navigate to an internal URL
    with the developer tools.

    This is the same as calling setInspectedPage() on \a devToolsPage
    with \c this as argument.

    \sa devToolsPage(), setInspectedPage()
*/

void QWebEnginePage::setDevToolsPage(QWebEnginePage *devToolsPage)
{
    Q_D(QWebEnginePage);
    if (d->devToolsPage == devToolsPage)
        return;
    d->ensureInitialized();
    QWebEnginePage *oldDevTools = d->devToolsPage;
    d->devToolsPage = nullptr;
    if (oldDevTools)
        oldDevTools->setInspectedPage(nullptr);
    d->devToolsPage = devToolsPage;
    if (devToolsPage)
        devToolsPage->setInspectedPage(this);
    if (d->adapter) {
        if (devToolsPage)
            d->adapter->openDevToolsFrontend(devToolsPage->d_ptr->adapter);
        else
            d->adapter->closeDevToolsFrontend();
    }
}

ASSERT_ENUMS_MATCH(FilePickerController::Open, QWebEnginePage::FileSelectOpen)
ASSERT_ENUMS_MATCH(FilePickerController::OpenMultiple, QWebEnginePage::FileSelectOpenMultiple)
ASSERT_ENUMS_MATCH(FilePickerController::UploadFolder, QWebEnginePage::FileSelectUploadFolder)

// TODO: remove virtuals
QStringList QWebEnginePage::chooseFiles(FileSelectionMode mode, const QStringList &oldFiles, const QStringList &acceptedMimeTypes)
{
    Q_D(const QWebEnginePage);
    return d->view ? d->view->chooseFiles(mode, oldFiles, acceptedMimeTypes) : QStringList();
}

void QWebEnginePage::javaScriptAlert(const QUrl &securityOrigin, const QString &msg)
{
    Q_UNUSED(securityOrigin);
    Q_D(const QWebEnginePage);
    if (d->view)
        d->view->javaScriptAlert(url(), msg);
}

bool QWebEnginePage::javaScriptConfirm(const QUrl &securityOrigin, const QString &msg)
{
    Q_UNUSED(securityOrigin);
    Q_D(const QWebEnginePage);
    return d->view ? d->view->javaScriptConfirm(url(), msg) : false;
}

bool QWebEnginePage::javaScriptPrompt(const QUrl &securityOrigin, const QString &msg, const QString &defaultValue, QString *result)
{
    Q_UNUSED(securityOrigin);
    Q_D(const QWebEnginePage);
    return d->view ? d->view->javaScriptPrompt(url(), msg, defaultValue, result) : false;
}

void QWebEnginePage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber, const QString &sourceID)
{
    static QLoggingCategory loggingCategory("js", QtWarningMsg);
    static QByteArray file = sourceID.toUtf8();
    QMessageLogger logger(file.constData(), lineNumber, nullptr, loggingCategory.categoryName());

    switch (level) {
    case JavaScriptConsoleMessageLevel::InfoMessageLevel:
        if (loggingCategory.isInfoEnabled())
            logger.info().noquote() << message;
        break;
    case JavaScriptConsoleMessageLevel::WarningMessageLevel:
        if (loggingCategory.isWarningEnabled())
            logger.warning().noquote() << message;
        break;
    case JavaScriptConsoleMessageLevel::ErrorMessageLevel:
        if (loggingCategory.isCriticalEnabled())
            logger.critical().noquote() << message;
        break;
    }
}

bool QWebEnginePage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    Q_UNUSED(url);
    Q_UNUSED(type);
    Q_UNUSED(isMainFrame);
    return true;
}

QPointF QWebEnginePage::scrollPosition() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->lastScrollOffset();
}

QSizeF QWebEnginePage::contentsSize() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->lastContentsSize();
}

/*!
    \fn void QWebEnginePage::newWindowRequested(WebEngineNewViewRequest &request)
    \since 6.2

    This signal is emitted when \a request is issued to load a page in a separate
    web engine window. This can either be because the current page requested it explicitly
    through a JavaScript call to \c window.open, or because the user clicked on a link
    while holding Shift, Ctrl, or a built-in combination that triggers the page to open
    in a new window.

    The signal is handled by calling acceptAsNewWindow() on the destination page.
    If this signal is not handled, the requested load will fail.

    \note This signal is not emitted if \l createWindow() handled the request first.

    \sa createWindow()
*/

/*!
    Handles the newWindowRequested() signal by opening the \a request in this page.
    \since 6.2
    \sa newWindowRequested
*/
void QWebEnginePage::acceptAsNewWindow(QWebEngineNewWindowRequest &request)
{
    Q_D(QWebEnginePage);
    auto adapter = request.adapter();
    QUrl url = request.requestedUrl();
    if ((!adapter && !url.isValid()) || request.isHandled()) {
        qWarning("Trying to open an empty request, it was either already used or was invalidated."
            "\nYou must complete the request synchronously within the newWindowRequested signal handler."
            " If a view hasn't been adopted before returning, the request will be invalidated.");
        return;
    }

    if (adapter)
        d->adoptWebContents(adapter.data());
    else
        setUrl(url);

    QRect geometry = request.requestedGeometry();
    if (!geometry.isEmpty())
        emit geometryChangeRequested(geometry);

    request.setHandled();
}

/*!
  \enum QWebEnginePage::LifecycleState
  \since 5.14

  This enum describes the lifecycle state of the page:

  \value  Active
  Normal state.
  \value  Frozen
  Low CPU usage state where most HTML task sources are suspended.
  \value  Discarded
  Very low resource usage state where the entire browsing context is discarded.

  \sa lifecycleState, {Page Lifecycle API}, {WebEngine Lifecycle Example}
*/

/*!
  \property QWebEnginePage::lifecycleState
  \since 5.14

  \brief The current lifecycle state of the page.

  The following restrictions are enforced by the setter:

  \list
  \li A \l{visible} page must remain in the \c{Active} state.
  \li If the page is being inspected by a \l{devToolsPage} then both pages must
      remain in the \c{Active} states.
  \li A page in the \c{Discarded} state can only transition to the \c{Active}
      state. This will cause a reload of the page.
  \endlist

  These are the only hard limits on the lifecycle state, but see also
  \l{recommendedState} for the recommended soft limits.

  \sa recommendedState, {Page Lifecycle API}, {WebEngine Lifecycle Example}
*/

QWebEnginePage::LifecycleState QWebEnginePage::lifecycleState() const
{
    Q_D(const QWebEnginePage);
    return static_cast<LifecycleState>(d->adapter->lifecycleState());
}

void QWebEnginePage::setLifecycleState(LifecycleState state)
{
    Q_D(QWebEnginePage);
    d->adapter->setLifecycleState(static_cast<WebContentsAdapterClient::LifecycleState>(state));
}

/*!
  \property QWebEnginePage::recommendedState
  \since 5.14

  \brief The recommended limit for the lifecycle state of the page.

  Setting the lifecycle state to a lower resource usage state than the
  recommended state may cause side-effects such as stopping background audio
  playback or loss of HTML form input. Setting the lifecycle state to a higher
  resource state is however completely safe.

  \sa lifecycleState, {Page Lifecycle API}, {WebEngine Lifecycle Example}
*/

QWebEnginePage::LifecycleState QWebEnginePage::recommendedState() const
{
    Q_D(const QWebEnginePage);
    return static_cast<LifecycleState>(d->adapter->recommendedState());
}

/*!
  \property QWebEnginePage::visible
  \since 5.14

  \brief Whether the page is considered visible in the Page Visibility API.

  Setting this property changes the \c{Document.hidden} and the
  \c{Document.visibilityState} properties in JavaScript which web sites can use
  to voluntarily reduce their resource usage if they are not visible to the
  user.

  If the page is connected to a \l{view} then this property will be managed
  automatically by the view according to it's own visibility.

  \sa lifecycleState
*/

bool QWebEnginePage::isVisible() const
{
    Q_D(const QWebEnginePage);
    return d->adapter->isVisible();
}

void QWebEnginePage::setVisible(bool visible)
{
    Q_D(QWebEnginePage);

    if (!d->adapter->isInitialized()) {
        // On the one hand, it is too early to initialize here. The application
        // may call show() before load(), or it may call show() from
        // createWindow(), and then we would create an unnecessary blank
        // WebContents here. On the other hand, if the application calls show()
        // then it expects something to be shown, so we have to initialize.
        // Therefore we have to delay the initialization via the event loop.
        if (visible)
            d->wasShownTimer.start();
        else
            d->wasShownTimer.stop();
        return;
    }

    d->adapter->setVisible(visible);
}

QWebEnginePage* QWebEnginePage::fromDownloadRequest(QWebEngineDownloadRequest *request) {
    return static_cast<QWebEnginePagePrivate *>(request->d_ptr->m_adapterClient)->q_ptr;
}

QDataStream &operator<<(QDataStream &stream, const QWebEngineHistory &history)
{
    auto adapter = history.d_func()->adapter();
    if (!adapter->isInitialized())
        adapter->loadDefault();
    adapter->serializeNavigationHistory(stream);
    return stream;
}

QDataStream &operator>>(QDataStream &stream, QWebEngineHistory &history)
{
    static_cast<QWebEnginePagePrivate *>(history.d_func()->client)->recreateFromSerializedHistory(stream);
    return stream;
}

QT_END_NAMESPACE

#include "moc_qwebenginepage.cpp"
