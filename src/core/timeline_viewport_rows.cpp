// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_viewport.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QString text(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

qint64 saturatingAdd(qint64 value, qint64 delta)
{
    if (const auto sum = TimelineTime::add(value, delta)) return *sum;
    return delta > 0 ? std::numeric_limits<qint64>::max() : std::numeric_limits<qint64>::min();
}

bool clipOrder(const TimelineClip &a, const TimelineClip &b)
{
    return a.start != b.start ? a.start < b.start : a.id < b.id;
}

struct ClipRow {
    TimelineClip clip;
    bool ghost = false;
    bool preview = false;
};
} // namespace

double TimelineViewport::heightFor(const TimelineTrack &track) const
{
    const double height = track.height > 0 ? double(track.height) : m_defaultTrackHeight;
    return std::max(m_minimumTrackHeight, height);
}

QVector<TimelineClip> TimelineViewport::clipsOnTrack(const QUuid &trackId, qint64 start,
                                                     qint64 end) const
{
    QVector<TimelineClip> result;
    if (!m_model || end <= start) return result;
    for (const auto &id : m_model->clipsInRange(trackId, start, end)) {
        if (m_model->previewClipChange(id)) continue;
        result.push_back(*m_model->clipRecord(id));
    }
    if (const auto *plan = m_model->previewPlan()) {
        for (const auto &change : plan->changes.changes) {
            if (change.kind != TimelineChange::Kind::Clip || !change.clipAfter) continue;
            const TimelineClip &clip = *change.clipAfter;
            if (clip.trackId == trackId && clip.start < end && clip.end() > start) result.push_back(clip);
        }
    }
    std::sort(result.begin(), result.end(), clipOrder);
    return result;
}

QVector<TimelineClip> TimelineViewport::ghostsOnTrack(const QUuid &trackId, qint64 start,
                                                      qint64 end) const
{
    QVector<TimelineClip> result;
    if (!m_model || !m_showGhosts || !m_model->hasPreview() || end <= start) return result;
    for (const auto &id : m_model->clipsInRange(trackId, start, end)) {
        const auto *change = m_model->previewClipChange(id);
        if (!change) continue;
        const auto *clip = m_model->clipRecord(id);
        // A clip that keeps its place (e.g. the left part of a split) needs no ghost.
        if (change->clipAfter && change->clipAfter->trackId == clip->trackId
            && change->clipAfter->start == clip->start && change->clipAfter->duration == clip->duration)
            continue;
        result.push_back(*clip);
    }
    return result;
}

void TimelineViewport::refresh()
{
    if (m_batch) return;
    if (!m_model) {
        m_order.clear();
        m_slotIndex.clear();
        m_contentHeight = 0;
        m_firstTrack = m_lastTrack = -1;
        m_summarized = 0;
        m_truncated = false;
        m_clips->update({});
        m_tracks->update({});
        m_markers->update({});
        emit layoutChanged();
        emit refreshed();
        return;
    }
    rebuildLayout();
    const qint64 overscan = ticksForLength(m_overscan).value_or(0);
    const qint64 width = ticksForLength(m_width).value_or(0);
    m_visibleStart = m_origin;
    m_visibleEnd = saturatingAdd(m_origin, width);
    const qint64 start = saturatingAdd(m_origin, -overscan);
    const qint64 end = saturatingAdd(m_visibleEnd, overscan);
    rebuildClips(start, std::max(end, saturatingAdd(start, 1)));
    rebuildMarkers(start, std::max(end, saturatingAdd(start, 1)));
    emit layoutChanged();
    emit refreshed();
}

void TimelineViewport::rebuildLayout()
{
    const QVector<QUuid> order = m_model->effectiveVisibleTrackOrder();
    m_order.clear();
    m_slotIndex.clear();
    double top = 0;
    for (const auto &id : order) {
        const auto track = m_model->effectiveTrack(id);
        if (!track) continue;
        const double height = heightFor(*track);
        m_slotIndex.insert(id, int(m_order.size()));
        m_order.push_back({id, top, height});
        top += height;
    }
    m_contentHeight = top;
    m_scrollY = std::clamp(m_scrollY, 0.0, maximumScrollY());
    m_firstTrack = m_lastTrack = -1;
    for (int i = 0; i < m_order.size(); ++i) {
        const TrackSlot &slot = m_order.at(i);
        if (slot.top + slot.height <= m_scrollY) continue;
        if (slot.top >= m_scrollY + m_height) break;
        if (m_firstTrack < 0) m_firstTrack = i;
        m_lastTrack = i;
    }
    QVector<QVariantList> rows;
    const QVector<QUuid> &fullOrder = m_model->trackOrder();
    const auto *plan = m_model->previewPlan();
    bool orderPreview = false;
    if (plan) {
        for (const auto &change : plan->changes.changes)
            if (change.kind == TimelineChange::Kind::TrackOrder) orderPreview = true;
    }
    for (int i = m_firstTrack; i >= 0 && i <= m_lastTrack; ++i) {
        const TrackSlot &slot = m_order.at(i);
        const auto track = m_model->effectiveTrack(slot.id);
        const auto *committed = m_model->trackRecord(slot.id);
        const bool preview = orderPreview || !committed || !(*committed == *track);
        rows.push_back({text(slot.id), text(slot.id), i, int(fullOrder.indexOf(slot.id)),
                        slot.top - m_scrollY, slot.height, track->name, track->kind, track->muted,
                        track->solo, track->armed, track->locked, track->hidden, track->routing,
                        track->color, int(m_model->trackIndex(slot.id).size()), preview});
    }
    m_tracks->update(rows);
}

void TimelineViewport::rebuildClips(qint64 start, qint64 end)
{
    m_summarized = 0;
    m_truncated = false;
    if (m_firstTrack < 0) {
        m_clips->update({});
        return;
    }
    const auto *plan = m_model->previewPlan();
    const bool invalid = plan && plan->error;
    // Gather effective clips and ghosts per visible track once.
    QVector<QVector<ClipRow>> perTrack;
    for (int i = m_firstTrack; i <= m_lastTrack; ++i) {
        const QUuid id = m_order.at(i).id;
        QVector<ClipRow> list;
        for (const auto &clip : ghostsOnTrack(id, start, end)) list.push_back({clip, true, false});
        for (const auto &clip : clipsOnTrack(id, start, end))
            list.push_back({clip, false, m_model->previewClipChange(clip.id) != nullptr});
        std::stable_sort(list.begin(), list.end(), [](const ClipRow &a, const ClipRow &b) {
            if (a.clip.start != b.clip.start) return a.clip.start < b.clip.start;
            if (a.ghost != b.ghost) return a.ghost;
            return a.clip.id < b.clip.id;
        });
        perTrack.push_back(list);
    }
    const auto row = [&](const ClipRow &entry, int trackIndex, int layer) {
        const TimelineClip &clip = entry.clip;
        const qint64 drawnStart = std::max(clip.start, start);
        const qint64 drawnEnd = std::min(clip.end(), end);
        const double x = xFor(drawnStart);
        const auto *track = m_model->trackRecord(clip.trackId);
        const TrackSlot &slot = m_order.at(trackIndex);
        const auto sourceStart = TimelineTime::add(clip.sourceOffset, drawnStart - clip.start);
        const auto sourceEnd = TimelineTime::add(clip.sourceOffset, drawnEnd - clip.start);
        const QString key = entry.ghost ? text(clip.id) + QStringLiteral(":ghost") : text(clip.id);
        return QVariantList{key, text(clip.id), text(clip.trackId), trackIndex, x,
                            slot.top - m_scrollY, std::max(0.0, xFor(drawnEnd) - x), slot.height,
                            clip.label, clip.color, clip.kind, clip.resourceId,
                            clip.locked || (track && track->locked), QString::number(clip.start),
                            QString::number(clip.duration), QString::number(clip.end()),
                            QString::number(clip.sourceOffset), QString::number(clip.sourceDuration),
                            sourceStart ? QString::number(*sourceStart) : QString(),
                            sourceEnd ? QString::number(*sourceEnd) : QString(),
                            clip.start < start, clip.end() > end, entry.preview, entry.ghost,
                            entry.preview && invalid, false, 0,
                            entry.ghost ? -1 : (entry.preview ? layer + 100000 : layer)};
    };
    QVector<QVariantList> rows;
    double threshold = m_summaryPixels;
    for (int attempt = 0; attempt < 10; ++attempt) {
        rows.clear();
        int summarized = 0;
        for (int t = 0; t < perTrack.size(); ++t) {
            const int trackIndex = m_firstTrack + t;
            const auto &list = perTrack.at(t);
            const TrackSlot &slot = m_order.at(trackIndex);
            int layer = 0;
            for (int i = 0; i < list.size();) {
                const ClipRow &entry = list.at(i);
                const auto tiny = [&](const ClipRow &candidate) {
                    return !candidate.ghost && !candidate.preview
                        && xFor(candidate.clip.end()) - xFor(candidate.clip.start) < threshold;
                };
                int last = i;
                if (tiny(entry)) {
                    qint64 runEnd = entry.clip.end();
                    while (last + 1 < list.size() && tiny(list.at(last + 1))
                           && xFor(list.at(last + 1).clip.start) - xFor(runEnd) < threshold) {
                        ++last;
                        runEnd = std::max(runEnd, list.at(last).clip.end());
                    }
                }
                if (last > i) {
                    // AGENT-NOTE: clips narrower than summaryPixels merge into one
                    // summary row; the row is presentation only and carries a count.
                    qint64 runEnd = entry.clip.end();
                    for (int k = i; k <= last; ++k) runEnd = std::max(runEnd, list.at(k).clip.end());
                    const qint64 drawnStart = std::max(entry.clip.start, start);
                    const qint64 drawnEnd = std::min(runEnd, end);
                    const double x = xFor(drawnStart);
                    rows.push_back({QStringLiteral("summary:%1:%2").arg(text(slot.id), text(entry.clip.id)),
                                    QString(), text(slot.id), trackIndex, x, slot.top - m_scrollY,
                                    std::max(0.0, xFor(drawnEnd) - x), slot.height, QString(), QString(),
                                    QString(), QString(), false, QString::number(entry.clip.start),
                                    QString::number(runEnd - entry.clip.start), QString::number(runEnd),
                                    QString(), QString(), QString(), QString(), entry.clip.start < start,
                                    runEnd > end, false, false, false, true, last - i + 1, layer++});
                    summarized += last - i + 1;
                } else {
                    rows.push_back(row(entry, trackIndex, layer++));
                }
                i = last + 1;
            }
        }
        m_summarized = summarized;
        if (rows.size() <= m_maximumClipRows) break;
        threshold = std::max(threshold, 1.0) * 2.0;
    }
    if (rows.size() > m_maximumClipRows) {
        rows.resize(m_maximumClipRows);
        m_truncated = true;
    }
    m_clips->update(rows);
}

void TimelineViewport::rebuildMarkers(qint64 start, qint64 end)
{
    QVector<QPair<TimelineMarker, int>> entries; // 0 committed, 1 preview, 2 ghost
    for (const auto &id : m_model->markersInRange(start, end, 4096)) {
        if (const auto *change = m_model->previewMarkerChange(id)) {
            if (m_showGhosts && change->markerAfter) entries.push_back({*m_model->markerRecord(id), 2});
            continue;
        }
        entries.push_back({*m_model->markerRecord(id), 0});
    }
    if (const auto *plan = m_model->previewPlan()) {
        for (const auto &change : plan->changes.changes) {
            if (change.kind != TimelineChange::Kind::Marker || !change.markerAfter) continue;
            const TimelineMarker &marker = *change.markerAfter;
            if (marker.position < end && marker.position + marker.duration >= start)
                entries.push_back({marker, 1});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        if (a.first.position != b.first.position) return a.first.position < b.first.position;
        return a.first.id < b.first.id;
    });
    QVector<QVariantList> rows;
    for (const auto &entry : entries) {
        const TimelineMarker &marker = entry.first;
        const qint64 markerEnd = marker.position + marker.duration;
        const qint64 drawnStart = std::max(marker.position, start);
        const double x = xFor(drawnStart);
        const double width = marker.duration > 0 ? std::max(0.0, xFor(std::min(markerEnd, end)) - x) : 0.0;
        const QString key = entry.second == 2 ? text(marker.id) + QStringLiteral(":ghost") : text(marker.id);
        rows.push_back({key, text(marker.id), x, width, QString::number(marker.position),
                        QString::number(marker.duration), QString::number(markerEnd), marker.label,
                        marker.color, marker.kind, marker.locked,
                        marker.anchor == TimelineMarker::Anchor::Musical ? QStringLiteral("musical")
                                                                         : QStringLiteral("time"),
                        entry.second == 1, entry.second == 2, marker.position < start, markerEnd > end});
    }
    m_markers->update(rows);
}

} // namespace QindaTK
