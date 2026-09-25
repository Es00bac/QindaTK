// SPDX-License-Identifier: LGPL-3.0-or-later
// Mask propagation by accepted tracks (docs/tracking-roto.md "Masks from
// tracking"): one track moves a path by translation, rotation and scale;
// four tracks corner-pin it with the perspective map of their quad.
#include "tracking_roto_model.h"

#include <QPolygonF>
#include <QSet>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace QindaTK {

bool TrackingRotoModel::mappedPath(const QString &maskId, int sourceFrame,
                                   const QVector<const Track *> &tracks, int targetFrame,
                                   QVector<PathPoint> *path, QString *error) const
{
    const QVariantMap shape = evaluateMask(maskId, sourceFrame);
    if (!shape.value(QStringLiteral("ok")).toBool()) {
        *error = QStringLiteral("Source mask is unavailable.");
        return false;
    }
    const QSize size = m_frameSource.value(tracks.first()->referenceFrame);
    for (const Track *track : tracks) {
        if (size.isEmpty() || m_frameSource.value(track->referenceFrame) != size) {
            *error = QStringLiteral("Rebind source frames before mask propagation.");
            return false;
        }
        // AGENT-GUARD: only accepted, nonlost points move a mask; a lost frame
        // holds the last trusted position, which must never read as motion.
        const QMap<int, Point> &points = track->points;
        if (!points.contains(sourceFrame) || !points.contains(targetFrame)
            || points.value(sourceFrame).state == QLatin1String("lost")
            || points.value(targetFrame).state == QLatin1String("lost")) {
            *error = QStringLiteral("Accepted tracking points are unavailable or lost.");
            return false;
        }
    }
    QVector<PathPoint> result;
    parsePath(shape.value(QStringLiteral("points")).toList(), &result);
    const double w = size.width(), h = size.height();
    if (tracks.size() == 4) {
        QPolygonF from, to;
        for (const Track *track : tracks) {
            from << QPointF(track->points.value(sourceFrame).x, track->points.value(sourceFrame).y);
            to << QPointF(track->points.value(targetFrame).x, track->points.value(targetFrame).y);
        }
        QTransform pin;
        if (!QTransform::quadToQuad(from, to, pin)) {
            *error = QStringLiteral("Corner-pin tracks do not form a quad at both frames.");
            return false;
        }
        for (PathPoint &point : result) {
            const QPointF mapped = pin.map(QPointF(point.x * w, point.y * h));
            point.x = mapped.x() / w;
            point.y = mapped.y() / h;
        }
    } else {
        const Point a = tracks.first()->points.value(sourceFrame);
        const Point b = tracks.first()->points.value(targetFrame);
        const double turn = qDegreesToRadians(b.rotation - a.rotation);
        const double ratio = b.scale / a.scale;
        for (PathPoint &point : result) {
            if (turn == 0.0 && ratio == 1.0) {
                point.x += (b.x - a.x) / w;
                point.y += (b.y - a.y) / h;
                continue;
            }
            // Similarity about the tracked point, in source pixels (y down, so
            // a positive angle turns clockwise on screen).
            const double ox = point.x * w - a.x, oy = point.y * h - a.y;
            point.x = (b.x + ratio * (std::cos(turn) * ox - std::sin(turn) * oy)) / w;
            point.y = (b.y + ratio * (std::sin(turn) * ox + std::cos(turn) * oy)) / h;
        }
    }
    for (PathPoint &point : result) {
        point.x = std::clamp(point.x, 0.0, 1.0);
        point.y = std::clamp(point.y, 0.0, 1.0);
    }
    *path = result;
    return true;
}

QVariantMap TrackingRotoModel::previewTrackedMask(const QString &maskId, int sourceFrame,
                                                   int targetFrame, const QString &trackId) const
{
    const int index = trackIndex(trackId);
    QVector<PathPoint> path;
    QString error = QStringLiteral("Track or source mask is unavailable.");
    if (index < 0 || !mappedPath(maskId, sourceFrame, {&m_tracks[index]}, targetFrame, &path, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("maskId"), maskId},
            {QStringLiteral("trackId"), trackId}, {QStringLiteral("frame"), targetFrame},
            {QStringLiteral("points"), pathList(path)}, {QStringLiteral("state"), QStringLiteral("preview")}};
}

bool TrackingRotoModel::propagate(const QString &maskId, int sourceFrame, const QStringList &trackIds,
                                  int fromFrame, int toFrame, Propagation *result, QString *error) const
{
    const int mask = maskIndex(maskId);
    QVector<const Track *> tracks;
    for (const QString &id : trackIds) {
        const int index = trackIndex(id);
        if (index >= 0) tracks.append(&m_tracks[index]);
    }
    if (mask < 0 || tracks.size() != trackIds.size() || (tracks.size() != 1 && tracks.size() != 4)
        || QSet<QString>(trackIds.cbegin(), trackIds.cend()).size() != trackIds.size()
        || fromFrame < 0 || toFrame < fromFrame || toFrame - fromFrame >= 512) {
        *error = QStringLiteral("Mask tracking needs a mask, one track (or four for a corner pin) "
                                "and a range of at most 512 frames.");
        return false;
    }
    // Source-side checks (mask shape, bound reference frame, trusted source
    // point) fail the request; a missing or lost target frame is skipped.
    QVector<PathPoint> path;
    if (!mappedPath(maskId, sourceFrame, tracks, sourceFrame, &path, error)) return false;
    result->keys = m_masks.at(mask).keys;
    for (int frame = fromFrame; frame <= toFrame; ++frame) {
        if (frame == sourceFrame) continue;
        // AGENT-GUARD (VID-16 "preserve intended masks"): a user key is never
        // overwritten by propagation; only empty or generated frames are keyed.
        const auto existing = result->keys.constFind(frame);
        if (existing != result->keys.cend() && existing->source != QLatin1String("generated")) {
            result->kept.append(frame);
            continue;
        }
        QString skipped;
        if (!mappedPath(maskId, sourceFrame, tracks, frame, &path, &skipped)) {
            result->skipped.append(frame);
            continue;
        }
        result->keys.insert(frame, MaskKey{path, QStringLiteral("generated"), trackIds});
        result->keyed.append(frame);
        result->generated.append(QVariantMap{{QStringLiteral("frame"), frame},
                                             {QStringLiteral("points"), pathList(path)},
                                             {QStringLiteral("source"), QStringLiteral("generated")}});
    }
    if (result->keyed.isEmpty()) {
        *error = QStringLiteral("No accepted, nonlost tracking point in the range.");
        return false;
    }
    if (result->keys.size() > 512) {
        *error = QStringLiteral("A mask holds at most 512 keyframes.");
        return false;
    }
    return true;
}

QVariantMap TrackingRotoModel::previewPropagation(const QString &maskId, int sourceFrame,
                                                  const QStringList &trackIds,
                                                  int fromFrame, int toFrame) const
{
    Propagation result;
    QString error;
    if (!propagate(maskId, sourceFrame, trackIds, fromFrame, toFrame, &result, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), QStringLiteral("preview")},
            {QStringLiteral("maskId"), maskId}, {QStringLiteral("sourceFrame"), sourceFrame},
            {QStringLiteral("fromFrame"), fromFrame}, {QStringLiteral("toFrame"), toFrame},
            {QStringLiteral("trackIds"), trackIds}, {QStringLiteral("keys"), result.generated},
            {QStringLiteral("keyedFrames"), result.keyed},
            {QStringLiteral("skippedFrames"), result.skipped},
            {QStringLiteral("keptFrames"), result.kept}};
}

QVariantMap TrackingRotoModel::applyPropagation(const QString &maskId, int sourceFrame,
                                                const QStringList &trackIds,
                                                int fromFrame, int toFrame)
{
    Propagation result;
    QString error = QStringLiteral("Mask propagation cannot run during analysis.");
    if (m_analysis.running
        || !propagate(maskId, sourceFrame, trackIds, fromFrame, toFrame, &result, &error)) {
        setError(error);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    // One undo step for the whole propagation.
    remember();
    m_masks[maskIndex(maskId)].keys = result.keys;
    setError({});
    emit changed();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("maskId"), maskId},
            {QStringLiteral("trackIds"), trackIds}, {QStringLiteral("keyedFrames"), result.keyed},
            {QStringLiteral("skippedFrames"), result.skipped},
            {QStringLiteral("keptFrames"), result.kept}};
}

QVariantMap TrackingRotoModel::applyTrackToMask(const QString &maskId, int sourceFrame,
                                                const QString &trackId, int fromFrame, int toFrame)
{
    QVariantMap result = applyPropagation(maskId, sourceFrame, {trackId}, fromFrame, toFrame);
    if (result.value(QStringLiteral("ok")).toBool()) result.insert(QStringLiteral("trackId"), trackId);
    return result;
}

} // namespace QindaTK
