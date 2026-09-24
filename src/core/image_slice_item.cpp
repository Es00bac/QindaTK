// SPDX-License-Identifier: LGPL-3.0-or-later
#include "image_slice_item.h"
#include "transfer_function_model.h"
#include "volume_cursor.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace QindaTK {
ImageSliceItem::ImageSliceItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
    connect(this, &QQuickItem::widthChanged, this, &ImageSliceItem::invalidate);
    connect(this, &QQuickItem::heightChanged, this, &ImageSliceItem::invalidate);
}
void ImageSliceItem::invalidate() { m_dirty = true; update(); }
void ImageSliceItem::setVolume(VoxelVolume *volume)
{
    if (m_volume == volume) return;
    if (m_volume) disconnect(m_volume, nullptr, this, nullptr);
    m_volume = volume;
    if (volume) connect(volume, &VoxelVolume::changed, this, &ImageSliceItem::invalidate);
    invalidate(); emit changed();
}
void ImageSliceItem::setCursor(VolumeCursor *cursor)
{
    if (m_cursor == cursor) return;
    if (m_cursor) disconnect(m_cursor, nullptr, this, nullptr);
    m_cursor = cursor;
    if (cursor) connect(cursor, &VolumeCursor::changed, this, &ImageSliceItem::invalidate);
    invalidate(); emit changed();
}
void ImageSliceItem::setTransfer(TransferFunctionModel *transfer)
{
    if (m_transfer == transfer) return;
    if (m_transfer) disconnect(m_transfer, nullptr, this, nullptr);
    m_transfer = transfer;
    if (transfer) connect(transfer, &TransferFunctionModel::changed, this, &ImageSliceItem::invalidate);
    invalidate(); emit changed();
}
void ImageSliceItem::setPlane(Plane plane)
{
    if (m_plane == plane) return;
    m_plane = plane; invalidate(); emit changed();
}
void ImageSliceItem::setChannel(int channel)
{
    if (m_channel == channel) return;
    m_channel = channel; invalidate(); emit changed();
}
void ImageSliceItem::setFrame(int frame)
{
    if (m_frame == frame) return;
    m_frame = frame; invalidate(); emit changed();
}
void ImageSliceItem::setInterpolation(VoxelVolume::Interpolation interpolation)
{
    if (m_interpolation == interpolation) return;
    m_interpolation = interpolation; invalidate(); emit changed();
}
void ImageSliceItem::setWindowCenter(double value)
{
    if (!std::isfinite(value) || m_windowCenter == value) return;
    m_windowCenter = value; invalidate(); emit changed();
}
void ImageSliceItem::setWindowWidth(double value)
{
    if (!std::isfinite(value) || value <= 0 || m_windowWidth == value) return;
    m_windowWidth = value; invalidate(); emit changed();
}
void ImageSliceItem::setBackgroundColor(const QColor &color)
{
    if (!color.isValid() || m_background == color) return;
    m_background = color; update(); emit changed();
}
void ImageSliceItem::setCrosshairColor(const QColor &color)
{
    if (!color.isValid() || m_crosshair == color) return;
    m_crosshair = color; update(); emit changed();
}
void ImageSliceItem::setCrosshairWidth(double width)
{
    if (!std::isfinite(width) || width < 0 || m_crosshairWidth == width) return;
    m_crosshairWidth = width; update(); emit changed();
}
int ImageSliceItem::horizontalSize() const
{
    if (!m_volume) return 0;
    return m_plane == Sagittal ? m_volume->sizeY() : m_volume->sizeX();
}
int ImageSliceItem::verticalSize() const
{
    if (!m_volume) return 0;
    return m_plane == Axial ? m_volume->sizeY() : m_volume->sizeZ();
}
double ImageSliceItem::horizontalSpacing() const
{
    if (!m_volume) return 1;
    return m_volume->spacing()[m_plane == Sagittal ? 1 : 0].toDouble();
}
double ImageSliceItem::verticalSpacing() const
{
    if (!m_volume) return 1;
    return m_volume->spacing()[m_plane == Axial ? 1 : 2].toDouble();
}
double ImageSliceItem::currentSlice() const
{
    if (!m_cursor || !m_volume) return 0;
    return m_plane == Axial ? m_cursor->z() : m_plane == Coronal ? m_cursor->y() : m_cursor->x();
}
QRectF ImageSliceItem::targetRect() const
{
    const double physicalWidth = horizontalSize() * horizontalSpacing();
    const double physicalHeight = verticalSize() * verticalSpacing();
    if (physicalWidth <= 0 || physicalHeight <= 0 || width() <= 0 || height() <= 0) return {};
    const double scale = std::min(width() / physicalWidth, height() / physicalHeight);
    const double w = physicalWidth * scale, h = physicalHeight * scale;
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}
QVariantList ImageSliceItem::voxelAt(double px, double py) const
{
    const QRectF rect = targetRect();
    if (!m_volume || !rect.contains(QPointF(px, py))) return {};
    const double h = horizontalSize() == 1 ? 0 : (px - rect.left()) / rect.width() * (horizontalSize() - 1);
    const double v = verticalSize() == 1 ? 0 : (py - rect.top()) / rect.height() * (verticalSize() - 1);
    const double slice = currentSlice();
    if (m_plane == Axial) return {h, v, slice};
    if (m_plane == Coronal) return {h, slice, v};
    return {slice, h, v};
}
QImage ImageSliceItem::sliceImage(int columns, int rows) const
{
    if (!m_volume || horizontalSize() < 1 || verticalSize() < 1) return {};
    columns = std::clamp(columns, 1, 2048);
    rows = std::clamp(rows, 1, 2048);
    QImage image(columns, rows, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    const double slice = currentSlice();
    for (int row = 0; row < rows; ++row) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(row));
        const double v = rows == 1 ? 0 : double(row) * (verticalSize() - 1) / (rows - 1);
        for (int column = 0; column < columns; ++column) {
            const double h = columns == 1 ? 0 : double(column) * (horizontalSize() - 1) / (columns - 1);
            const double x = m_plane == Sagittal ? slice : h;
            const double y = m_plane == Sagittal ? h : m_plane == Coronal ? slice : v;
            const double z = m_plane == Axial ? slice : v;
            const double value = m_volume->sampleValue(x, y, z, m_channel, m_frame, m_interpolation);
            QColor color;
            if (!std::isfinite(value)) color = Qt::transparent;
            else if (m_transfer) color = m_transfer->colorAt(value);
            else {
                const double normalized = std::clamp((value - m_windowCenter) / m_windowWidth + 0.5, 0.0, 1.0);
                color = QColor::fromRgbF(normalized, normalized, normalized);
            }
            line[column] = qPremultiply(color.rgba());
        }
    }
    return image;
}
void ImageSliceItem::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), m_background);
    const QRectF rect = targetRect();
    if (rect.isEmpty()) return;
    const int columns = std::clamp(int(std::ceil(rect.width())), 1, 2048);
    const int rows = std::clamp(int(std::ceil(rect.height())), 1, 2048);
    if (m_dirty || m_image.size() != QSize(columns, rows)) {
        m_image = sliceImage(columns, rows);
        m_dirty = false;
    }
    if (m_image.isNull()) return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, m_interpolation == VoxelVolume::Linear);
    painter->drawImage(rect, m_image);
    if (!m_cursor || m_crosshairWidth <= 0 || m_crosshair.alpha() == 0) return;
    const double h = m_plane == Sagittal ? m_cursor->y() : m_cursor->x();
    const double v = m_plane == Axial ? m_cursor->y() : m_cursor->z();
    const double px = rect.left() + (horizontalSize() == 1 ? 0.5 : h / (horizontalSize() - 1)) * rect.width();
    const double py = rect.top() + (verticalSize() == 1 ? 0.5 : v / (verticalSize() - 1)) * rect.height();
    QPen pen(m_crosshair);
    pen.setWidthF(m_crosshairWidth);
    painter->setPen(pen);
    painter->drawLine(QPointF(px, rect.top()), QPointF(px, rect.bottom()));
    painter->drawLine(QPointF(rect.left(), py), QPointF(rect.right(), py));
}
} // namespace QindaTK
