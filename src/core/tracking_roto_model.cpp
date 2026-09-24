// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tracking_roto_model.h"

#include <QColorSpace>
#include <QImageReader>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr qint64 kMaxFramePixels = 4 * 1024 * 1024;
constexpr qsizetype kMaxFrameBytes = 256 * 1024 * 1024;
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool finite(double value) { return std::isfinite(value); }

double patchDifference(const QImage &reference, int rx, int ry,
                       const QImage &target, int tx, int ty, int radius)
{
    int sum = 0;
    const int width = radius * 2 + 1;
    for (int y = -radius; y <= radius; ++y) {
        const auto *a = reference.constScanLine(ry + y) + rx - radius;
        const auto *b = target.constScanLine(ty + y) + tx - radius;
        for (int x = 0; x < width; ++x) sum += std::abs(int(a[x]) - int(b[x]));
    }
    return double(sum) / double(width * width);
}

double patchVariation(const QImage &image, int x, int y, int radius)
{
    int sum = 0;
    int sumSquares = 0;
    const int width = radius * 2 + 1;
    for (int yy = -radius; yy <= radius; ++yy) {
        const auto *row = image.constScanLine(y + yy) + x - radius;
        for (int xx = 0; xx < width; ++xx) {
            const int v = row[xx];
            sum += v;
            sumSquares += v * v;
        }
    }
    const double n = double(width * width);
    const double mean = sum / n;
    return std::sqrt(std::max(0.0, sumSquares / n - mean * mean));
}
} // namespace

TrackingRotoModel::TrackingRotoModel(QObject *parent) : QObject(parent) {}

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

QVariantMap TrackingRotoModel::pointMap(const Point &point)
{
    return {{QStringLiteral("frame"), point.frame}, {QStringLiteral("x"), point.x},
            {QStringLiteral("y"), point.y}, {QStringLiteral("confidence"), point.confidence},
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
                                    {QStringLiteral("points"), pathList(it.value())}});
        result.append(QVariantMap{{QStringLiteral("maskId"), mask.id},
                                  {QStringLiteral("label"), mask.label},
                                  {QStringLiteral("feather"), mask.feather},
                                  {QStringLiteral("keyframes"), keys}});
    }
    return result;
}

double TrackingRotoModel::analysisProgress() const
{
    if (m_analysis.trackId.isEmpty()) return 0.0;
    return std::clamp(double(m_analysis.next - m_analysis.start)
        / double(m_analysis.end - m_analysis.start + 1), 0.0, 1.0);
}

bool TrackingRotoModel::setFrame(int frame, const QImage &image)
{
    if (frame < 0 || image.isNull() || !image.colorSpace().isValid()
        || qint64(image.width()) * image.height() > kMaxFramePixels) {
        setError(QStringLiteral("Tracking frames need a nonnegative index, color tag and at most 4 megapixels."));
        return false;
    }
    const QImage srgb = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    const QImage gray = srgb.convertToFormat(QImage::Format_Grayscale8);
    const qsizetype oldBytes = m_frames.value(frame).sizeInBytes();
    if (gray.isNull() || m_frameBytes - oldBytes + gray.sizeInBytes() > kMaxFrameBytes) {
        setError(QStringLiteral("Tracking frame conversion failed or exceeds the 256 MiB budget."));
        return false;
    }
    cancelAnalysis();
    m_frameBytes += gray.sizeInBytes() - oldBytes;
    m_frames.insert(frame, gray);
    ++m_frameRevision;
    setError({});
    return true;
}

bool TrackingRotoModel::loadFrame(int frame, const QUrl &url)
{
    if (!url.isLocalFile()) { setError(QStringLiteral("Tracking frame URL must be local.")); return false; }
    QImageReader reader(url.toLocalFile());
    const QSize size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() > kMaxFramePixels) {
        setError(QStringLiteral("Tracking frame exceeds 4 megapixels."));
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
    track.points.insert(referenceFrame, {referenceFrame, x, y, 1.0,
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

bool TrackingRotoModel::startAnalysis(const QString &trackId, int startFrame, int endFrame)
{
    const int index = trackIndex(trackId);
    if (m_analysis.running || index < 0 || startFrame < 0 || endFrame < startFrame
        || endFrame - startFrame >= 300) {
        setError(QStringLiteral("Analysis needs one valid track and a range of at most 300 frames."));
        return false;
    }
    Track &track = m_tracks[index];
    const QImage reference = m_frames.value(track.referenceFrame);
    const int rx = qRound(track.seedX), ry = qRound(track.seedY);
    if (reference.isNull() || rx - track.radius < 0 || ry - track.radius < 0
        || rx + track.radius >= reference.width() || ry + track.radius >= reference.height()
        || patchVariation(reference, rx, ry, track.radius) < 6.0) {
        setError(QStringLiteral("Reference patch is missing, out of bounds or lacks texture."));
        return false;
    }
    for (int frame = startFrame; frame <= endFrame; ++frame)
        if (!m_frames.contains(frame) || m_frames.value(frame).size() != reference.size()) {
            setError(QStringLiteral("Every analysis frame must be loaded at the reference size."));
            return false;
        }
    track.preview.clear();
    track.state = QStringLiteral("running");
    m_analysis = {true, trackId, startFrame, startFrame, endFrame, m_frameRevision};
    setError({});
    emit changed();
    emit analysisChanged();
    QTimer::singleShot(0, this, [this] { analyzeStep(); });
    return true;
}

void TrackingRotoModel::analyzeStep()
{
    if (!m_analysis.running) return;
    const int index = trackIndex(m_analysis.trackId);
    if (index < 0 || m_analysis.frameRevision != m_frameRevision) { cancelAnalysis(); return; }
    Track &track = m_tracks[index];
    const int frame = m_analysis.next;
    if (frame > m_analysis.end) {
        m_analysis.running = false;
        track.state = QStringLiteral("ready");
        emit changed(); emit analysisChanged();
        return;
    }
    const Point prior = !track.preview.isEmpty() ? track.preview.last()
        : track.points.value(frame - 1, track.points.value(track.referenceFrame));
    const QImage &reference = m_frames[track.referenceFrame];
    const QImage &target = m_frames[frame];
    const int rx = qRound(track.seedX), ry = qRound(track.seedY);
    double best = std::numeric_limits<double>::infinity();
    int bestX = qRound(prior.x), bestY = qRound(prior.y);
    for (int y = bestY - track.searchRadius; y <= qRound(prior.y) + track.searchRadius; ++y) {
        if (y - track.radius < 0 || y + track.radius >= target.height()) continue;
        for (int x = qRound(prior.x) - track.searchRadius;
             x <= qRound(prior.x) + track.searchRadius; ++x) {
            if (x - track.radius < 0 || x + track.radius >= target.width()) continue;
            const double difference = patchDifference(reference, rx, ry, target, x, y, track.radius);
            if (difference < best) { best = difference; bestX = x; bestY = y; }
        }
    }
    const double confidence = std::isfinite(best) ? std::max(0.0, 1.0 - best / 64.0) : 0.0;
    const bool found = best <= 16.0;
    track.preview.insert(frame, {frame, double(bestX), double(bestY), confidence,
        found ? QStringLiteral("tracked") : QStringLiteral("lost"), QStringLiteral("generated")});
    if (!found) {
        track.state = QStringLiteral("lost");
        m_analysis.running = false;
        setError(QStringLiteral("Feature lost at frame %1 (confidence %2).")
                 .arg(frame).arg(confidence, 0, 'f', 2));
        emit changed(); emit analysisChanged();
        return;
    }
    ++m_analysis.next;
    emit changed(); emit analysisChanged();
    QTimer::singleShot(0, this, [this] { analyzeStep(); });
}

void TrackingRotoModel::cancelAnalysis()
{
    if (!m_analysis.running) return;
    const int index = trackIndex(m_analysis.trackId);
    if (index >= 0) {
        m_tracks[index].state = QStringLiteral("cancelled");
        m_tracks[index].preview.clear();
    }
    m_analysis.running = false;
    emit changed(); emit analysisChanged();
}

bool TrackingRotoModel::acceptAnalysis(const QString &trackId)
{
    const int index = trackIndex(trackId);
    if (index < 0 || m_analysis.running || m_tracks[index].preview.isEmpty()
        || m_tracks[index].state == QLatin1String("cancelled")) return false;
    remember();
    Track &track = m_tracks[index];
    for (auto it = track.preview.cbegin(); it != track.preview.cend(); ++it)
        track.points.insert(it.key(), it.value());
    track.preview.clear();
    setError({});
    emit changed();
    return true;
}

bool TrackingRotoModel::correctPoint(const QString &trackId, int frame, double x, double y)
{
    const int index = trackIndex(trackId);
    if (index < 0 || frame < 0 || !finite(x) || !finite(y) || m_analysis.running) return false;
    remember();
    Track &track = m_tracks[index];
    track.points.insert(frame, {frame, x, y, 1.0,
                        QStringLiteral("corrected"), QStringLiteral("user")});
    if (frame == track.referenceFrame) { track.seedX = x; track.seedY = y; }
    track.preview.clear();
    track.state = QStringLiteral("ready");
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
    emit changed();
    return true;
}

bool TrackingRotoModel::undo()
{
    if (m_undo.isEmpty() || m_analysis.running) return false;
    m_redo.append({m_tracks, m_masks});
    const State state = m_undo.takeLast();
    m_tracks = state.tracks; m_masks = state.masks;
    emit changed();
    return true;
}

bool TrackingRotoModel::redo()
{
    if (m_redo.isEmpty() || m_analysis.running) return false;
    m_undo.append({m_tracks, m_masks});
    const State state = m_redo.takeLast();
    m_tracks = state.tracks; m_masks = state.masks;
    emit changed();
    return true;
}

} // namespace QindaTK
