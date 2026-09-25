// SPDX-License-Identifier: LGPL-3.0-or-later
// Tracking analysis jobs of TrackingRotoModel (docs/tracking-roto.md
// "Tracking contract"): worker start, samples, cancel, acceptance and the
// frame-version check that keeps stale results from applying.
#include "tracking_roto_model.h"
#include "tracking_engine.h"

#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace QindaTK {

double TrackingRotoModel::analysisProgress() const
{
    if (m_analysis.total < 1) return 0.0;
    return std::clamp(double(m_analysis.done) / double(m_analysis.total), 0.0, 1.0);
}

bool TrackingRotoModel::previewCurrent(const Track &track) const
{
    for (auto it = track.previewInputs.cbegin(); it != track.previewInputs.cend(); ++it)
        if (m_frameVersions.value(it.key()) != it.value()) return false;
    return true;
}

void TrackingRotoModel::discardStaleResults(int frame)
{
    bool discarded = false;
    for (Track &track : m_tracks) {
        if (track.preview.isEmpty() || !track.previewInputs.contains(frame)) continue;
        track.preview.clear();
        track.previewInputs.clear();
        track.state = QStringLiteral("cancelled");
        discarded = true;
    }
    if (!discarded) return;
    setError(QStringLiteral("Frame %1 was replaced; tracking results measured on it were "
                            "discarded. Rerun the analysis.").arg(frame));
    emit changed();
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
    const QSize sourceSize = m_frameSource.value(track.referenceFrame);
    if (reference.isNull() || sourceSize.isEmpty()) {
        setError(QStringLiteral("Reference patch is missing, out of bounds or lacks texture."));
        return false;
    }
    // AGENT-NOTE: positions are source pixels; the proxy may be a downscale
    // (TrackingEngine::proxy), so the job runs in proxy pixels and samples are
    // mapped back. Radii are proxy pixels (equal to source pixels up to 4 MP).
    const double sx = double(reference.width()) / sourceSize.width();
    const double sy = double(reference.height()) / sourceSize.height();
    const QPointF seed(track.seedX * sx, track.seedY * sy);
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
    job.motion = track.motion == QLatin1String("similarity") ? TrackingEngine::Motion::Similarity
                                                             : TrackingEngine::Motion::Translation;
    QHash<int, quint64> inputs{{track.referenceFrame, m_frameVersions.value(track.referenceFrame)}};
    for (int frame = startFrame;; frame += step) {
        const QImage image = m_frames.value(frame);
        if (image.isNull() || image.size() != reference.size()
            || m_frameSource.value(frame) != sourceSize) {
            setError(QStringLiteral("Every analysis frame must be loaded at the reference size."));
            return false;
        }
        job.frames.append(frame);
        job.images.append(image);
        inputs.insert(frame, m_frameVersions.value(frame));
        if (frame == endFrame) break;
    }
    // The search starts from the accepted neighbour before the range when it
    // is trusted, otherwise from the reference seed. The engine works relative
    // to the reference pose, so a rebased reference keeps absolute angles.
    const Point before = track.points.value(startFrame - step);
    const bool trusted = track.points.contains(startFrame - step) && before.state != QLatin1String("lost");
    job.start = trusted ? QPointF(before.x * sx, before.y * sy) : seed;
    job.startRotation = trusted ? before.rotation - track.referenceRotation : 0.0;
    job.startScale = trusted ? before.scale / track.referenceScale : 1.0;

    track.preview.clear();
    track.previewInputs = inputs;
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
    const double baseRotation = track.referenceRotation, baseScale = track.referenceScale;
    // AGENT-NOTE: the worker owns shared proxy copies (TrackingEngine::Job)
    // and posts each sample back; stale jobs are ignored by ID, so cancel
    // is immediate on this thread even while the worker finishes a frame.
    m_analysisFuture = QtConcurrent::run([this, id, token, sx, sy, baseRotation, baseScale,
                                          job = std::move(job)] {
        const auto post = [this, id, sx, sy, baseRotation, baseScale](const TrackingEngine::Sample &s) {
            Point point{s.frame, s.x / sx, s.y / sy, s.confidence, s.rotation + baseRotation,
                        s.scale * baseScale, s.lost ? QStringLiteral("lost") : QStringLiteral("tracked"),
                        QStringLiteral("generated")};
            QMetaObject::invokeMethod(this, [this, id, point] { onAnalysisSample(id, point); },
                                      Qt::QueuedConnection);
        };
        const TrackingEngine::End end = TrackingEngine::run(job, token, post);
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

void TrackingRotoModel::onAnalysisSample(quint64 job, const Point &point)
{
    // AGENT-GUARD: both checks are needed: `running` drops samples after a
    // cancel, the job ID drops a cancelled job's queued samples once a new
    // job runs (tst_tracking_roto_model cancelThenRestartDropsLateSamples).
    if (!m_analysis.running || m_analysis.job != job) return;
    const int index = trackIndex(m_analysis.trackId);
    if (index < 0) { cancelAnalysis(); return; }
    m_tracks[index].preview.insert(point.frame, point);
    ++m_analysis.done;
    m_analysis.lastFrame = point.frame;
    m_analysis.lastConfidence = point.confidence;
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
        m_tracks[index].previewInputs.clear();
    }
    m_analysis.running = false;
    emit changed(); emit analysisChanged();
}

bool TrackingRotoModel::acceptAnalysis(const QString &trackId)
{
    const int index = trackIndex(trackId);
    if (index < 0 || m_analysis.running || m_tracks[index].preview.isEmpty()
        || m_tracks[index].state == QLatin1String("cancelled")) return false;
    const Track &current = m_tracks.at(index);
    if (!previewCurrent(current)) {
        setError(QStringLiteral("Frames changed since this analysis ran; rerun it."));
        return false;
    }
    qsizetype added = 0;
    for (auto it = current.preview.cbegin(); it != current.preview.cend(); ++it)
        if (!current.points.contains(it.key())) ++added;
    if (current.points.size() + added > kMaxTrackPoints) {
        setError(QStringLiteral("A track holds at most %1 points.")
                 .arg(kMaxTrackPoints));
        return false;
    }
    remember();
    Track &track = m_tracks[index];
    for (auto it = track.preview.cbegin(); it != track.preview.cend(); ++it)
        track.points.insert(it.key(), it.value());
    track.preview.clear();
    track.previewInputs.clear();
    setError({});
    emit changed();
    return true;
}

} // namespace QindaTK
