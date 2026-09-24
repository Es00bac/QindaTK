// SPDX-License-Identifier: LGPL-3.0-or-later
#include "image_compare_item.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qint64 kMaximumPixels = 16 * 1024 * 1024;

QImage normalized(QImage image)
{
    const QColorSpace srgb(QColorSpace::SRgb);
    if (image.colorSpace().isValid()) image.convertToColorSpace(srgb);
    else image.setColorSpace(srgb);
    return image.convertToFormat(QImage::Format_RGBA8888);
}
}

ImageCompareItem::ImageCompareItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
}

QImage ImageCompareItem::read(const QUrl &url)
{
    const QString text = url.toString();
    constexpr auto prefix = "data:image/png;base64,";
    if (text.startsWith(QLatin1String(prefix))) {
        const QByteArray encoded = text.mid(QLatin1String(prefix).size()).toLatin1();
        if (encoded.size() > 32 * 1024 * 1024) return {};
        QByteArray bytes = QByteArray::fromBase64(encoded);
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::ReadOnly)) return {};
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (size.isEmpty() || qint64(size.width()) * size.height() > kMaximumPixels) return {};
        return reader.read();
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

void ImageCompareItem::reload()
{
    if (m_beforeSource.isEmpty() || m_afterSource.isEmpty()) {
        m_before = {}; m_after = {}; m_difference = {}; m_error.clear();
        emit sourcesChanged(); update(); return;
    }
    setImages(read(m_beforeSource), read(m_afterSource));
}

bool ImageCompareItem::setImages(const QImage &before, const QImage &after)
{
    m_before = {}; m_after = {}; m_difference = {};
    if (before.isNull() || after.isNull())
        m_error = QStringLiteral("Both comparison images must load");
    else if (before.size() != after.size())
        m_error = QStringLiteral("Comparison images must have the same dimensions");
    else if (qint64(before.width()) * before.height() > kMaximumPixels)
        m_error = QStringLiteral("Comparison images exceed the 16-megapixel bound");
    else {
        m_before = normalized(before);
        m_after = normalized(after);
        m_difference = difference();
        m_error.clear();
    }
    emit sourcesChanged();
    update();
    return m_error.isEmpty();
}

QImage ImageCompareItem::difference() const
{
    QImage delta(m_before.size(), QImage::Format_RGBA8888);
    delta.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < delta.height(); ++y) {
        const auto *left = reinterpret_cast<const uchar *>(m_before.constScanLine(y));
        const auto *right = reinterpret_cast<const uchar *>(m_after.constScanLine(y));
        auto *out = reinterpret_cast<uchar *>(delta.scanLine(y));
        for (int x = 0; x < delta.width(); ++x) {
            const int offset = x * 4;
            // AGENT-CONTRACT: this is an encoded-sRGB visual difference,
            // not a perceptual or print-proof color metric.
            for (int channel = 0; channel < 3; ++channel)
                out[offset + channel] = uchar(std::abs(int(left[offset + channel]) - int(right[offset + channel])));
            out[offset + 3] = 255;
        }
    }
    return delta;
}

void ImageCompareItem::setBeforeSource(const QUrl &source)
{
    if (m_beforeSource == source) return;
    m_beforeSource = source;
    reload();
}

void ImageCompareItem::setAfterSource(const QUrl &source)
{
    if (m_afterSource == source) return;
    m_afterSource = source;
    reload();
}

void ImageCompareItem::setMode(const QString &mode)
{
    if ((mode != QLatin1String("split") && mode != QLatin1String("blink")
         && mode != QLatin1String("difference")) || m_mode == mode) return;
    m_mode = mode; emit viewChanged(); update();
}

void ImageCompareItem::setSplit(qreal value)
{
    value = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(m_split, value)) return;
    m_split = value; emit viewChanged(); update();
}

void ImageCompareItem::setShowAfter(bool value)
{
    if (m_showAfter == value) return;
    m_showAfter = value; emit viewChanged(); update();
}

void ImageCompareItem::setZoom(qreal value)
{
    value = std::clamp(value, 0.1, 16.0);
    if (qFuzzyCompare(m_zoom, value)) return;
    m_zoom = value; emit viewChanged(); update();
}

void ImageCompareItem::setPanX(qreal value)
{
    if (qFuzzyCompare(m_panX, value)) return;
    m_panX = value; emit viewChanged(); update();
}

void ImageCompareItem::setPanY(qreal value)
{
    if (qFuzzyCompare(m_panY, value)) return;
    m_panY = value; emit viewChanged(); update();
}

QImage ImageCompareItem::renderImage(const QSize &size) const
{
    if (m_before.isNull() || size.isEmpty() || qint64(size.width()) * size.height() > kMaximumPixels)
        return {};
    QImage frame(size, QImage::Format_RGBA8888);
    frame.setColorSpace(QColorSpace(QColorSpace::SRgb));
    frame.fill(Qt::transparent);
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal scale = std::min(qreal(size.width()) / m_before.width(),
                                 qreal(size.height()) / m_before.height()) * m_zoom;
    const QSizeF drawn(m_before.width() * scale, m_before.height() * scale);
    const QRectF target((size.width() - drawn.width()) / 2 + m_panX,
                        (size.height() - drawn.height()) / 2 + m_panY,
                        drawn.width(), drawn.height());
    if (m_mode == QLatin1String("difference")) painter.drawImage(target, m_difference);
    else if (m_mode == QLatin1String("blink"))
        painter.drawImage(target, m_showAfter ? m_after : m_before);
    else {
        painter.drawImage(target, m_before);
        painter.save();
        painter.setClipRect(QRectF(size.width() * m_split, 0,
                                   size.width() * (1 - m_split), size.height()));
        painter.drawImage(target, m_after);
        painter.restore();
    }
    return frame;
}

void ImageCompareItem::paint(QPainter *painter)
{
    const QImage frame = renderImage(QSize(std::lround(width()), std::lround(height())));
    if (!frame.isNull()) painter->drawImage(QPointF(0, 0), frame);
}

} // namespace QindaTK
