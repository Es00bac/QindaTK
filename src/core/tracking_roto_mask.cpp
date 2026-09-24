// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tracking_roto_model.h"

#include <QColorSpace>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <iterator>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qint64 kMaxMaskPixels = 16 * 1024 * 1024;
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool finite(double v) { return std::isfinite(v); }
}

bool TrackingRotoModel::parsePath(const QVariantList &value, QVector<PathPoint> *points)
{
    if (value.size() < 3 || value.size() > 256) return false;
    QVector<PathPoint> parsed;
    QSet<QString> ids;
    for (const QVariant &entry : value) {
        const QVariantMap item = entry.toMap();
        QString id = item.value(QStringLiteral("pointId")).toString();
        if (id.isEmpty()) id = freshId();
        const double x = item.value(QStringLiteral("x")).toDouble();
        const double y = item.value(QStringLiteral("y")).toDouble();
        if (ids.contains(id) || !finite(x) || !finite(y)
            || x < 0 || x > 1 || y < 0 || y > 1) return false;
        ids.insert(id);
        parsed.append({id, x, y});
    }
    *points = parsed;
    return true;
}

QString TrackingRotoModel::addMask(const QString &label)
{
    if (m_masks.size() >= 128) {
        setError(QStringLiteral("Mask limit is 128."));
        return {};
    }
    remember();
    Mask mask;
    mask.id = freshId();
    mask.label = label.trimmed().left(128);
    m_masks.append(mask);
    emit changed();
    return mask.id;
}

bool TrackingRotoModel::removeMask(const QString &maskId)
{
    const int index = maskIndex(maskId);
    if (index < 0) return false;
    remember();
    m_masks.removeAt(index);
    emit changed();
    return true;
}

bool TrackingRotoModel::setMaskKeyframe(const QString &maskId, int frame,
                                        const QVariantList &points)
{
    const int index = maskIndex(maskId);
    QVector<PathPoint> parsed;
    if (index < 0 || frame < 0 || m_masks[index].keys.size() >= 512
        || !parsePath(points, &parsed)) {
        setError(QStringLiteral("Mask keyframe needs 3–256 unique finite points and a valid frame."));
        return false;
    }
    remember();
    m_masks[index].keys.insert(frame, parsed);
    setError({});
    emit changed();
    return true;
}

bool TrackingRotoModel::removeMaskKeyframe(const QString &maskId, int frame)
{
    const int index = maskIndex(maskId);
    if (index < 0 || !m_masks[index].keys.contains(frame)) return false;
    remember();
    m_masks[index].keys.remove(frame);
    emit changed();
    return true;
}

bool TrackingRotoModel::setMaskFeather(const QString &maskId, double pixels)
{
    const int index = maskIndex(maskId);
    if (index < 0 || !finite(pixels) || pixels < 0 || pixels > 64) return false;
    remember();
    m_masks[index].feather = pixels;
    emit changed();
    return true;
}

QVariantMap TrackingRotoModel::evaluateMask(const QString &maskId, int frame) const
{
    const int index = maskIndex(maskId);
    if (index < 0 || frame < 0 || m_masks[index].keys.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Mask or keyframe is unavailable.")}};
    const Mask &mask = m_masks[index];
    auto upper = mask.keys.upperBound(frame);
    if (upper == mask.keys.begin()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Mask has no key at or before this frame.")}};
    }
    const auto lower = std::prev(upper);
    QVector<PathPoint> result = lower.value();
    QString interpolation = QStringLiteral("key");
    if (lower.key() != frame) {
        interpolation = QStringLiteral("held");
        if (upper != mask.keys.end()) {
            const QVector<PathPoint> &next = upper.value();
            bool sameTopology = result.size() == next.size();
            for (int i = 0; sameTopology && i < result.size(); ++i)
                sameTopology = result[i].id == next[i].id;
            if (sameTopology) {
                const double t = double(frame - lower.key()) / double(upper.key() - lower.key());
                for (int i = 0; i < result.size(); ++i) {
                    result[i].x += t * (next[i].x - result[i].x);
                    result[i].y += t * (next[i].y - result[i].y);
                }
                interpolation = QStringLiteral("linear");
            } else interpolation = QStringLiteral("heldTopology");
        }
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("maskId"), maskId},
            {QStringLiteral("frame"), frame}, {QStringLiteral("points"), pathList(result)},
            {QStringLiteral("feather"), mask.feather},
            {QStringLiteral("interpolation"), interpolation}};
}

QVariantMap TrackingRotoModel::previewTrackedMask(const QString &maskId, int sourceFrame,
                                                   int targetFrame, const QString &trackId) const
{
    const int index = trackIndex(trackId);
    const QVariantMap shape = evaluateMask(maskId, sourceFrame);
    if (index < 0 || !shape.value(QStringLiteral("ok")).toBool())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Track or source mask is unavailable.")}};
    const Track &track = m_tracks[index];
    const QImage reference = m_frames.value(track.referenceFrame);
    if (reference.isNull())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Rebind source frames before mask propagation.")}};
    if (!track.points.contains(sourceFrame) || !track.points.contains(targetFrame)
        || track.points.value(sourceFrame).state == QLatin1String("lost")
        || track.points.value(targetFrame).state == QLatin1String("lost"))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Accepted tracking points are unavailable or lost.")}};
    const double dx = (track.points.value(targetFrame).x - track.points.value(sourceFrame).x)
        / reference.width();
    const double dy = (track.points.value(targetFrame).y - track.points.value(sourceFrame).y)
        / reference.height();
    QVariantList points = shape.value(QStringLiteral("points")).toList();
    for (QVariant &entry : points) {
        QVariantMap point = entry.toMap();
        point[QStringLiteral("x")] = std::clamp(point.value(QStringLiteral("x")).toDouble() + dx, 0.0, 1.0);
        point[QStringLiteral("y")] = std::clamp(point.value(QStringLiteral("y")).toDouble() + dy, 0.0, 1.0);
        entry = point;
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("maskId"), maskId},
            {QStringLiteral("trackId"), trackId}, {QStringLiteral("frame"), targetFrame},
            {QStringLiteral("points"), points}, {QStringLiteral("state"), QStringLiteral("preview")}};
}

QImage TrackingRotoModel::applyMask(const QString &maskId, int frame, const QImage &source)
{
    if (source.isNull() || qint64(source.width()) * source.height() > kMaxMaskPixels) {
        setError(QStringLiteral("Mask source needs at most 16 megapixels."));
        return {};
    }
    const QVariantMap shape = evaluateMask(maskId, frame);
    if (!shape.value(QStringLiteral("ok")).toBool()) {
        setError(shape.value(QStringLiteral("error")).toString());
        return {};
    }
    const QVariantList points = shape.value(QStringLiteral("points")).toList();
    QPainterPath path;
    path.moveTo(points.first().toMap().value(QStringLiteral("x")).toDouble() * source.width(),
                points.first().toMap().value(QStringLiteral("y")).toDouble() * source.height());
    for (int i = 1; i < points.size(); ++i) {
        const QVariantMap point = points.at(i).toMap();
        path.lineTo(point.value(QStringLiteral("x")).toDouble() * source.width(),
                    point.value(QStringLiteral("y")).toDouble() * source.height());
    }
    path.closeSubpath();

    QImage painted(source.size(), QImage::Format_ARGB32_Premultiplied);
    painted.fill(Qt::black);
    {
        QPainter painter(&painted);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(path, Qt::white);
    }
    QImage alpha = painted.convertToFormat(QImage::Format_Grayscale8);
    const int radius = qRound(shape.value(QStringLiteral("feather")).toDouble());
    // AGENT-NOTE: separable box blur gives a bounded, deterministic feather
    // while keeping mask evaluation independent of any app image pipeline.
    if (radius > 0) {
        QImage horizontal(alpha.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < alpha.height(); ++y) {
            const auto *src = alpha.constScanLine(y);
            auto *dst = horizontal.scanLine(y);
            int sum = 0;
            for (int x = -radius; x <= radius; ++x)
                if (x >= 0 && x < alpha.width()) sum += src[x];
            for (int x = 0; x < alpha.width(); ++x) {
                dst[x] = uchar(sum / (2 * radius + 1));
                const int leave = x - radius;
                const int enter = x + radius + 1;
                if (leave >= 0) sum -= src[leave];
                if (enter < alpha.width()) sum += src[enter];
            }
        }
        for (int x = 0; x < alpha.width(); ++x) {
            int sum = 0;
            for (int y = -radius; y <= radius; ++y)
                if (y >= 0 && y < alpha.height()) sum += horizontal.constScanLine(y)[x];
            for (int y = 0; y < alpha.height(); ++y) {
                alpha.scanLine(y)[x] = uchar(sum / (2 * radius + 1));
                const int leave = y - radius;
                const int enter = y + radius + 1;
                if (leave >= 0) sum -= horizontal.constScanLine(leave)[x];
                if (enter < alpha.height()) sum += horizontal.constScanLine(enter)[x];
            }
        }
    }
    QImage output = source.colorSpace().isValid()
        ? source.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32_Premultiplied)
        : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    output.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < output.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(output.scanLine(y));
        const auto *mask = alpha.constScanLine(y);
        for (int x = 0; x < output.width(); ++x) {
            const int a = mask[x];
            const QRgb p = row[x];
            row[x] = qRgba(qRed(p) * a / 255, qGreen(p) * a / 255,
                           qBlue(p) * a / 255, qAlpha(p) * a / 255);
        }
    }
    setError({});
    return output;
}

bool TrackingRotoModel::exportMaskedPng(const QString &maskId, int frame, const QUrl &url)
{
    const QImage source = m_frames.value(frame);
    if (!url.isLocalFile() || source.isNull()) {
        setError(QStringLiteral("Export needs a loaded frame and local PNG path."));
        return false;
    }
    // Tracking frames are grayscale proxies; hosts should call applyMask on
    // their full-color decoded image for production composition.
    const QImage output = applyMask(maskId, frame, source);
    if (output.isNull()) return false;
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly)) { setError(file.errorString()); return false; }
    if (!output.save(&file, "PNG") || !file.commit()) {
        setError(file.errorString()); return false;
    }
    return true;
}

} // namespace QindaTK
