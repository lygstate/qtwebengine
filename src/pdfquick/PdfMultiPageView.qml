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
import QtQuick
import QtQuick.Controls
import QtQuick.Pdf
import QtQuick.Shapes

/*!
    \qmltype PdfMultiPageView
    \inqmlmodule QtQuick.Pdf
    \brief A complete PDF viewer component for scrolling through multiple pages.

    PdfMultiPageView provides a PDF viewer component that offers a user
    experience similar to many common PDF viewer applications. It supports
    flicking through the pages in the entire document, with narrow gaps between
    the page images.

    PdfMultiPageView also supports selecting text and copying it to the
    clipboard, zooming in and out, clicking an internal link to jump to another
    section in the document, rotating the view, and searching for text. The
    \l {PDF Multipage Viewer Example} demonstrates how to use these features
    in an application.

    The implementation is a QML assembly of smaller building blocks that are
    available separately. In case you want to make changes in your own version
    of this component, you can copy the QML, which is installed into the
    \c QtQuick/Pdf/qml module directory, and modify it as needed.

    \sa PdfPageView, PdfScrollablePageView, PdfStyle
*/
Item {
    /*!
        \qmlproperty PdfDocument PdfMultiPageView::document

        A PdfDocument object with a valid \c source URL is required:

        \snippet multipageview.qml 0
    */
    required property PdfDocument document

    /*!
        \qmlproperty PdfDocument PdfMultiPageView::selectedText

        The selected text.
    */
    property string selectedText

    /*!
        \qmlmethod void PdfMultiPageView::selectAll()

        Selects all the text on the \l {currentPage}{current page}, and makes it
        available as the system \l {QClipboard::Selection}{selection} on systems
        that support that feature.

        \sa copySelectionToClipboard()
    */
    function selectAll() {
        const currentItem = tableView.itemAtCell(tableView.cellAtPos(root.width / 2, root.height / 2))
        if (currentItem)
            currentItem.selection.selectAll()
    }

    /*!
        \qmlmethod void PdfMultiPageView::copySelectionToClipboard()

        Copies the selected text (if any) to the
        \l {QClipboard::Clipboard}{system clipboard}.

        \sa selectAll()
    */
    function copySelectionToClipboard() {
        const currentItem = tableView.itemAtCell(tableView.cellAtPos(root.width / 2, root.height / 2))
        console.log(lcMPV, "currentItem", currentItem, "sel", currentItem.selection.text)
        if (currentItem)
            currentItem.selection.copyToClipboard()
    }

    // --------------------------------
    // page navigation

    /*!
        \qmlproperty int PdfMultiPageView::currentPage
        \readonly

        This property holds the zero-based page number of the page visible in the
        scrollable view. If there is no current page, it holds -1.

        This property is read-only, and is typically used in a binding (or
        \c onCurrentPageChanged script) to update the part of the user interface
        that shows the current page number, such as a \l SpinBox.

        \sa PdfNavigationStack::currentPage
    */
    property alias currentPage: navigationStack.currentPage

    /*!
        \qmlproperty bool PdfMultiPageView::backEnabled
        \readonly

        This property indicates if it is possible to go back in the navigation
        history to a previous-viewed page.

        \sa PdfNavigationStack::backAvailable, back()
    */
    property alias backEnabled: navigationStack.backAvailable

    /*!
        \qmlproperty bool PdfMultiPageView::forwardEnabled
        \readonly

        This property indicates if it is possible to go to next location in the
        navigation history.

        \sa PdfNavigationStack::forwardAvailable, forward()
    */
    property alias forwardEnabled: navigationStack.forwardAvailable

    /*!
        \qmlmethod void PdfMultiPageView::back()

        Scrolls the view back to the previous page that the user visited most
        recently; or does nothing if there is no previous location on the
        navigation stack.

        \sa PdfNavigationStack::back(), currentPage, backEnabled
    */
    function back() { navigationStack.back() }

    /*!
        \qmlmethod void PdfMultiPageView::forward()

        Scrolls the view to the page that the user was viewing when the back()
        method was called; or does nothing if there is no "next" location on the
        navigation stack.

        \sa PdfNavigationStack::forward(), currentPage
    */
    function forward() { navigationStack.forward() }

    /*!
        \qmlmethod void PdfMultiPageView::goToPage(int page)

        Scrolls the view to the given \a page number, if possible.

        \sa PdfNavigationStack::jump(), currentPage
    */
    function goToPage(page) {
        if (page === navigationStack.currentPage)
            return
        goToLocation(page, Qt.point(-1, -1), 0)
    }

    /*!
        \qmlmethod void PdfMultiPageView::goToLocation(int page, point location, real zoom)

        Scrolls the view to the \a location on the \a page, if possible,
        and sets the \a zoom level.

        \sa PdfNavigationStack::jump(), currentPage
    */
    function goToLocation(page, location, zoom) {
        if (zoom > 0) {
            navigationStack.jumping = true // don't call navigationStack.update() because we will jump() instead
            root.renderScale = zoom
            tableView.forceLayout() // but do ensure that the table layout is correct before we try to jump
            navigationStack.jumping = false
        }
        navigationStack.jump(page, location, zoom) // actually jump
    }

    /*!
        \qmlproperty int PdfMultiPageView::currentPageRenderingStatus

        This property holds the \l {QtQuick::Image::status}{rendering status} of
        the \l {currentPage}{current page}.

        \sa PdfPageImage::status
    */
    property int currentPageRenderingStatus: Image.Null

    // --------------------------------
    // page scaling

    /*!
        \qmlproperty real PdfMultiPageView::renderScale

        This property holds the ratio of pixels to points. The default is \c 1,
        meaning one point (1/72 of an inch) equals 1 logical pixel.

        \sa PdfPageImage::status
    */
    property real renderScale: 1

    /*!
        \qmlproperty real PdfMultiPageView::pageRotation

        This property holds the clockwise rotation of the pages.

        The default value is \c 0 degrees (that is, no rotation relative to the
        orientation of the pages as stored in the PDF file).

        \sa PdfPageImage::rotation
    */
    property real pageRotation: 0

    /*!
        \qmlmethod void PdfMultiPageView::resetScale()

        Sets \l renderScale back to its default value of \c 1.
    */
    function resetScale() { root.renderScale = 1 }

    /*!
        \qmlmethod void PdfMultiPageView::scaleToWidth(real width, real height)

        Sets \l renderScale such that the width of the first page will fit into a
        viewport with the given \a width and \a height. If the page is not rotated,
        it will be scaled so that its width fits \a width. If it is rotated +/- 90
        degrees, it will be scaled so that its width fits \a height.
    */
    function scaleToWidth(width, height) {
        root.renderScale = width / (tableView.rot90 ? tableView.firstPagePointSize.height : tableView.firstPagePointSize.width)
    }

    /*!
        \qmlmethod void PdfMultiPageView::scaleToPage(real width, real height)

        Sets \l renderScale such that the whole first page will fit into a viewport
        with the given \a width and \a height. The resulting \l renderScale depends
        on \l pageRotation: the page will fit into the viewport at a larger size if
        it is first rotated to have a matching aspect ratio.
    */
    function scaleToPage(width, height) {
        const windowAspect = width / height
        const pageAspect = tableView.firstPagePointSize.width / tableView.firstPagePointSize.height
        if (tableView.rot90) {
            if (windowAspect > pageAspect) {
                root.renderScale = height / tableView.firstPagePointSize.width
            } else {
                root.renderScale = width / tableView.firstPagePointSize.height
            }
        } else {
            if (windowAspect > pageAspect) {
                root.renderScale = height / tableView.firstPagePointSize.height
            } else {
                root.renderScale = width / tableView.firstPagePointSize.width
            }
        }
    }

    // --------------------------------
    // text search

    /*!
        \qmlproperty PdfSearchModel PdfMultiPageView::searchModel

        This property holds a PdfSearchModel containing the list of search results
        for a given \l searchString.

        \sa PdfSearchModel
    */
    property alias searchModel: searchModel

    /*!
        \qmlproperty string PdfMultiPageView::searchString

        This property holds the search string that the user may choose to search
        for. It is typically used in a binding to the
        \l {QtQuick.Controls::TextField::text}{text} property of a TextField.

        \sa searchModel
    */
    property alias searchString: searchModel.searchString

    /*!
        \qmlmethod void PdfMultiPageView::searchBack()

        Decrements the
        \l{PdfSearchModel::currentResult}{searchModel's current result}
        so that the view will jump to the previous search result.
    */
    function searchBack() { --searchModel.currentResult }

    /*!
        \qmlmethod void PdfMultiPageView::searchForward()

        Increments the
        \l{PdfSearchModel::currentResult}{searchModel's current result}
        so that the view will jump to the next search result.
    */
    function searchForward() { ++searchModel.currentResult }

    LoggingCategory {
        id: lcMPV
        name: "qt.pdf.multipageview"
    }

    id: root
    PdfStyle { id: style }
    TableView {
        id: tableView
        property bool debug: false
        property vector2d jumpLocationMargin: Qt.vector2d(10, 10)  // px from top-left corner
        anchors.fill: parent
        anchors.leftMargin: 2
        model: modelInUse && root.document ? root.document.pageCount : 0
        // workaround to make TableView do scheduleRebuildTable(RebuildOption::All) in cases when forceLayout() doesn't
        property bool modelInUse: true
        function rebuild() {
            modelInUse = false
            modelInUse = true
        }
        // end workaround
        rowSpacing: 6
        property real rotationNorm: Math.round((360 + (root.pageRotation % 360)) % 360)
        property bool rot90: rotationNorm == 90 || rotationNorm == 270
        onRot90Changed: forceLayout()
        property size firstPagePointSize: document?.status === PdfDocument.Ready ? document.pagePointSize(0) : Qt.size(1, 1)
        property real pageHolderWidth: Math.max(root.width, ((rot90 ? document?.maxPageHeight : document?.maxPageWidth) ?? 0) * root.renderScale)
        columnWidthProvider: function(col) { return document ? pageHolderWidth + vscroll.width + 2 : 0 }
        rowHeightProvider: function(row) { return (rot90 ? document.pagePointSize(row).width : document.pagePointSize(row).height) * root.renderScale }
        delegate: Rectangle {
            id: pageHolder
            color: tableView.debug ? "beige" : "transparent"
            Text {
                visible: tableView.debug
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                rotation: -90; text: pageHolder.width.toFixed(1) + "x" + pageHolder.height.toFixed(1) + "\n" +
                                     image.width.toFixed(1) + "x" + image.height.toFixed(1)
            }
            property alias selection: selection
            Rectangle {
                id: paper
                width: image.width
                height: image.height
                rotation: root.pageRotation
                anchors.centerIn: pinch.active ? undefined : parent
                property size pagePointSize: document.pagePointSize(index)
                property real pageScale: image.paintedWidth / pagePointSize.width
                PdfPageImage {
                    id: image
                    document: root.document
                    currentPage: index
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    width: paper.pagePointSize.width * root.renderScale
                    height: paper.pagePointSize.height * root.renderScale
                    property real renderScale: root.renderScale
                    property real oldRenderScale: 1
                    onRenderScaleChanged: {
                        image.sourceSize.width = paper.pagePointSize.width * renderScale * Screen.devicePixelRatio
                        image.sourceSize.height = 0
                        paper.scale = 1
                        searchHighlights.update()
                        tableView.forceLayout()
                    }
                    onStatusChanged: {
                        if (index === navigationStack.currentPage)
                            root.currentPageRenderingStatus = status
                    }
                }
                Shape {
                    anchors.fill: parent
                    visible: image.status === Image.Ready
                    onVisibleChanged: searchHighlights.update()
                    ShapePath {
                        strokeWidth: -1
                        fillColor: style.pageSearchResultsColor
                        scale: Qt.size(paper.pageScale, paper.pageScale)
                        PathMultiline {
                            id: searchHighlights
                            function update() {
                                // paths could be a binding, but we need to be able to "kick" it sometimes
                                paths = searchModel.boundingPolygonsOnPage(index)
                            }
                        }
                    }
                    Connections {
                        target: searchModel
                        // whenever the highlights on the _current_ page change, they actually need to change on _all_ pages
                        // (usually because the search string has changed)
                        function onCurrentPageBoundingPolygonsChanged() { searchHighlights.update() }
                    }
                    ShapePath {
                        strokeWidth: -1
                        fillColor: style.selectionColor
                        scale: Qt.size(paper.pageScale, paper.pageScale)
                        PathMultiline {
                            paths: selection.geometry
                        }
                    }
                }
                Shape {
                    anchors.fill: parent
                    visible: image.status === Image.Ready && searchModel.currentPage === index
                    ShapePath {
                        strokeWidth: style.currentSearchResultStrokeWidth
                        strokeColor: style.currentSearchResultStrokeColor
                        fillColor: "transparent"
                        scale: Qt.size(paper.pageScale, paper.pageScale)
                        PathMultiline {
                            paths: searchModel.currentResultBoundingPolygons
                        }
                    }
                }
                PinchHandler {
                    id: pinch
                    minimumScale: 0.1
                    maximumScale: root.renderScale < 4 ? 2 : 1
                    minimumRotation: root.pageRotation
                    maximumRotation: root.pageRotation
                    enabled: image.sourceSize.width < 5000
                    onActiveChanged:
                        if (active) {
                            paper.z = 10
                        } else {
                            paper.z = 0
                            const centroidInPoints = Qt.point(pinch.centroid.position.x / root.renderScale,
                                                            pinch.centroid.position.y / root.renderScale)
                            const centroidInFlickable = tableView.mapFromItem(paper, pinch.centroid.position.x, pinch.centroid.position.y)
                            const newSourceWidth = image.sourceSize.width * paper.scale
                            const ratio = newSourceWidth / image.sourceSize.width
                            console.log(lcMPV, "pinch ended on page", index, "with centroid", pinch.centroid.position, centroidInPoints, "wrt flickable", centroidInFlickable,
                                        "page at", pageHolder.x.toFixed(2), pageHolder.y.toFixed(2),
                                        "contentX/Y were", tableView.contentX.toFixed(2), tableView.contentY.toFixed(2))
                            if (ratio > 1.1 || ratio < 0.9) {
                                const centroidOnPage = Qt.point(centroidInPoints.x * root.renderScale * ratio, centroidInPoints.y * root.renderScale * ratio)
                                paper.scale = 1
                                paper.x = 0
                                paper.y = 0
                                root.renderScale *= ratio
                                tableView.forceLayout()
                                if (tableView.rotationNorm == 0) {
                                    tableView.contentX = pageHolder.x + tableView.originX + centroidOnPage.x - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + centroidOnPage.y - centroidInFlickable.y
                                } else if (tableView.rotationNorm == 90) {
                                    tableView.contentX = pageHolder.x + tableView.originX + image.height - centroidOnPage.y - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + centroidOnPage.x - centroidInFlickable.y
                                } else if (tableView.rotationNorm == 180) {
                                    tableView.contentX = pageHolder.x + tableView.originX + image.width - centroidOnPage.x - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + image.height - centroidOnPage.y - centroidInFlickable.y
                                } else if (tableView.rotationNorm == 270) {
                                    tableView.contentX = pageHolder.x + tableView.originX + centroidOnPage.y - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + image.width - centroidOnPage.x - centroidInFlickable.y
                                }
                                console.log(lcMPV, "contentX/Y adjusted to", tableView.contentX.toFixed(2), tableView.contentY.toFixed(2), "y @top", pageHolder.y)
                                tableView.returnToBounds()
                            }
                        }
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                }
                DragHandler {
                    id: textSelectionDrag
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
                    target: null
                }
                TapHandler {
                    id: mouseClickHandler
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
                }
                TapHandler {
                    id: touchTapHandler
                    acceptedDevices: PointerDevice.TouchScreen
                    onTapped: {
                        selection.clear()
                        selection.forceActiveFocus()
                    }
                }
                Repeater {
                    model: PdfLinkModel {
                        id: linkModel
                        document: root.document
                        page: image.currentPage
                    }
                    delegate: Shape {
                        required property rect rect
                        required property url url
                        required property int page
                        required property point location
                        required property real zoom
                        x: rect.x * paper.pageScale
                        y: rect.y * paper.pageScale
                        width: rect.width * paper.pageScale
                        height: rect.height * paper.pageScale
                        visible: image.status === Image.Ready
                        ShapePath {
                            strokeWidth: style.linkUnderscoreStrokeWidth
                            strokeColor: style.linkUnderscoreColor
                            strokeStyle: style.linkUnderscoreStrokeStyle
                            dashPattern: style.linkUnderscoreDashPattern
                            startX: 0; startY: height
                            PathLine { x: width; y: height }
                        }
                        HoverHandler {
                            id: linkHH
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: {
                                if (page >= 0)
                                    root.goToLocation(page, location, zoom)
                                else
                                    Qt.openUrlExternally(url)
                            }
                        }
                        ToolTip {
                            visible: linkHH.hovered
                            delay: 1000
                            text: page >= 0 ?
                                      ("page " + (page + 1) +
                                       " location " + location.x.toFixed(1) + ", " + location.y.toFixed(1) +
                                       " zoom " + zoom) : url
                        }
                    }
                }
                PdfSelection {
                    id: selection
                    anchors.fill: parent
                    document: root.document
                    page: image.currentPage
                    renderScale: image.renderScale
                    fromPoint: textSelectionDrag.centroid.pressPosition
                    toPoint: textSelectionDrag.centroid.position
                    hold: !textSelectionDrag.active && !mouseClickHandler.pressed
                    onTextChanged: root.selectedText = text
                    focus: true
                }
            }
        }
        ScrollBar.vertical: ScrollBar {
            id: vscroll
            property bool moved: false
            onPositionChanged: moved = true
            onPressedChanged: if (pressed) {
                // When the user starts scrolling, push the location where we came from so the user can go "back" there
                const cell = tableView.cellAtPos(root.width / 2, root.height / 2)
                const currentItem = tableView.itemAtCell(cell)
                const currentLocation = currentItem
                                      ? Qt.point((tableView.contentX - currentItem.x + tableView.jumpLocationMargin.x) / root.renderScale,
                                                 (tableView.contentY - currentItem.y + tableView.jumpLocationMargin.y) / root.renderScale)
                                      : Qt.point(0, 0) // maybe the delegate wasn't loaded yet
                navigationStack.jump(cell.y, currentLocation, root.renderScale)
            }
            onActiveChanged: if (!active ) {
                // When the scrollbar stops moving, tell navstack where we are, so as to update currentPage etc.
                const cell = tableView.cellAtPos(root.width / 2, root.height / 2)
                const currentItem = tableView.itemAtCell(cell)
                const currentLocation = currentItem
                                      ? Qt.point((tableView.contentX - currentItem.x + tableView.jumpLocationMargin.x) / root.renderScale,
                                                 (tableView.contentY - currentItem.y + tableView.jumpLocationMargin.y) / root.renderScale)
                                      : Qt.point(0, 0) // maybe the delegate wasn't loaded yet
                navigationStack.update(cell.y, currentLocation, root.renderScale)
            }
        }
        ScrollBar.horizontal: ScrollBar { }
    }
    onRenderScaleChanged: {
        // if navigationStack.jumped changes the scale, don't turn around and update the stack again;
        // and don't force layout either, because positionViewAtCell() will do that
        if (navigationStack.jumping)
            return
        // make TableView rebuild from scratch, because otherwise it doesn't know the delegates are changing size
        tableView.rebuild()
        const cell = tableView.cellAtPos(root.width / 2, root.height / 2)
        const currentItem = tableView.itemAtCell(cell)
        if (currentItem) {
            const currentLocation = Qt.point((tableView.contentX - currentItem.x + tableView.jumpLocationMargin.x) / root.renderScale,
                                             (tableView.contentY - currentItem.y + tableView.jumpLocationMargin.y) / root.renderScale)
            navigationStack.update(cell.y, currentLocation, renderScale)
        }
    }
    PdfNavigationStack {
        id: navigationStack
        property bool jumping: false
        property int previousPage: 0
        onJumped: function(page, location, zoom) {
            jumping = true
            root.renderScale = zoom
            if (location.y < 0) {
                // invalid to indicate that a specific location was not needed,
                // so attempt to position the new page just as the current page is
                const previousPageDelegate = tableView.itemAtCell(0, previousPage)
                const currentYOffset = previousPageDelegate
                                     ? tableView.contentY - previousPageDelegate.y
                                     : 0
                tableView.positionViewAtRow(page, Qt.AlignTop, currentYOffset)
                console.log(lcMPV, "going from page", previousPage, "to", page, "offset", currentYOffset,
                            "ended up @", tableView.contentX.toFixed(1) + ", " + tableView.contentY.toFixed(1))
            } else {
                // jump to a page and position the given location relative to the top-left corner of the viewport
                var pageSize = root.document.pagePointSize(page)
                pageSize.width *= root.renderScale
                pageSize.height *= root.renderScale
                const xOffsetLimit = Math.max(0, pageSize.width - root.width) / 2
                const offset = Qt.point(Math.max(-xOffsetLimit, Math.min(xOffsetLimit,
                                            location.x * root.renderScale - tableView.jumpLocationMargin.x)),
                                        Math.max(0, location.y * root.renderScale - tableView.jumpLocationMargin.y))
                tableView.positionViewAtCell(0, page, Qt.AlignLeft | Qt.AlignTop, offset)
                console.log(lcMPV, "going to zoom", zoom, "loc", location, "on page", page,
                            "ended up @", tableView.contentX.toFixed(1) + ", " + tableView.contentY.toFixed(1))
            }
            jumping = false
            previousPage = page
        }
        onCurrentPageChanged: searchModel.currentPage = currentPage

        property url documentSource: root.document.source
        onDocumentSourceChanged: {
            navigationStack.clear()
            root.resetScale()
            tableView.contentX = 0
            tableView.contentY = 0
        }
    }
    PdfSearchModel {
        id: searchModel
        document: root.document === undefined ? null : root.document
        // TODO maybe avoid jumping if the result is already fully visible in the viewport
        onCurrentResultBoundingRectChanged: root.goToLocation(currentPage,
            Qt.point(currentResultBoundingRect.x, currentResultBoundingRect.y), 0)
    }
}
