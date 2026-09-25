// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tracking_roto_model.h"
#include "tracking_engine.h"

#include <QColorSpace>
#include <QImageReader>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

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
    if (m_analysis.total < 1) return 0.0;
    return std::clamp(double(m_analysis.done) / double(m_analysis.total), 0.0, 1.0);
}

bool TrackingRotoModel::setFrame(int frame, const QImage &image)
{
    QString error;
    const QImage gray = frame < 0 ? QImage() : TrackingEngine::proxy(image, &error);
    if (gray.isNull()) {
        setError(frame < 0 ? QStringLiteral("Tracking frames need a nonnegative index.") : error);
        return false;
    }
    return storeProxy(frame, gray);
}

bool TrackingRotoModel::storeProxy(int frame, const QImage &gray)
{
    const qsizetype oldBytes = m_frames.value(frame).sizeInBytes();
    if (m_frameBytes - oldBytes + gray.sizeInBytes() > TrackingEngine::kMaxProxyBytes) {
        setError(QStringLiteral("Tracking frames exceed the 256 MiB proxy budget."));
        return false;
    }
    // AGENT-GUARD: replacing a frame invalidates a running analysis, whose job
    // holds the old proxies; results measured on stale pixels never apply.
    cancelAnalysis();
    m_frameBytes += gray.sizeInBytes() - oldBytes;
    m_frames.insert(frame, gray);
    setError({});
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
    if (size.isValid() && qint64(size.width()) * size.height() > TrackingEngine::kMaxProxyPixels) {
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

bool TrackingRotoModel::startAnalysis(const QString &trackId, int startFrame, int endFrame,
                                      int occlusionFrames)
{
    const int index = trackIndex(trackId);
    if (m_analysis.running || m_load.running || index < 0 || startFrame < 0 || endFrame < 0
        || std::abs(endFrame - startFrame) >= 300 || occlusionFrames < 0 || occlusionFrames > 30) {
        setError(QStringLiteral("Analysis needs one valid track, no running job, a range of at most "
                                "300 frames and 0–30 occlusion frames."));
        return false;
    }
    Track &track = m_tracks[index];
    const QImage reference = m_frames.value(track.referenceFrame);
    const QPointF seed(track.seedX, track.seedY);
    if (!TrackingEngine::trackableReference(reference, seed, track.radius)) {
        setError(QStringLiteral("Reference patch is missing, out of bounds or lacks texture."));
        return false;
    }
    const int step = endFrame >= startFrame ? 1 : -1;
    TrackingEngine::Job job;
    job.reference = reference;
    job.seed = seed;
    job.radius = track.radius;
    job.searchRadius = track.searchRadius;
    job.occlusionFrames = occlusionFrames;
    for (int frame = startFrame;; frame += step) {
        const QImage image = m_frames.value(frame);
        if (image.isNull() || image.size() != reference.size()) {
            setError(QStringLiteral("Every analysis frame must be loaded at the reference size."));
            return false;
        }
        job.frames.append(frame);
        job.images.append(image);
        if (frame == endFrame) break;
    }
    // The search starts from the accepted neighbour before the range when it
    // is trusted, otherwise from the reference seed.
    const Point before = track.points.value(startFrame - step);
    const bool trusted = track.points.contains(startFrame - step) && before.state != QLatin1String("lost");
    job.start = trusted ? QPointF(before.x, before.y) : seed;

    track.preview.clear();
    track.state = QStringLiteral("running");
    // A cancelled job stops at its next frame; never leave it unjoined.
    m_analysisFuture.waitForFinished();
    m_analysis = Analysis();
    m_analysis.running = true;
    m_analysis.trackId = trackId;
    m_analysis.total = int(job.frames.size());
    m_analysis.job = ++m_jobSerial;
    const quint64 id = m_analysis.job;
    const CancellationToken token = m_analysis.cancel.token();
    // AGENT-NOTE: the worker owns shared proxy copies (TrackingEngine::Job)
    // and posts each sample back; stale jobs are ignored by ID, so cancel
    // is immediate on this thread even while the worker finishes a frame.
    m_analysisFuture = QtConcurrent::run([this, id, token, job = std::move(job)] {
        const TrackingEngine::End end = TrackingEngine::run(job, token, [this, id](const TrackingEngine::Sample &s) {
            QMetaObject::invokeMethod(this, [this, id, s] {
                onAnalysisSample(id, s.frame, s.x, s.y, s.confidence, s.lost);
            }, Qt::QueuedConnection);
        });
        if (end == TrackingEngine::End::Cancelled) return;
        const bool lost = end == TrackingEngine::End::Lost;
        QMetaObject::invokeMethod(this, [this, id, lost] { onAnalysisFinished(id, lost); },
                                  Qt::QueuedConnection);
    });
    setError({});
    emit changed();
    emit analysisChanged();
    return true;
}

void TrackingRotoModel::onAnalysisSample(quint64 job, int frame, double x, double y,
                                         double confidence, bool lost)
{
    if (!m_analysis.running || m_analysis.job != job) return;
    const int index = trackIndex(m_analysis.trackId);
    if (index < 0) { cancelAnalysis(); return; }
    m_tracks[index].preview.insert(frame, {frame, x, y, confidence,
        lost ? QStringLiteral("lost") : QStringLiteral("tracked"), QStringLiteral("generated")});
    ++m_analysis.done;
    m_analysis.lastFrame = frame;
    m_analysis.lastConfidence = confidence;
    emit changed();
    emit analysisChanged();
}

void TrackingRotoModel::onAnalysisFinished(quint64 job, bool lost)
{
    if (!m_analysis.running || m_analysis.job != job) return;
    m_analysis.running = false;
    const int index = trackIndex(m_analysis.trackId);
    if (index >= 0) {
        Track &track = m_tracks[index];
        track.state = lost ? QStringLiteral("lost") : QStringLiteral("ready");
        if (lost)
            setError(QStringLiteral("Feature lost at frame %1 (confidence %2).")
                     .arg(m_analysis.lastFrame).arg(m_analysis.lastConfidence, 0, 'f', 2));
    }
    emit changed();
    emit analysisChanged();
}

void TrackingRotoModel::cancelAnalysis()
{
    if (!m_analysis.running) return;
    m_analysis.cancel.cancel();
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
    track.preview.clear();
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
