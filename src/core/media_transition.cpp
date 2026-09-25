// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_transition.h"

#include "timeline_time.h"

#include <QColor>
#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr double kHalfPi = 1.57079632679489661923;

quint32 mixPixel(quint32 a, quint32 b, qint64 weight)
{
    const qint64 keep = MediaTransition::kWeightOne - weight;
    quint32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const qint64 channel = (qint64((a >> shift) & 0xff) * keep + qint64((b >> shift) & 0xff) * weight + 32768) >> 16;
        out |= quint32(std::clamp<qint64>(channel, 0, 255)) << shift;
    }
    return out;
}
} // namespace

int MediaTransition::frameWeight(qint64 frameStart, qint64 frameEnd, qint64 windowStart, qint64 windowDuration)
{
    if (windowDuration <= 0 || frameEnd <= frameStart) return 0;
    const auto offset = TimelineTime::subtract(frameStart, windowStart);
    const auto length = TimelineTime::subtract(frameEnd, frameStart);
    if (!offset || !length || *offset < 0 || *offset >= windowDuration) return 0;
    const auto twice = TimelineTime::add(*offset, *offset);
    const auto numerator = twice ? TimelineTime::add(*twice, *length) : std::nullopt;
    if (!numerator || windowDuration > (std::numeric_limits<qint64>::max() / 2)) return kWeightOne / 2;
    const auto weight = TimelineTime::mulDiv(*numerator, quint64(kWeightOne), quint64(windowDuration) * 2,
                                             TimeMap::NearestAway);
    return int(std::clamp<qint64>(weight.value_or(kWeightOne / 2), 0, kWeightOne));
}

QImage MediaTransition::blend(const QImage &outgoingImage, const QImage &incomingImage, const QString &type,
                              int weight, const QVariantMap &parameters)
{
    const QImage outgoing = outgoingImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage incoming = incomingImage.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                                .scaled(outgoing.size());
    QImage result(outgoing.size(), QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return result;
    result.setColorSpace(outgoing.colorSpace());
    const qint64 w = std::clamp(weight, 0, kWeightOne);
    const int width = result.width(), height = result.height();
    if (type == QLatin1String("wipe")) {
        const QString direction = parameters.value(QStringLiteral("direction"), QStringLiteral("left")).toString();
        const bool vertical = direction == QLatin1String("up") || direction == QLatin1String("down");
        const qint64 extent = vertical ? height : width;
        const int covered = int((extent * w + kWeightOne / 2) / kWeightOne);
        for (int y = 0; y < height; ++y) {
            const auto *a = reinterpret_cast<const quint32 *>(outgoing.constScanLine(y));
            const auto *b = reinterpret_cast<const quint32 *>(incoming.constScanLine(y));
            auto *out = reinterpret_cast<quint32 *>(result.scanLine(y));
            for (int x = 0; x < width; ++x) {
                bool inside = false;
                if (direction == QLatin1String("right")) inside = x >= width - covered;
                else if (direction == QLatin1String("up")) inside = y >= height - covered;
                else if (direction == QLatin1String("down")) inside = y < covered;
                else inside = x < covered;
                out[x] = inside ? b[x] : a[x];
            }
        }
        return result;
    }
    if (type == QLatin1String("dip")) {
        QColor color(parameters.value(QStringLiteral("color"), QStringLiteral("#000000")).toString());
        if (!color.isValid()) color = Qt::black;
        const quint32 solid = qPremultiply(color.rgba());
        const bool first = w < kWeightOne / 2;
        const qint64 local = first ? w * 2 : w * 2 - kWeightOne;
        for (int y = 0; y < height; ++y) {
            const auto *a = reinterpret_cast<const quint32 *>(outgoing.constScanLine(y));
            const auto *b = reinterpret_cast<const quint32 *>(incoming.constScanLine(y));
            auto *out = reinterpret_cast<quint32 *>(result.scanLine(y));
            for (int x = 0; x < width; ++x)
                out[x] = first ? mixPixel(a[x], solid, local) : mixPixel(solid, b[x], local);
        }
        return result;
    }
    for (int y = 0; y < height; ++y) {
        const auto *a = reinterpret_cast<const quint32 *>(outgoing.constScanLine(y));
        const auto *b = reinterpret_cast<const quint32 *>(incoming.constScanLine(y));
        auto *out = reinterpret_cast<quint32 *>(result.scanLine(y));
        for (int x = 0; x < width; ++x) out[x] = mixPixel(a[x], b[x], w);
    }
    return result;
}

float MediaTransition::audioGain(qint64 sample, qint64 first, qint64 count, bool incoming,
                                 const QVariantMap &parameters)
{
    if (count <= 0 || sample < first || sample - first >= count) return 1.0f;
    const double position = (double(sample - first) + 0.5) / double(count);
    if (parameters.value(QStringLiteral("curve")).toString() == QLatin1String("equalPower"))
        return float(incoming ? std::sin(position * kHalfPi) : std::cos(position * kHalfPi));
    return float(incoming ? position : 1.0 - position);
}

} // namespace QindaTK
