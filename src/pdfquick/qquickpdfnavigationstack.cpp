/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtPDF module of the Qt Toolkit.
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

#include "qquickpdfnavigationstack_p.h"
#include <QLoggingCategory>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcNav, "qt.pdf.navigationstack")

/*!
    \qmltype PdfNavigationStack
//!    \instantiates QQuickPdfNavigationStack
    \inqmlmodule QtQuick.Pdf
    \ingroup pdf
    \brief History of the destinations visited within a PDF Document.
    \since 5.15

    PdfNavigationStack remembers which destinations the user has visited in a PDF
    document, and provides the ability to traverse backward and forward.
*/

QQuickPdfNavigationStack::QQuickPdfNavigationStack(QObject *parent)
    : QObject(parent)
{
}

/*!
    \internal
*/
QQuickPdfNavigationStack::~QQuickPdfNavigationStack() = default;

/*!
    \internal
*/
QPdfNavigationStack *QQuickPdfNavigationStack::navStack()
{
    return static_cast<QPdfNavigationStack *>(qmlExtendedObject(this));
}

/*!
    \qmlmethod void PdfNavigationStack::forward()

    Goes back to the page, location and zoom level that was being viewed before
    back() was called, and then emits the \l jumped() signal.

    If a new destination was pushed since the last time \l back() was called,
    the forward() function does nothing, because there is a branch in the
    timeline which causes the "future" to be lost.
*/

/*!
    \qmlmethod void PdfNavigationStack::back()

    Pops the stack, updates the \l currentPage, \l currentLocation and
    \l currentZoom properties to the most-recently-viewed destination, and then
    emits the \l jumped() signal.
*/

/*!
    \qmlproperty int PdfNavigationStack::currentPage

    This property holds the current page that is being viewed.
    If there is no current page, it holds \c -1.
*/

/*!
    \qmlproperty point PdfNavigationStack::currentLocation

    This property holds the current location on the page that is being viewed.
*/

/*!
    \qmlproperty real PdfNavigationStack::currentZoom

    This property holds the magnification scale on the page that is being viewed.
*/

/*!
    \qmlmethod void PdfNavigationStack::jump(int page, point location, qreal zoom, bool emitJumped)

    Adds the given destination, consisting of \a page, \a location, and \a zoom,
    to the history of visited locations.  If \a emitJumped is \c false, the
    \l jumped() signal will not be emitted.

    If forwardAvailable is \c true, calling this function represents a branch
    in the timeline which causes the "future" to be lost, and therefore
    forwardAvailable will change to \c false.
*/

/*!
    \qmlmethod void PdfNavigationStack::update(int page, point location, qreal zoom)

    Modifies the current destination, consisting of \a page, \a location and \a zoom.

    This can be called periodically while the user is manually moving around
    the document, so that after back() is called, forward() will jump back to
    the most-recently-viewed destination rather than the destination that was
    last specified by jump().

    The \c currentZoomChanged, \c currentPageChanged and \c currentLocationChanged
    signals will be emitted if the respective properties are actually changed.
    The \l jumped signal is not emitted, because this operation
    represents smooth movement rather than a navigational jump.
*/

/*!
    \qmlproperty bool PdfNavigationStack::backAvailable
    \readonly

    Holds \c true if a \e back destination is available in the history.
*/

/*!
    \qmlproperty bool PdfNavigationStack::forwardAvailable
    \readonly

    Holds \c true if a \e forward destination is available in the history.
*/

/*!
    \qmlsignal PdfNavigationStack::jumped(int page, point location, qreal zoom)

    This signal is emitted when an abrupt jump occurs, to the specified \a page
    index, \a location on the page, and \a zoom level; but \e not when simply
    scrolling through the document one page at a time. That is, forward(),
    back() and jump() always emit this signal; update() does not.
*/

QT_END_NAMESPACE
