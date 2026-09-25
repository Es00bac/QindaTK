// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tracking_roto_model.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qsizetype kMaxJsonBytes = 4 * 1024 * 1024;
bool finite(double value) { return std::isfinite(value); }
}

QVariantMap TrackingRotoModel::contentMap() const
{
    QVariantList trackRecords;
    for (const Track &track : m_tracks) {
        QVariantList points;
        for (const Point &point : track.points) points.append(pointMap(point));
        trackRecords.append(QVariantMap{{QStringLiteral("trackId"), track.id},
            {QStringLiteral("label"), track.label},
            {QStringLiteral("referenceFrame"), track.referenceFrame},
            {QStringLiteral("seedX"), track.seedX}, {QStringLiteral("seedY"), track.seedY},
            {QStringLiteral("radius"), track.radius},
            {QStringLiteral("searchRadius"), track.searchRadius},
            {QStringLiteral("motion"), track.motion},
            {QStringLiteral("referenceRotation"), track.referenceRotation},
            {QStringLiteral("referenceScale"), track.referenceScale},
            {QStringLiteral("state"), track.state}, {QStringLiteral("points"), points}});
    }
    return {{QStringLiteral("schema"), QStringLiteral("qindatk.tracking-roto")},
            {QStringLiteral("version"), 1}, {QStringLiteral("tracks"), trackRecords},
            {QStringLiteral("masks"), masks()}};
}

// AGENT-CONTRACT: saveToFile runs this parser on the bytes it is about to
// write and refuses to write what it would reject, so every saved document
// reopens (the limits here and in the editing operations must agree).
// Fields added after the first version 1 files (motion, reference pose,
// point rotation/scale, key source/trackIds) are optional with defaults.
bool TrackingRotoModel::parseContent(const QVariantMap &content, QVector<Track> *outTracks,
                                     QVector<Mask> *outMasks)
{
    if (content.value(QStringLiteral("schema")).toString() != QLatin1String("qindatk.tracking-roto")
        || content.value(QStringLiteral("version")).toInt() != 1) return false;
    const QVariantList tracks = content.value(QStringLiteral("tracks")).toList();
    const QVariantList masks = content.value(QStringLiteral("masks")).toList();
    if (tracks.size() > 128 || masks.size() > 128) return false;
    QVector<Track> nextTracks;
    QVector<Mask> nextMasks;
    QSet<QString> ids;
    for (const QVariant &entry : tracks) {
        const QVariantMap item = entry.toMap();
        Track track;
        track.id = item.value(QStringLiteral("trackId")).toString();
        track.label = item.value(QStringLiteral("label")).toString();
        track.referenceFrame = item.value(QStringLiteral("referenceFrame")).toInt();
        track.seedX = item.value(QStringLiteral("seedX")).toDouble();
        track.seedY = item.value(QStringLiteral("seedY")).toDouble();
        track.radius = item.value(QStringLiteral("radius")).toInt();
        track.searchRadius = item.value(QStringLiteral("searchRadius")).toInt();
        track.state = item.value(QStringLiteral("state")).toString();
        track.motion = item.value(QStringLiteral("motion"), QStringLiteral("translation")).toString();
        track.referenceRotation = item.value(QStringLiteral("referenceRotation"), 0.0).toDouble();
        track.referenceScale = item.value(QStringLiteral("referenceScale"), 1.0).toDouble();
        if (track.motion != QLatin1String("translation") && track.motion != QLatin1String("similarity"))
            return false;
        if (!finite(track.referenceRotation) || !finite(track.referenceScale)
            || track.referenceScale <= 0) return false;
        if (track.id.isEmpty() || ids.contains(track.id) || track.referenceFrame < 0
            || !finite(track.seedX) || !finite(track.seedY)
            || track.radius < 2 || track.radius > 16
            || track.searchRadius < 1 || track.searchRadius > 64) return false;
        ids.insert(track.id);
        const QVariantList points = item.value(QStringLiteral("points")).toList();
        if (points.size() > kMaxTrackPoints) return false;
        for (const QVariant &pointValue : points) {
            const QVariantMap p = pointValue.toMap();
            Point point;
            point.frame = p.value(QStringLiteral("frame")).toInt();
            point.x = p.value(QStringLiteral("x")).toDouble();
            point.y = p.value(QStringLiteral("y")).toDouble();
            point.confidence = p.value(QStringLiteral("confidence")).toDouble();
            point.rotation = p.value(QStringLiteral("rotation"), 0.0).toDouble();
            point.scale = p.value(QStringLiteral("scale"), 1.0).toDouble();
            point.state = p.value(QStringLiteral("state")).toString();
            point.source = p.value(QStringLiteral("source")).toString();
            if (point.frame < 0 || track.points.contains(point.frame)
                || !finite(point.x) || !finite(point.y) || !finite(point.confidence)
                || point.confidence < 0 || point.confidence > 1
                || !finite(point.rotation) || !finite(point.scale) || point.scale <= 0
                || (point.state != QLatin1String("tracked")
                    && point.state != QLatin1String("lost")
                    && point.state != QLatin1String("corrected"))) return false;
            track.points.insert(point.frame, point);
        }
        // A loaded analysis cannot resume without source frames; only accepted
        // edits survive reopening.
        if (track.state == QLatin1String("running")) track.state = QStringLiteral("idle");
        nextTracks.append(track);
    }
    ids.clear();
    for (const QVariant &entry : masks) {
        const QVariantMap item = entry.toMap();
        Mask mask;
        mask.id = item.value(QStringLiteral("maskId")).toString();
        mask.label = item.value(QStringLiteral("label")).toString();
        mask.feather = item.value(QStringLiteral("feather")).toDouble();
        if (mask.id.isEmpty() || ids.contains(mask.id) || !finite(mask.feather)
            || mask.feather < 0 || mask.feather > 64) return false;
        ids.insert(mask.id);
        const QVariantList keys = item.value(QStringLiteral("keyframes")).toList();
        if (keys.size() > 512) return false;
        for (const QVariant &keyValue : keys) {
            const QVariantMap key = keyValue.toMap();
            const int frame = key.value(QStringLiteral("frame")).toInt();
            MaskKey parsed;
            parsed.source = key.value(QStringLiteral("source"), QStringLiteral("user")).toString();
            parsed.trackIds = key.value(QStringLiteral("trackIds")).toStringList();
            if (frame < 0 || mask.keys.contains(frame)
                || (parsed.source != QLatin1String("user") && parsed.source != QLatin1String("generated"))
                || parsed.trackIds.size() > 4
                || !parsePath(key.value(QStringLiteral("points")).toList(), &parsed.points)) return false;
            mask.keys.insert(frame, parsed);
        }
        nextMasks.append(mask);
    }
    *outTracks = nextTracks;
    *outMasks = nextMasks;
    return true;
}

bool TrackingRotoModel::loadContent(const QVariantMap &content)
{
    QVector<Track> nextTracks;
    QVector<Mask> nextMasks;
    if (!parseContent(content, &nextTracks, &nextMasks)) return false;
    cancelAnalysis();
    m_tracks = nextTracks;
    m_masks = nextMasks;
    m_undo.clear(); m_redo.clear();
    emit changed();
    return true;
}

bool TrackingRotoModel::saveToFile(const QUrl &url)
{
    if (!url.isLocalFile()) {
        setError(QStringLiteral("Tracking project path must be local."));
        return false;
    }
    const QByteArray bytes = QJsonDocument::fromVariant(contentMap()).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaxJsonBytes) {
        setError(QStringLiteral("Tracking project exceeds 4 MiB."));
        return false;
    }
    QVector<Track> checkTracks;
    QVector<Mask> checkMasks;
    if (!parseContent(QJsonDocument::fromJson(bytes).toVariant().toMap(), &checkTracks, &checkMasks)) {
        setError(QStringLiteral("Tracking project would not reopen; nothing was written."));
        return false;
    }
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(file.errorString());
        return false;
    }
    setError({});
    return true;
}

bool TrackingRotoModel::loadFromFile(const QUrl &url)
{
    if (!url.isLocalFile()) {
        setError(QStringLiteral("Tracking project path must be local."));
        return false;
    }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxJsonBytes) {
        setError(QStringLiteral("Tracking project is unreadable or exceeds 4 MiB."));
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || !loadContent(document.toVariant().toMap())) {
        setError(QStringLiteral("Tracking project schema or content is invalid."));
        return false;
    }
    setError({});
    return true;
}

} // namespace QindaTK
