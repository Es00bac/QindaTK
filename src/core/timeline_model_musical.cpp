// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_model.h"

#include <algorithm>

namespace QindaTK {

TimelineMarker TimelineModel::withDerivedPosition(TimelineMarker marker) const
{
    if (marker.anchor != TimelineMarker::Anchor::Musical) return marker;
    const qint64 latest = std::numeric_limits<qint64>::max() - std::max<qint64>(0, marker.duration);
    if (const auto position = ticksForMusicalTick(marker.musicalTick)) marker.position = std::min(*position, latest);
    return marker;
}

TimelineClip TimelineModel::anchoredClip(TimelineClip clip) const
{
    if (clip.anchor != TimelineClip::Anchor::Musical) {
        clip.musicalTick = clip.musicalOffset = 0;
        return clip;
    }
    // AGENT-GUARD: the offset is measured from the same derived anchor that
    // withDerivedStart() adds it back to, so an unchanged map reproduces
    // `start` exactly and undoing a tempo edit restores every clip bit-exact.
    const auto tick = musicalTickFor(clip.start);
    const auto anchor = tick ? ticksForMusicalTick(*tick) : std::nullopt;
    const auto offset = anchor ? TimelineTime::subtract(clip.start, *anchor) : std::nullopt;
    if (!offset) return clip;
    clip.musicalTick = *tick;
    clip.musicalOffset = *offset;
    return clip;
}

TimelineClip TimelineModel::withDerivedStart(TimelineClip clip) const
{
    if (clip.anchor != TimelineClip::Anchor::Musical) return clip;
    const auto anchor = ticksForMusicalTick(clip.musicalTick);
    if (!anchor) return clip;
    const qint64 latest = std::numeric_limits<qint64>::max() - clip.duration;
    const auto start = TimelineTime::add(*anchor, clip.musicalOffset);
    clip.start = start ? std::min(*start, latest) : (clip.musicalOffset < 0
                                                         ? std::numeric_limits<qint64>::min() : latest);
    return clip;
}

void TimelineModel::resolveMusicalAnchors()
{
    // Any time-map change (tempo, meter, frame or sample rate, origin) retires
    // an active edit: its preview snapped and derived musical ticks under the
    // old map, and committing it would store what was never shown.
    invalidateSession(tr("The time map changed"));
    bool markersMoved = false;
    for (auto it = m_markers.begin(); it != m_markers.end(); ++it) {
        if (it->anchor != TimelineMarker::Anchor::Musical) continue;
        const TimelineMarker derived = withDerivedPosition(*it);
        if (derived.position == it->position) continue;
        it->position = derived.position;
        markersMoved = true;
    }
    QSet<QUuid> tracks;
    for (auto it = m_clips.begin(); it != m_clips.end(); ++it) {
        if (it->anchor != TimelineClip::Anchor::Musical) continue;
        const qint64 start = withDerivedStart(*it).start;
        if (start == it->start) continue;
        it->start = start;
        tracks.insert(it->trackId);
    }
    if (!markersMoved && tracks.isEmpty()) return;
    // AGENT-NOTE: a tempo change moves beat-locked clips and musically
    // anchored markers without a history step; their canonical musical
    // anchors are unchanged, so the tempo edit's own undo restores them. The
    // revision still advances so requests based on old positions go stale.
    for (const auto &trackId : tracks) {
        auto index = m_indexes.find(trackId);
        if (index == m_indexes.end()) continue;
        index->maxDuration = 0;
        index->maxEnd = std::numeric_limits<qint64>::min();
        for (auto &entry : index->entries) {
            const auto &clip = m_clips[entry.id];
            entry.start = clip.start;
            entry.end = clip.end();
            index->maxDuration = std::max(index->maxDuration, entry.end - entry.start);
            index->maxEnd = std::max(index->maxEnd, entry.end);
        }
        std::sort(index->entries.begin(), index->entries.end(), [](const auto &a, const auto &b) {
            return a.start != b.start ? a.start < b.start : a.id < b.id;
        });
    }
    ++m_revision;
    if (!tracks.isEmpty()) m_clipEdgesDirty = true;
    if (markersMoved) m_markerEdgesDirty = true;
    recomputeContentEnd();
    if (!tracks.isEmpty()) emit clipsChanged();
    if (markersMoved) emit markersChanged();
    emit revisionChanged();
}

OperationError TimelineModel::musicalPlacementError() const
{
    const auto *rules = policy();
    QSet<QUuid> tracks;
    for (auto it = m_clips.cbegin(); it != m_clips.cend(); ++it) {
        if (it->anchor != TimelineClip::Anchor::Musical) continue;
        if (!rules->allowNegativeTime() && it->start < 0)
            return {ErrorCode::Validation,
                    tr("Beat-locked clip “%1” would start before zero").arg(it->label), {}, {}};
        tracks.insert(it->trackId);
    }
    if (rules->overlapMode() != TimelineEditPolicy::RejectOverlap) return {};
    for (const auto &trackId : tracks) {
        const auto &entries = trackIndex(trackId);
        qint64 reach = std::numeric_limits<qint64>::min();
        QUuid reachId;
        for (const auto &entry : entries) {
            if (entry.start < reach) {
                const auto *a = clipRecord(reachId);
                const auto *b = clipRecord(entry.id);
                return {ErrorCode::Conflict, tr("Clip “%1” would overlap “%2”")
                            .arg(b ? b->label : QString(), a ? a->label : QString()), {}, {}};
            }
            if (entry.end > reach) {
                reach = entry.end;
                reachId = entry.id;
            }
        }
    }
    return {};
}

QString TimelineModel::musicalTickAt(const QString &ticks) const
{
    const auto value = TimelineTime::parseDecimal(ticks);
    const auto tick = value ? musicalTickFor(*value) : std::nullopt;
    return tick ? QString::number(*tick) : QString();
}

QString TimelineModel::ticksAtMusicalTick(const QString &musicalTick) const
{
    const auto value = TimelineTime::parseDecimal(musicalTick);
    const auto ticks = value ? ticksForMusicalTick(*value) : std::nullopt;
    return ticks ? QString::number(*ticks) : QString();
}

} // namespace QindaTK
