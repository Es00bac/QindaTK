// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tiled_raster_document.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPainter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int RasterTileSize = 256;
constexpr int MaximumTouchedTiles = 64;
constexpr int MaximumStrokeStamps = 8192;
constexpr int MaximumLayers = 256;
constexpr qsizetype MaximumUndoBytes = 64 * 1024 * 1024;
constexpr qsizetype MaximumDocumentTileBytes = 128 * 1024 * 1024;
constexpr qsizetype MaximumStrokePixelWork = 16 * 1024 * 1024;

bool finite(qreal value) { return std::isfinite(value); }
}

TiledRasterDocument::TiledRasterDocument(QObject *parent) : QObject(parent) { updatePreview(); }

int TiledRasterDocument::columns() const { return (m_size.width() + RasterTileSize - 1) / RasterTileSize; }
QRect TiledRasterDocument::tileRect(int index) const
{
    const int x = (index % columns()) * RasterTileSize;
    const int y = (index / columns()) * RasterTileSize;
    return QRect(x, y, RasterTileSize, RasterTileSize).intersected(QRect(QPoint(0, 0), m_size));
}
int TiledRasterDocument::layerIndex(const QString &id) const
{
    for (int i = 0; i < m_layers.size(); ++i) if (m_layers.at(i).id == id) return i;
    return -1;
}
QStringList TiledRasterDocument::layerIds() const
{
    QStringList result;
    for (const Layer &layer : m_layers) result.append(layer.id);
    return result;
}
void TiledRasterDocument::setError(const QString &error)
{
    m_error = error;
    emit changed();
}

bool TiledRasterDocument::resizeCanvas(int width, int height)
{
    if (width < 1 || height < 1 || width > 8192 || height > 8192
        || qint64(width) * height > 16 * 1024 * 1024 || !m_layers.isEmpty() || m_strokeActive) {
        setError(QStringLiteral("Canvas size must be 1–8192 per side, at most 16 megapixels, and set before layers are created."));
        return false;
    }
    m_size = QSize(width, height);
    m_history.clear(); m_cursor = 0; m_historyBytes = 0; m_allocatedTileBytes = 0; ++m_revision;
    m_error.clear(); updatePreview(); return true;
}

bool TiledRasterDocument::createLayer(const QString &id, const QString &name)
{
    if (id.trimmed().isEmpty() || name.trimmed().isEmpty() || id.size() > 256 || name.size() > 256
        || layerIndex(id) >= 0 || m_layers.size() >= MaximumLayers || m_strokeActive) {
        setError(QStringLiteral("Layer needs a unique stable ID and a name; at most 256 layers are supported."));
        return false;
    }
    const int count = columns() * ((m_size.height() + RasterTileSize - 1) / RasterTileSize);
    Layer layer;
    layer.id = id; layer.name = name; layer.pixels.resize(count);
    m_layers.append(layer); ++m_revision; m_error.clear(); updatePreview(); return true;
}

bool TiledRasterDocument::addMask(const QString &layerId)
{
    const int index = layerIndex(layerId);
    if (index < 0 || m_strokeActive) { setError(QStringLiteral("Select an existing layer before adding its mask.")); return false; }
    if (!m_layers[index].hasMask) {
        m_layers[index].hasMask = true;
        m_layers[index].mask.resize(m_layers[index].pixels.size());
        ++m_revision; m_error.clear(); updatePreview();
    }
    return true;
}

bool TiledRasterDocument::setMaskEnabled(const QString &layerId, bool enabled)
{
    const int index = layerIndex(layerId);
    if (index < 0 || !m_layers[index].hasMask || m_strokeActive) {
        setError(QStringLiteral("Layer has no editable mask.")); return false;
    }
    if (m_layers[index].maskEnabled != enabled) {
        m_layers[index].maskEnabled = enabled; ++m_revision; m_error.clear(); updatePreview();
    }
    return true;
}

bool TiledRasterDocument::beginStroke(const QString &layerId, bool paintMask,
                                      const QColor &color, const QVariantMap &preset)
{
    const int index = layerIndex(layerId);
    if (m_strokeActive || index < 0 || !color.isValid()
        || (paintMask && !m_layers.at(index).hasMask)) {
        setError(QStringLiteral("Stroke needs an idle document, a valid layer and color, and a mask when mask painting is selected."));
        return false;
    }
    Brush brush;
    brush.diameter = preset.value(QStringLiteral("diameter"), brush.diameter).toReal();
    brush.hardness = preset.value(QStringLiteral("hardness"), brush.hardness).toReal();
    brush.spacing = preset.value(QStringLiteral("spacing"), brush.spacing).toReal();
    brush.opacity = preset.value(QStringLiteral("opacity"), brush.opacity).toReal();
    brush.pressureSize = preset.value(QStringLiteral("pressureSize"), brush.pressureSize).toReal();
    brush.pressureOpacity = preset.value(QStringLiteral("pressureOpacity"), brush.pressureOpacity).toReal();
    if (!finite(brush.diameter) || !finite(brush.hardness) || !finite(brush.spacing)
        || !finite(brush.opacity) || !finite(brush.pressureSize) || !finite(brush.pressureOpacity)
        || brush.diameter < 1 || brush.diameter > 512 || brush.hardness < 0 || brush.hardness > 1
        || brush.spacing < 0.05 || brush.spacing > 1 || brush.opacity < 0 || brush.opacity > 1
        || brush.pressureSize < 0 || brush.pressureSize > 1 || brush.pressureOpacity < 0 || brush.pressureOpacity > 1) {
        setError(QStringLiteral("Brush preset values are outside their supported finite ranges.")); return false;
    }
    m_strokeActive = true; m_strokeMask = paintMask; m_strokeLayer = index;
    m_strokeColor = color; m_brush = brush; m_strokeBefore.clear(); m_strokeError.clear();
    m_strokeStampCount = 0; m_strokePixelWork = 0;
    m_hasPrevious = false; m_error.clear(); emit changed(); return true;
}

QImage &TiledRasterDocument::writableTile(int layer, int tile, bool mask)
{
    QVector<QImage> &tiles = mask ? m_layers[layer].mask : m_layers[layer].pixels;
    if (tiles.at(tile).isNull()) {
        const QSize size = tileRect(tile).size();
        tiles[tile] = QImage(size, mask ? QImage::Format_Grayscale8 : QImage::Format_ARGB32_Premultiplied);
        tiles[tile].fill(mask ? 255 : 0);
        m_allocatedTileBytes += tiles[tile].sizeInBytes();
    }
    return tiles[tile];
}

bool TiledRasterDocument::rememberBefore(int tileIndex)
{
    if (m_strokeBefore.contains(tileIndex)) return true;
    if (m_strokeBefore.size() >= MaximumTouchedTiles) {
        m_strokeError = QStringLiteral("Stroke exceeded the 64-tile transaction bound and was canceled.");
        return false;
    }
    const QVector<QImage> &tiles = m_strokeMask ? m_layers[m_strokeLayer].mask : m_layers[m_strokeLayer].pixels;
    m_strokeBefore.insert(tileIndex, tiles.at(tileIndex));
    return true;
}

void TiledRasterDocument::paintTile(QImage &tile, int tileIndex, const Sample &sample,
                                    qreal diameter, qreal opacity, qreal flatten, qreal angle)
{
    const QRect rect = tileRect(tileIndex);
    const qreal rx = diameter * 0.5;
    const qreal ry = qMax<qreal>(0.5, rx * (1.0 - flatten));
    const qreal cosine = std::cos(angle), sine = std::sin(angle);
    const qreal edge = qMax<qreal>(1.0, rx + 1.0);
    const int left = qMax(rect.left(), int(std::floor(sample.point.x() - edge)));
    const int right = qMin(rect.right(), int(std::ceil(sample.point.x() + edge)));
    const int top = qMax(rect.top(), int(std::floor(sample.point.y() - edge)));
    const int bottom = qMin(rect.bottom(), int(std::ceil(sample.point.y() + edge)));
    const QColor source = m_strokeColor.toRgb();
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const qreal dx = x + 0.5 - sample.point.x(), dy = y + 0.5 - sample.point.y();
            const qreal localX = (dx * cosine + dy * sine) / rx;
            const qreal localY = (-dx * sine + dy * cosine) / ry;
            const qreal distance = std::sqrt(localX * localX + localY * localY);
            if (distance >= 1.0) continue;
            const qreal edgeCoverage = distance <= m_brush.hardness || m_brush.hardness >= 1
                ? 1.0 : (1.0 - distance) / qMax<qreal>(0.0001, 1.0 - m_brush.hardness);
            const qreal alpha = qBound<qreal>(0, opacity * edgeCoverage * source.alphaF(), 1);
            if (alpha <= 0) continue;
            const int localXPixel = x - rect.x(), localYPixel = y - rect.y();
            if (m_strokeMask) {
                uchar *row = tile.scanLine(localYPixel);
                const qreal previous = row[localXPixel] / 255.0;
                row[localXPixel] = uchar(qBound(0, qRound(source.redF() * alpha * 255 + previous * (1 - alpha) * 255), 255));
            } else {
                QRgb *row = reinterpret_cast<QRgb *>(tile.scanLine(localYPixel));
                const QRgb previous = row[localXPixel];
                const qreal inverse = 1.0 - alpha;
                const int a = qRound(alpha * 255 + qAlpha(previous) * inverse);
                const int red = qRound(source.redF() * alpha * 255 + qRed(previous) * inverse);
                const int green = qRound(source.greenF() * alpha * 255 + qGreen(previous) * inverse);
                const int blue = qRound(source.blueF() * alpha * 255 + qBlue(previous) * inverse);
                row[localXPixel] = qRgba(qMin(red, a), qMin(green, a), qMin(blue, a), a);
            }
        }
    }
}

void TiledRasterDocument::stamp(const Sample &sample)
{
    const qreal diameter = qMax<qreal>(1, m_brush.diameter * (1 - m_brush.pressureSize + m_brush.pressureSize * sample.pressure));
    const qreal opacity = m_brush.opacity * (1 - m_brush.pressureOpacity + m_brush.pressureOpacity * sample.pressure);
    const qreal tilt = qMin<qreal>(1, std::hypot(sample.tiltX, sample.tiltY) / 90.0);
    const qreal flatten = tilt * 0.65;
    const qreal angle = std::atan2(sample.tiltY, sample.tiltX);
    const qsizetype side = qsizetype(std::ceil(diameter + 2));
    m_strokePixelWork += side * side;
    if (++m_strokeStampCount > MaximumStrokeStamps || m_strokePixelWork > MaximumStrokePixelWork) {
        m_strokeError = QStringLiteral("Stroke exceeded its bounded brush-work budget and was canceled.");
        return;
    }
    const qreal radius = diameter * 0.5 + 1;
    const QRect stampRect(int(std::floor(sample.point.x() - radius)), int(std::floor(sample.point.y() - radius)),
                          int(std::ceil(diameter + 2)), int(std::ceil(diameter + 2)));
    const QRect bounds = stampRect.intersected(QRect(QPoint(0, 0), m_size));
    if (bounds.isEmpty()) return;
    const int minColumn = bounds.left() / RasterTileSize, maxColumn = bounds.right() / RasterTileSize;
    const int minRow = bounds.top() / RasterTileSize, maxRow = bounds.bottom() / RasterTileSize;
    for (int row = minRow; row <= maxRow; ++row) {
        for (int column = minColumn; column <= maxColumn; ++column) {
            const int index = row * columns() + column;
            if (!rememberBefore(index)) return;
            const QVector<QImage> &tiles = m_strokeMask ? m_layers[m_strokeLayer].mask : m_layers[m_strokeLayer].pixels;
            const QSize size = tileRect(index).size();
            const qsizetype tileBytes = qsizetype(size.width()) * size.height() * (m_strokeMask ? 1 : 4);
            if (tiles.at(index).isNull() && m_allocatedTileBytes + tileBytes > MaximumDocumentTileBytes) {
                m_strokeError = QStringLiteral("Raster tile storage exceeded the 128 MiB document bound; stroke canceled.");
                return;
            }
            QImage &tile = writableTile(m_strokeLayer, index, m_strokeMask);
            paintTile(tile, index, sample, diameter, opacity, flatten, angle);
        }
    }
}

bool TiledRasterDocument::sampleStroke(qreal x, qreal y, qreal pressure, qreal tiltX, qreal tiltY)
{
    if (!m_strokeActive || !finite(x) || !finite(y) || x < -512 || y < -512
        || x > m_size.width() + 512 || y > m_size.height() + 512
        || !finite(pressure) || !finite(tiltX) || !finite(tiltY)
        || pressure < 0 || pressure > 1 || tiltX < -90 || tiltX > 90 || tiltY < -90 || tiltY > 90) {
        const QString reason = QStringLiteral("Stroke sample requires an active stroke, canvas-bounded coordinates, pressure [0,1] and tilt [-90,90] degrees.");
        if (m_strokeActive) { m_strokeError = reason; cancelStroke(); }
        setError(reason); return false;
    }
    const Sample current{QPointF(x, y), pressure, tiltX, tiltY};
    if (!m_hasPrevious) stamp(current);
    else {
        const qreal dx = current.point.x() - m_previous.point.x();
        const qreal dy = current.point.y() - m_previous.point.y();
        const qreal distance = std::hypot(dx, dy);
        const qreal step = qMax<qreal>(1, m_brush.diameter * m_brush.spacing * 0.5);
        const int samples = qMax(1, int(std::ceil(distance / step)));
        for (int i = 1; i <= samples && m_strokeError.isEmpty(); ++i) {
            const qreal t = qreal(i) / samples;
            stamp({QPointF(m_previous.point.x() + dx * t, m_previous.point.y() + dy * t),
                   m_previous.pressure + (current.pressure - m_previous.pressure) * t,
                   m_previous.tiltX + (current.tiltX - m_previous.tiltX) * t,
                   m_previous.tiltY + (current.tiltY - m_previous.tiltY) * t});
        }
    }
    if (!m_strokeError.isEmpty()) { const QString reason = m_strokeError; cancelStroke(); setError(reason); return false; }
    m_previous = current; m_hasPrevious = true; updatePreview(); return true;
}

void TiledRasterDocument::applyEdit(const StrokeEdit &edit, bool forward)
{
    const int layer = layerIndex(edit.layerId);
    if (layer < 0) return;
    QVector<QImage> &target = edit.mask ? m_layers[layer].mask : m_layers[layer].pixels;
    for (const TileChange &change : edit.tiles) {
        m_allocatedTileBytes -= target[change.index].sizeInBytes();
        target[change.index] = forward ? change.after : change.before;
        m_allocatedTileBytes += target[change.index].sizeInBytes();
    }
}

bool TiledRasterDocument::endStroke()
{
    if (!m_strokeActive) { setError(QStringLiteral("No stroke is active.")); return false; }
    StrokeEdit edit; edit.layerId = m_layers.at(m_strokeLayer).id; edit.mask = m_strokeMask;
    QList<int> indices = m_strokeBefore.keys(); std::sort(indices.begin(), indices.end());
    const QVector<QImage> &afterTiles = edit.mask ? m_layers[m_strokeLayer].mask : m_layers[m_strokeLayer].pixels;
    for (int index : indices) {
        const QImage before = m_strokeBefore.value(index), after = afterTiles.at(index);
        if (before == after) continue;
        edit.tiles.append({index, before, after});
        edit.byteCost += before.sizeInBytes() + after.sizeInBytes();
    }
    m_strokeActive = false; m_hasPrevious = false; m_strokeBefore.clear();
    if (edit.tiles.isEmpty()) { m_error.clear(); emit changed(); return true; }
    while (m_history.size() > m_cursor) { m_historyBytes -= m_history.last().byteCost; m_history.removeLast(); }
    m_history.append(edit); m_historyBytes += edit.byteCost; ++m_cursor;
    while (m_historyBytes > MaximumUndoBytes && m_history.size() > 1) {
        m_historyBytes -= m_history.first().byteCost; m_history.removeFirst(); --m_cursor;
    }
    ++m_revision; m_error.clear(); updatePreview(); return true;
}

void TiledRasterDocument::cancelStroke()
{
    if (!m_strokeActive) return;
    const QString reason = m_strokeError;
    for (auto it = m_strokeBefore.cbegin(); it != m_strokeBefore.cend(); ++it) {
        QVector<QImage> &tiles = m_strokeMask ? m_layers[m_strokeLayer].mask : m_layers[m_strokeLayer].pixels;
        m_allocatedTileBytes -= tiles[it.key()].sizeInBytes();
        tiles[it.key()] = it.value();
        m_allocatedTileBytes += tiles[it.key()].sizeInBytes();
    }
    m_strokeActive = false; m_hasPrevious = false; m_strokeBefore.clear(); m_strokeError.clear();
    m_strokeStampCount = 0; m_strokePixelWork = 0;
    m_error = reason; updatePreview();
}

bool TiledRasterDocument::undo()
{
    if (!canUndo() || m_strokeActive) return false;
    applyEdit(m_history.at(int(--m_cursor)), false); ++m_revision; m_error.clear(); updatePreview(); return true;
}
bool TiledRasterDocument::redo()
{
    if (!canRedo() || m_strokeActive) return false;
    applyEdit(m_history.at(int(m_cursor++)), true); ++m_revision; m_error.clear(); updatePreview(); return true;
}

QImage TiledRasterDocument::composite() const
{
    QImage output(m_size, QImage::Format_ARGB32_Premultiplied); output.fill(Qt::transparent);
    QPainter painter(&output); painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (const Layer &layer : m_layers) {
        if (!layer.visible) continue;
        for (int index = 0; index < layer.pixels.size(); ++index) {
            const QImage &source = layer.pixels.at(index);
            if (source.isNull()) continue;
            QImage tile = source;
            if (layer.hasMask && layer.maskEnabled) {
                const QImage &mask = layer.mask.at(index);
                if (!mask.isNull()) {
                    tile = source.copy();
                    for (int y = 0; y < tile.height(); ++y) {
                        const uchar *maskRow = mask.constScanLine(y);
                        QRgb *row = reinterpret_cast<QRgb *>(tile.scanLine(y));
                        for (int x = 0; x < tile.width(); ++x) {
                            const int alpha = maskRow[x]; const QRgb pixel = row[x];
                            row[x] = qRgba(qRed(pixel) * alpha / 255, qGreen(pixel) * alpha / 255,
                                           qBlue(pixel) * alpha / 255, qAlpha(pixel) * alpha / 255);
                        }
                    }
                }
            }
            painter.drawImage(tileRect(index).topLeft(), tile);
        }
    }
    painter.end(); return output;
}

QString TiledRasterDocument::pngDataUrl(const QImage &image)
{
    QByteArray data; QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(data.toBase64());
}
void TiledRasterDocument::updatePreview()
{
    m_imageDataUrl = pngDataUrl(composite()); emit changed();
}
bool TiledRasterDocument::exportPng(const QString &path)
{
    const QString output = path.trimmed();
    if (output.isEmpty() || !QDir().mkpath(QFileInfo(output).absolutePath())) {
        setError(QStringLiteral("PNG export needs a writable file path.")); return false;
    }
    QSaveFile file(output); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !composite().save(&file, "PNG") || !file.commit()) {
        setError(QStringLiteral("PNG export failed: %1").arg(file.errorString())); return false;
    }
    m_error.clear(); emit changed(); return true;
}
QVariantMap TiledRasterDocument::layerInfo(const QString &layerId) const
{
    const int index = layerIndex(layerId); if (index < 0) return {};
    const Layer &layer = m_layers.at(index);
    return {{QStringLiteral("id"), layer.id}, {QStringLiteral("name"), layer.name},
            {QStringLiteral("hasMask"), layer.hasMask}, {QStringLiteral("maskEnabled"), layer.maskEnabled},
            {QStringLiteral("visible"), layer.visible}};
}

} // namespace QindaTK
