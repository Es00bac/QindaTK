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
    // A cursor move always repaints the crosshair; paint() regenerates the
    // slice image only when this plane's slice coordinate changed.
    if (cursor) connect(cursor, &VolumeCursor::changed, this, [this] { update(); });
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
    return m_volume ? m_volume->spacingAt(m_plane == Sagittal ? 1 : 0) : 1;
}
double ImageSliceItem::verticalSpacing() const
{
    return m_volume ? m_volume->spacingAt(m_plane == Axial ? 1 : 2) : 1;
}
double ImageSliceItem::currentSlice() const
{
    if (!m_volume) return 0;
    if (!m_cursor) {
        // Without a cursor the view shows the middle slice.
        const int count = m_plane == Axial ? m_volume->sizeZ()
            : m_plane == Coronal ? m_volume->sizeY() : m_volume->sizeX();
        return std::max(0, count - 1) / 2.0;
    }
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
    if (!m_volume || rect.isEmpty() || !rect.contains(QPointF(px, py))) return {};
    const double h = (px - rect.left()) / rect.width() * horizontalSize() - 0.5;
    const double v = (py - rect.top()) / rect.height() * verticalSize() - 0.5;
    const double slice = currentSlice();
    if (m_plane == Axial) return {h, v, slice};
    if (m_plane == Coronal) return {h, slice, v};
    return {slice, h, v};
}
QVariantList ImageSliceItem::positionForVoxel(double x, double y, double z) const
{
    const QRectF rect = targetRect();
    if (!m_volume || rect.isEmpty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return {};
    const double h = m_plane == Sagittal ? y : x;
    const double v = m_plane == Axial ? y : z;
    return {rect.left() + (h + 0.5) / horizontalSize() * rect.width(),
            rect.top() + (v + 0.5) / verticalSize() * rect.height()};
}
QRgb ImageSliceItem::colorFor(double value) const
{
    if (!std::isfinite(value)) return qRgba(0, 0, 0, 0);
    if (m_transfer) return m_transfer->colorAt(value).rgba();
    const double level = std::clamp((value - m_windowCenter) / m_windowWidth + 0.5, 0.0, 1.0);
    const int gray = int(std::lround(level * 255));
    return qRgb(gray, gray, gray);
}
QImage ImageSliceItem::sliceImage(int columns, int rows) const
{
    if (!m_volume || horizontalSize() < 1 || verticalSize() < 1) return {};
    columns = std::clamp(columns, 1, MaximumImageSide);
    rows = std::clamp(rows, 1, MaximumImageSide);
    QImage image(columns, rows, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    const double slice = currentSlice();
    // Pixel centres map to continuous voxel indices across whole cells.
    const double columnScale = double(horizontalSize()) / columns;
    const double rowScale = double(verticalSize()) / rows;
    for (int row = 0; row < rows; ++row) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(row));
        const double v = (row + 0.5) * rowScale - 0.5;
        for (int column = 0; column < columns; ++column) {
            const double h = (column + 0.5) * columnScale - 0.5;
            const double x = m_plane == Sagittal ? slice : h;
            const double y = m_plane == Sagittal ? h : m_plane == Coronal ? slice : v;
            const double z = m_plane == Axial ? slice : v;
            line[column] = qPremultiply(colorFor(
                m_volume->sampleValue(x, y, z, m_channel, m_frame, m_interpolation)));
        }
    }
    return image;
}
void ImageSliceItem::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), m_background);
    const QRectF rect = targetRect();
    if (rect.isEmpty()) return;
    // AGENT-NOTE: Nearest needs at most one sample per voxel along each axis;
    // the painter's nearest scaling of that image matches sampling every item
    // pixel (cell edges may round to the neighbouring pixel), so a magnified
    // repaint costs voxels, not view pixels. Linear
    // interpolates values before the transfer or window mapping (a painter
    // blend would interpolate colors instead), so it samples per item pixel.
    const bool nearest = m_interpolation == VoxelVolume::Nearest;
    const int pixelColumns = int(std::ceil(rect.width())), pixelRows = int(std::ceil(rect.height()));
    const int columns = std::clamp(nearest ? std::min(pixelColumns, horizontalSize()) : pixelColumns,
                                   1, MaximumImageSide);
    const int rows = std::clamp(nearest ? std::min(pixelRows, verticalSize()) : pixelRows,
                                1, MaximumImageSide);
    const double slice = currentSlice();
    if (m_dirty || slice != m_imageSlice || m_image.size() != QSize(columns, rows)) {
        m_image = sliceImage(columns, rows);
        m_imageSlice = slice;
        m_dirty = false;
    }
    if (m_image.isNull()) return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, m_interpolation == VoxelVolume::Linear);
    painter->drawImage(rect, m_image);
    if (!m_cursor || m_crosshairWidth <= 0 || m_crosshair.alpha() == 0) return;
    const QVariantList point = positionForVoxel(m_cursor->x(), m_cursor->y(), m_cursor->z());
    if (point.size() != 2) return;
    const double px = point[0].toDouble(), py = point[1].toDouble();
    QPen pen(m_crosshair);
    pen.setWidthF(m_crosshairWidth);
    painter->setPen(pen);
    painter->drawLine(QPointF(px, rect.top()), QPointF(px, rect.bottom()));
    painter->drawLine(QPointF(rect.left(), py), QPointF(rect.right(), py));
}
} // namespace QindaTK
