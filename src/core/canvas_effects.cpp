// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_effects.h"

#include <QPainter>
#include <QVector>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <utility>

namespace QindaTK::CanvasEffects {
namespace {
// out[i] is the mean of in[i - lead .. i - lead + size - 1], zero outside
// [0, count). Running sums keep each pass linear in `count`.
struct Box {
    int size;
    int lead;
};

void boxPass(const QRgb *in, QRgb *out, int count, const Box &box)
{
    int sum[4] = {0, 0, 0, 0};
    const auto take = [&](int j, int sign) {
        if (j < 0 || j >= count) return;
        const QRgb p = in[j];
        sum[0] += sign * qAlpha(p);
        sum[1] += sign * qRed(p);
        sum[2] += sign * qGreen(p);
        sum[3] += sign * qBlue(p);
    };
    for (int j = -box.lead; j < box.size - box.lead; ++j) take(j, 1);
    const int half = box.size / 2;
    for (int i = 0; i < count; ++i) {
        // Premultiplied channels never exceed alpha, and neither do their means.
        out[i] = qRgba((sum[1] + half) / box.size, (sum[2] + half) / box.size,
                       (sum[3] + half) / box.size, (sum[0] + half) / box.size);
        take(i - box.lead, -1);
        take(i - box.lead + box.size, 1);
    }
}

QVector<Box> boxes(qreal sigma)
{
    if (!std::isfinite(sigma) || sigma <= 0) return {};
    const int d = int(std::floor(sigma * 3.0 * std::sqrt(2.0 * M_PI) / 4.0 + 0.5));
    if (d < 2) return {};
    if (d % 2) return {{d, (d - 1) / 2}, {d, (d - 1) / 2}, {d, (d - 1) / 2}};
    return {{d, d / 2}, {d, d / 2 - 1}, {d + 1, d / 2}};
}
} // namespace

void gaussianBlur(QImage &image, qreal sigma)
{
    const QVector<Box> passes = boxes(sigma);
    if (passes.isEmpty() || image.isNull()) return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image.convertTo(QImage::Format_ARGB32_Premultiplied);
    const int width = image.width();
    const int height = image.height();
    const qsizetype stride = image.bytesPerLine() / qsizetype(sizeof(QRgb));
    QRgb *pixels = reinterpret_cast<QRgb *>(image.bits());
    QVector<QRgb> a(std::max(width, height));
    QVector<QRgb> b(std::max(width, height));
    for (int y = 0; y < height; ++y) {
        QRgb *row = pixels + y * stride;
        std::copy(row, row + width, a.begin());
        for (const Box &box : passes) {
            boxPass(a.constData(), b.data(), width, box);
            std::swap(a, b);
        }
        std::copy(a.cbegin(), a.cbegin() + width, row);
    }
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) a[y] = pixels[y * stride + x];
        for (const Box &box : passes) {
            boxPass(a.constData(), b.data(), height, box);
            std::swap(a, b);
        }
        for (int y = 0; y < height; ++y) pixels[y * stride + x] = a[y];
    }
}

void dropShadow(QImage &layer, QPoint offset, qreal sigma, const QColor &color)
{
    if (layer.isNull()) return;
    QImage shadow(layer.size(), QImage::Format_ARGB32_Premultiplied);
    shadow.fill(Qt::transparent);
    {
        QPainter painter(&shadow);
        painter.drawImage(offset, layer);
        // SourceIn keeps the tint where the shifted layer has alpha.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(shadow.rect(), color);
    }
    gaussianBlur(shadow, sigma);
    {
        QPainter painter(&shadow);
        painter.drawImage(QPoint(0, 0), layer);
    }
    layer = std::move(shadow);
}

void colorAdjust(QImage &layer, qreal brightness, qreal contrast, qreal saturation)
{
    if (layer.format() != QImage::Format_ARGB32_Premultiplied)
        layer = layer.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal s = saturation;
    const qreal m[3][3] = {{0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s},
                           {0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s},
                           {0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s}};
    const qreal intercept = 0.5 - 0.5 * contrast + brightness;
    const auto clamp = [](qreal v) { return std::clamp(v, 0.0, 1.0); };
    for (int y = 0; y < layer.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(layer.scanLine(y));
        for (int x = 0; x < layer.width(); ++x) {
            const QRgb straight = qUnpremultiply(line[x]);
            const int alpha = qAlpha(straight);
            if (alpha == 0) continue;
            const qreal in[3] = {qRed(straight) / 255.0, qGreen(straight) / 255.0, qBlue(straight) / 255.0};
            int out[3];
            for (int c = 0; c < 3; ++c) {
                const qreal saturated = clamp(m[c][0] * in[0] + m[c][1] * in[1] + m[c][2] * in[2]);
                out[c] = int(std::lround(clamp(saturated * contrast + intercept) * 255));
            }
            line[x] = qPremultiply(qRgba(out[0], out[1], out[2], alpha));
        }
    }
}

} // namespace QindaTK::CanvasEffects
