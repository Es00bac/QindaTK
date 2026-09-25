// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tracking_roto_model.h"
#include "tracking_engine.h"

#include <QColorSpace>
#include <QImageReader>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace QindaTK {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool finite(double value) { return std::isfinite(value); }
} // namespace

TrackingRotoModel::TrackingRotoModel(QObject *parent) : QObject(parent) {}

TrackingRotoModel::~TrackingRotoModel()
{
    m_analysis.cancel.cancel();
    m_load.cancel.cancel();
    m_analysisFuture.waitForFinished();
    m_loadFuture.waitForFinished();
}

int TrackingRotoModel::trackIndex(const QString &id) const
{
    for (int i = 0; i < m_tracks.size(); ++i) if (m_tracks[i].id == id) return i;
    return -1;
}

int TrackingRotoModel::maskIndex(const QString &id) const
{
    for (int i = 0; i < m_masks.size(); ++i) if (m_masks[i].id == id) return i;
    return -1;
}

void TrackingRotoModel::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

void TrackingRotoModel::remember()
{
    if (m_undo.size() == 64) m_undo.removeFirst();
    m_undo.append({m_tracks, m_masks});
    m_redo.clear();
}

// AGENT-GUARD (VID-15: analysis results are separate from accepted edits):
// undo/redo restore accepted data only. A live preview (unaccepted results)
// and the live analysis state win over the snapshot's, so undoing an
// unrelated edit never discards finished results or resurrects "running"
// (a snapshot taken while a job ran). A snapshot's own preview comes back
// only when the live track has none and its frames are unchanged, which is
// how undoing an accept returns the results to preview.
void TrackingRotoModel::restore(const State &state)
{
    QVector<Track> tracks = state.tracks;
    for (Track &track : tracks) {
        const int live = trackIndex(track.id);
        const bool stale = track.state == QLatin1String("running") || !previewCurrent(track);
        if (live >= 0 && (!m_tracks[live].preview.isEmpty() || stale)) {
            track.state = m_tracks[live].state;
            track.preview = m_tracks[live].preview;
            track.previewInputs = m_tracks[live].previewInputs;
        } else if (stale) {
            track.state = QStringLiteral("idle");
            track.preview.clear();
            track.previewInputs.clear();
        }
    }
    m_tracks = tracks;
    m_masks = state.masks;
}

QVariantMap TrackingRotoModel::pointMap(const Point &point)
{
    return {{QStringLiteral("frame"), point.frame}, {QStringLiteral("x"), point.x},
            {QStringLiteral("y"), point.y}, {QStringLiteral("confidence"), point.confidence},
            {QStringLiteral("rotation"), point.rotation}, {QStringLiteral("scale"), point.scale},
            {QStringLiteral("state"), point.state}, {QStringLiteral("source"), point.source}};
}

QVariantList TrackingRotoModel::tracks() const
{
    QVariantList result;
    for (const Track &track : m_tracks) {
        QVariantList points, preview;
        for (const Point &point : track.points) points.append(pointMap(point));
        for (const Point &point : track.preview) preview.append(pointMap(point));
        result.append(QVariantMap{{QStringLiteral("trackId"), track.id},
            {QStringLiteral("label"), track.label},
            {QStringLiteral("referenceFrame"), track.referenceFrame},
            {QStringLiteral("seedX"), track.seedX}, {QStringLiteral("seedY"), track.seedY},
            {QStringLiteral("radius"), track.radius},
            {QStringLiteral("searchRadius"), track.searchRadius},
            {QStringLiteral("motion"), track.motion},
            {QStringLiteral("state"), track.state},
            {QStringLiteral("points"), points}, {QStringLiteral("previewPoints"), preview}});
    }
    return result;
}

QVariantList TrackingRotoModel::pathList(const QVector<PathPoint> &points)
{
    QVariantList result;
    for (const PathPoint &point : points)
        result.append(QVariantMap{{QStringLiteral("pointId"), point.id},
                                  {QStringLiteral("x"), point.x}, {QStringLiteral("y"), point.y}});
    return result;
}

QVariantList TrackingRotoModel::masks() const
{
    QVariantList result;
    for (const Mask &mask : m_masks) {
        QVariantList keys;
        for (auto it = mask.keys.cbegin(); it != mask.keys.cend(); ++it)
            keys.append(QVariantMap{{QStringLiteral("frame"), it.key()},
                                    {QStringLiteral("points"), pathList(it->points)},
                                    {QStringLiteral("source"), it->source},
                                    {QStringLiteral("trackIds"), it->trackIds}});
        result.append(QVariantMap{{QStringLiteral("maskId"), mask.id},
                                  {QStringLiteral("label"), mask.label},
                                  {QStringLiteral("feather"), mask.feather},
                                  {QStringLiteral("keyframes"), keys}});
    }
    return result;
}

bool TrackingRotoModel::setFrame(int frame, const QImage &image)
{
    QString error;
    const QImage gray = frame < 0 ? QImage() : TrackingEngine::proxy(image, &error);
    if (gray.isNull()) {
        setError(frame < 0 ? QStringLiteral("Tracking frames need a nonnegative index.") : error);
        return false;
    }
    return storeProxy(frame, gray, image.size());
}

bool TrackingRotoModel::storeProxy(int frame, const QImage &gray, QSize sourceSize)
{
    const qsizetype oldBytes = m_frames.value(frame).sizeInBytes();
    if (m_frameBytes - oldBytes + gray.sizeInBytes() > TrackingEngine::kMaxProxyBytes) {
        setError(QStringLiteral("Tracking frames exceed the 256 MiB proxy budget."));
        return false;
    }
    // AGENT-GUARD: replacing a frame invalidates a running analysis, whose job
    // holds the old proxies, and every finished preview measured on it
    // (discardStaleResults; acceptAnalysis also checks previewInputs), so
    // results measured on stale pixels never apply.
    cancelAnalysis();
    m_frameBytes += gray.sizeInBytes() - oldBytes;
    m_frames.insert(frame, gray);
    m_frameSource.insert(frame, sourceSize);
    m_frameVersions.insert(frame, ++m_frameSerial);
    setError({});
    discardStaleResults(frame);
    emit framesChanged();
    return true;
}

QVariantList TrackingRotoModel::loadedFrames() const
{
    QList<int> keys = m_frames.keys();
    std::sort(keys.begin(), keys.end());
    QVariantList result;
    for (int key : keys) result.append(key);
    return result;
}

bool TrackingRotoModel::loadFrame(int frame, const QUrl &url)
{
    if (!url.isLocalFile()) { setError(QStringLiteral("Tracking frame URL must be local.")); return false; }
    QImageReader reader(url.toLocalFile());
    const QSize size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() > TrackingEngine::kMaxSourcePixels) {
        setError(QStringLiteral("Tracking frame exceeds 64 megapixels."));
        return false;
    }
    QImage image = reader.read();
    if (image.isNull()) { setError(reader.errorString()); return false; }
    if (!image.colorSpace().isValid()) image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return setFrame(frame, image);
}

QString TrackingRotoModel::addTrack(const QString &label, int referenceFrame,
                                     double x, double y, int radius, int searchRadius)
{
    if (m_tracks.size() >= 128 || referenceFrame < 0 || !finite(x) || !finite(y)
        || radius < 2 || radius > 16 || searchRadius < 1 || searchRadius > 64) {
        setError(QStringLiteral("Track needs finite coordinates, radius 2–16 and search radius 1–64."));
        return {};
    }
    remember();
    Track track;
    track.id = freshId();
    track.label = label.trimmed().left(128);
    track.referenceFrame = referenceFrame;
    track.seedX = x;
    track.seedY = y;
    track.radius = radius;
    track.searchRadius = searchRadius;
    track.points.insert(referenceFrame, {referenceFrame, x, y, 1.0, 0.0, 1.0,
                         QStringLiteral("tracked"), QStringLiteral("seed")});
    m_tracks.append(track);
    setError({});
    emit changed();
    return track.id;
}

bool TrackingRotoModel::removeTrack(const QString &trackId)
{
    const int index = trackIndex(trackId);
    if (index < 0) return false;
    if (m_analysis.running && m_analysis.trackId == trackId) cancelAnalysis();
    remember();
    m_tracks.removeAt(index);
    emit changed();
    return true;
}

bool TrackingRotoModel::setTrackOptions(const QString &trackId, int radius, int searchRadius,
                                        const QString &motion)
{
    const int index = trackIndex(trackId);
    if (index < 0 || (m_analysis.running && m_analysis.trackId == trackId)
        || radius < 2 || radius > 16 || searchRadius < 1 || searchRadius > 64
        || (motion != QLatin1String("translation") && motion != QLatin1String("similarity"))) {
        setError(QStringLiteral("Track options need radius 2–16, search radius 1–64, motion "
                                "translation or similarity, and no running analysis on the track."));
        return false;
    }
    const Track &current = m_tracks.at(index);
    if (current.radius == radius && current.searchRadius == searchRadius && current.motion == motion)
        return true;
    remember();
    Track &track = m_tracks[index];
    track.radius = radius;
    track.searchRadius = searchRadius;
    track.motion = motion;
    setError({});
    emit changed();
    return true;
}

bool TrackingRotoModel::correctPoint(const QString &trackId, int frame, double x, double y)
{
    const int index = trackIndex(trackId);
    if (index < 0 || frame < 0 || !finite(x) || !finite(y) || m_analysis.running) return false;
    const QMap<int, Point> &existing = m_tracks.at(index).points;
    if (!existing.contains(frame) && existing.size() >= kMaxTrackPoints) {
        setError(QStringLiteral("A track holds at most %1 points.").arg(kMaxTrackPoints));
        return false;
    }
    // A corrected position keeps the pose (rotation/scale) of the nearest
    // accepted point at or before the frame.
    auto posed = existing.upperBound(frame);
    const Point pose = posed == existing.cbegin() ? Point() : *std::prev(posed);
    remember();
    Track &track = m_tracks[index];
    track.points.insert(frame, {frame, x, y, 1.0, pose.rotation, pose.scale,
                        QStringLiteral("corrected"), QStringLiteral("user")});
    if (frame == track.referenceFrame) { track.seedX = x; track.seedY = y; }
    track.preview.clear();
    track.previewInputs.clear();
    track.state = QStringLiteral("ready");
    setError({});
    emit changed();
    return true;
}

bool TrackingRotoModel::offsetTrack(const QString &trackId, double dx, double dy,
                                    int fromFrame, int toFrame)
{
    const int index = trackIndex(trackId);
    if (index < 0 || !finite(dx) || !finite(dy) || fromFrame > toFrame || m_analysis.running) return false;
    remember();
    Track &track = m_tracks[index];
    for (auto it = track.points.lowerBound(fromFrame);
         it != track.points.end() && it.key() <= toFrame; ++it) {
        it->x += dx; it->y += dy;
        it->state = QStringLiteral("corrected");
        it->source = QStringLiteral("user");
    }
    track.preview.clear();
    track.previewInputs.clear();
    emit changed();
    return true;
}

bool TrackingRotoModel::rebaseTrack(const QString &trackId, int frame)
{
    const int index = trackIndex(trackId);
    if (index < 0 || m_analysis.running) return false;
    const QMap<int, Point> &points = m_tracks.at(index).points;
    if (!points.contains(frame) || points.value(frame).state == QLatin1String("lost")) {
        setError(QStringLiteral("Rebase needs an accepted, nonlost point at that frame."));
        return false;
    }
    // AGENT-NOTE: rebasing moves the reference patch to a trusted later (or
    // earlier) appearance of the feature, so analysis survives gradual
    // appearance change; the accepted points themselves are unchanged.
    const Point point = points.value(frame);
    remember();
    Track &track = m_tracks[index];
    track.referenceFrame = frame;
    track.seedX = point.x;
    track.seedY = point.y;
    track.referenceRotation = point.rotation;
    track.referenceScale = point.scale;
    track.preview.clear();
    track.previewInputs.clear();
    if (track.state == QLatin1String("lost") || track.state == QLatin1String("cancelled"))
        track.state = QStringLiteral("ready");
    setError({});
    emit changed();
    return true;
}

bool TrackingRotoModel::undo()
{
    if (m_undo.isEmpty() || m_analysis.running) return false;
    m_redo.append({m_tracks, m_masks});
    restore(m_undo.takeLast());
    emit changed();
    return true;
}

bool TrackingRotoModel::redo()
{
    if (m_redo.isEmpty() || m_analysis.running) return false;
    m_undo.append({m_tracks, m_masks});
    restore(m_redo.takeLast());
    emit changed();
    return true;
}

} // namespace QindaTK
