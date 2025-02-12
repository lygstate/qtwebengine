/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Pdf
import QtQuick.Shapes

ApplicationWindow {
    id: root
    width: 800
    height: 940
    color: "darkgrey"
    title: doc.source
    visible: true

    property PdfDocument doc: PdfDocument { source: "test.pdf" }

    Component.onCompleted: {
        if (Application.arguments.length > 2)
            doc.source = Application.arguments[Application.arguments.length - 1]
    }
    FileDialog {
        id: fileDialog
        title: "Open a PDF file"
        nameFilters: [ "PDF files (*.pdf)" ]
        onAccepted: doc.source = selectedFile
    }

    SplitView {
        anchors.fill: parent

        Pane {
            SplitView.minimumWidth: 6
            SplitView.preferredWidth: 200
            TreeView {
                id: bookmarksTree
                anchors.fill: parent
                columnWidthProvider: function() { return width }
                onWidthChanged: forceLayout() // workaround to avoid column width getting stuck
                clip: true
                delegate: TreeViewDelegate {
                    width: parent.width
                    onClicked: image.currentFrame = pageNumber
                }
                model: PdfBookmarkModel {
                    document: root.doc
                }
                ScrollIndicator.vertical: ScrollIndicator {
                    // get the ScrollIndicator out into the margin area of the Pane...
                    // no need to overlap the tree when so much space is wasted anyway
                    parent: bookmarksTree.parent
                    anchors {
                        top: bookmarksTree.top
                        left: bookmarksTree.right
                        bottom: bookmarksTree.bottom
                    }
                }
            }
        }

        ScrollView {
            contentWidth: paper.width
            contentHeight: paper.height

            Rectangle {
                id: paper
                width: image.width
                height: image.height
                PdfPageImage {
                    id: image
                    document: doc

                    property real zoomFactor: Math.sqrt(2)

                    Shortcut {
                        sequence: StandardKey.MoveToNextPage
                        enabled: image.currentFrame < image.frameCount - 1
                        onActivated: image.currentFrame++
                    }
                    Shortcut {
                        sequence: StandardKey.MoveToPreviousPage
                        enabled: image.currentFrame > 0
                        onActivated: image.currentFrame--
                    }
                    Shortcut {
                        sequence: StandardKey.ZoomIn
                        enabled: image.sourceSize.width < 5000
                        onActivated: {
                            image.sourceSize.width = image.implicitWidth * image.zoomFactor
                            image.sourceSize.height = image.implicitHeight * image.zoomFactor
                        }
                    }
                    Shortcut {
                        sequence: StandardKey.ZoomOut
                        enabled: image.width > 50
                        onActivated: {
                            image.sourceSize.width = image.implicitWidth / image.zoomFactor
                            image.sourceSize.height = image.implicitHeight / image.zoomFactor
                        }
                    }
                    Shortcut {
                        sequence: "Ctrl+0"
                        onActivated: image.sourceSize = undefined
                    }
                    Shortcut {
                        sequence: StandardKey.Open
                        onActivated: fileDialog.open()
                    }
                    Shortcut {
                        sequence: StandardKey.Quit
                        onActivated: Qt.quit()
                    }
                }

                Repeater {
                    model: PdfLinkModel {
                        id: linkModel
                        document: doc
                        page: image.currentFrame
                    }
                    delegate: Rectangle {
                        color: "transparent"
                        border.color: "lightgrey"
                        x: rect.x
                        y: rect.y
                        width: rect.width
                        height: rect.height
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (page >= 0)
                                    image.currentFrame = page
                                else
                                    Qt.openUrlExternally(url)
                            }
                        }
                    }
                }
            }
        }
    }
    Label {
        anchors { bottom: parent.bottom; right: parent.right; margins: 6 }
        text: "page " + (image.currentFrame + 1) + " of " + doc.pageCount
    }
}
