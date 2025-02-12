/****************************************************************************
**
** Copyright (C) 2017 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Tobias König <tobias.koenig@kdab.com>
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

#ifndef QPDFVIEW_P_H
#define QPDFVIEW_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qpdfview.h"

#include <QHash>
#include <QPointer>

QT_BEGIN_NAMESPACE

class QPdfPageRenderer;

class QPdfViewPrivate
{
    Q_DECLARE_PUBLIC(QPdfView)

public:
    QPdfViewPrivate(QPdfView *q);
    void init();

    void documentStatusChanged();
    void currentPageChanged(int currentPage);
    void calculateViewport();
    void setViewport(QRect viewport);
    void updateScrollBars();

    void pageRendered(int pageNumber, QSize imageSize, const QImage &image, quint64 requestId);
    void invalidateDocumentLayout();
    void invalidatePageCache();

    qreal yPositionForPage(int page) const;

    struct DocumentLayout
    {
        QSize documentSize;
        QHash<int, QRect> pageGeometries;
    };

    DocumentLayout calculateDocumentLayout() const;
    void updateDocumentLayout();

    QPdfView *q_ptr;
    QPointer<QPdfDocument> m_document;
    QPdfNavigationStack* m_pageNavigation;
    QPdfPageRenderer *m_pageRenderer;

    QPdfView::PageMode m_pageMode;
    QPdfView::ZoomMode m_zoomMode;
    qreal m_zoomFactor;

    int m_pageSpacing;
    QMargins m_documentMargins;

    bool m_blockPageScrolling;

    QMetaObject::Connection m_documentStatusChangedConnection;

    QRect m_viewport;

    QHash<int, QImage> m_pageCache;
    QList<int> m_cachedPagesLRU;
    int m_pageCacheLimit;

    DocumentLayout m_documentLayout;

    qreal m_screenResolution; // pixels per point
};

Q_DECLARE_TYPEINFO(QPdfViewPrivate::DocumentLayout, Q_RELOCATABLE_TYPE);

QT_END_NAMESPACE

#endif // QPDFVIEW_P_H
