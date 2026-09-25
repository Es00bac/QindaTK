// SPDX-License-Identifier: LGPL-3.0-or-later
#include "print_sheet_view.h"

#include <QPainter>
#include <QQuickWindow>
#include <QtMath>

namespace QindaTK {

PrintSheetView::PrintSheetView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void PrintSheetView::setAdapter(PrintAdapter *adapter)
{
    if (m_adapter == adapter) return;
    disconnect(m_invalidated);
    m_adapter = adapter;
    if (adapter)
        m_invalidated = connect(adapter, &PrintAdapter::previewInvalidated, this, [this] { polish(); });
    emit adapterChanged();
    polish();
}

void PrintSheetView::setPageIndex(int page)
{
    if (m_page == page) return;
    m_page = page;
    emit pageIndexChanged();
    polish();
}

void PrintSheetView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) polish();
}

QRectF PrintSheetView::sheetRect() const
{
    if (m_sheetSize.isEmpty() || width() <= 0 || height() <= 0) return {};
    const QSizeF fitted = m_sheetSize.scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
}

void PrintSheetView::updatePolish()
{
    QImage image;
    QSizeF size;
    bool selected = false;
    double zoom = 0;
    const QVariantMap sheet = m_adapter ? m_adapter->sheetForPage(m_page) : QVariantMap();
    if (!sheet.isEmpty() && width() > 0 && height() > 0) {
        size = QSizeF(sheet.value(QStringLiteral("widthPt")).toDouble(), sheet.value(QStringLiteral("heightPt")).toDouble());
        selected = sheet.value(QStringLiteral("selected")).toBool();
        const double ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
        const QSizeF pixels = size * (Print::DeviceDpi / 72.0);
        // AGENT-NOTE: zoom snaps to 1/64 steps so resizing reuses cached
        // images instead of filling the bounded cache with near-duplicates.
        zoom = qMin(width() / pixels.width(), height() / pixels.height()) * ratio;
        zoom = qBound(1.0 / 64, qFloor(zoom * 64) / 64.0, 8.0);
        image = m_adapter->previewImage(m_page, zoom);
    }
    const bool changed = size != m_sheetSize || selected != m_selected || zoom != m_zoom
        || image.isNull() != m_image.isNull();
    m_image = image;
    m_sheetSize = size;
    m_selected = selected;
    m_zoom = zoom;
    if (changed) emit sheetChanged();
    update();
}

void PrintSheetView::paint(QPainter *painter)
{
    const QRectF target = sheetRect();
    if (m_image.isNull() || target.isEmpty()) return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(target, m_image);
}

} // namespace QindaTK
