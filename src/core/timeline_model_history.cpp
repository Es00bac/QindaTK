// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_codec.h"
#include "timeline_model.h"

#include <algorithm>

namespace QindaTK {
namespace {
constexpr int kMaxExternalSteps = 4096;

OperationError diverged(const QString &message)
{
    return {ErrorCode::Conflict, message, {}, {}};
}

// A musically anchored marker's position is derived from its musical tick
// and the current tempo map, so preconditions compare the canonical fields.
bool sameMarker(const TimelineMarker &current, const TimelineMarker &expected)
{
    if (current.anchor != expected.anchor) return false;
    if (current.anchor == TimelineMarker::Anchor::Musical) {
        TimelineMarker a = current, b = expected;
        a.position = b.position = 0;
        return a == b;
    }
    return current == expected;
}

// Likewise a beat-locked clip's start is derived from its musical anchor.
bool sameClip(const TimelineClip &current, const TimelineClip &expected)
{
    if (current.anchor == TimelineClip::Anchor::Musical && expected.anchor == current.anchor) {
        TimelineClip a = current, b = expected;
        a.start = b.start = 0;
        return a == b;
    }
    return current == expected;
}

quint64 textCost(const QString &text)
{
    return quint64(text.size()) * sizeof(QChar);
}
} // namespace

quint64 TimelineModel::cost(const TimelineChangeSet &changes)
{
    quint64 bytes = sizeof(TimelineChangeSet) + textCost(changes.label) + textCost(changes.transactionId);
    for (const auto &change : changes.changes) {
        bytes += sizeof(TimelineChange);
        for (const auto *clip : {change.clipBefore ? &*change.clipBefore : nullptr,
                                 change.clipAfter ? &*change.clipAfter : nullptr})
            if (clip) bytes += textCost(clip->label) + textCost(clip->resourceId)
                             + textCost(clip->color) + textCost(clip->kind)
                             + textCost(clip->transition.type)
                             + quint64(clip->transition.parameters.size()) * 64;
        for (const auto *marker : {change.markerBefore ? &*change.markerBefore : nullptr,
                                   change.markerAfter ? &*change.markerAfter : nullptr})
            if (marker) bytes += textCost(marker->label) + textCost(marker->color) + textCost(marker->kind);
        for (const auto *track : {change.trackBefore ? &*change.trackBefore : nullptr,
                                  change.trackAfter ? &*change.trackAfter : nullptr})
            if (track) bytes += textCost(track->name) + textCost(track->routing)
                              + textCost(track->kind) + textCost(track->color);
        bytes += quint64(change.orderBefore.size() + change.orderAfter.size()) * sizeof(QUuid);
    }
    return bytes;
}

OperationError TimelineModel::preflight(const TimelineChangeSet &changes, bool reverse) const
{
    QSet<QUuid> seenTracks, seenClips, seenMarkers, createdTracks, removedTracks;
    bool seenOrder = false;
    QVector<QUuid> nextOrder = m_trackOrder;
    for (const auto &change : changes.changes) {
        switch (change.kind) {
        case TimelineChange::Kind::Track: {
            const auto &expected = reverse ? change.trackAfter : change.trackBefore;
            const auto &next = reverse ? change.trackBefore : change.trackAfter;
            if (seenTracks.contains(change.id)) return diverged(tr("A track appears twice in one edit"));
            seenTracks.insert(change.id);
            const auto *current = trackRecord(change.id);
            if (bool(current) != expected.has_value() || (current && !(*current == *expected)))
                return diverged(tr("The track no longer matches this edit"));
            if (!expected && (clipRecord(change.id) || markerRecord(change.id)))
                return diverged(tr("A new track would reuse an existing ID"));
            if (!expected && next) createdTracks.insert(change.id);
            if (expected && !next) removedTracks.insert(change.id);
            break;
        }
        case TimelineChange::Kind::TrackOrder: {
            if (seenOrder) return diverged(tr("Track order appears twice in one edit"));
            seenOrder = true;
            if (m_trackOrder != (reverse ? change.orderAfter : change.orderBefore))
                return diverged(tr("The track order no longer matches this edit"));
            nextOrder = reverse ? change.orderBefore : change.orderAfter;
            break;
        }
        case TimelineChange::Kind::Clip: {
            const auto &expected = reverse ? change.clipAfter : change.clipBefore;
            if (seenClips.contains(change.id)) return diverged(tr("A clip appears twice in one edit"));
            seenClips.insert(change.id);
            const auto *current = clipRecord(change.id);
            if (bool(current) != expected.has_value() || (current && !sameClip(*current, *expected)))
                return diverged(tr("A clip no longer matches this edit"));
            if (!expected && (trackRecord(change.id) || markerRecord(change.id)))
                return diverged(tr("A new clip would reuse an existing ID"));
            break;
        }
        case TimelineChange::Kind::Marker: {
            const auto &expected = reverse ? change.markerAfter : change.markerBefore;
            if (seenMarkers.contains(change.id)) return diverged(tr("A marker appears twice in one edit"));
            seenMarkers.insert(change.id);
            const auto *current = markerRecord(change.id);
            if (bool(current) != expected.has_value() || (current && !sameMarker(*current, *expected)))
                return diverged(tr("A marker no longer matches this edit"));
            if (!expected && (trackRecord(change.id) || clipRecord(change.id)))
                return diverged(tr("A new marker would reuse an existing ID"));
            break;
        }
        }
    }
    // The resulting document must stay referentially whole.
    QSet<QUuid> tracksAfter;
    for (auto it = m_tracks.cbegin(); it != m_tracks.cend(); ++it)
        if (!removedTracks.contains(it.key())) tracksAfter.insert(it.key());
    for (const auto &id : createdTracks) tracksAfter.insert(id);
    if (nextOrder.size() != tracksAfter.size())
        return diverged(tr("The track order does not list every track"));
    for (const auto &id : nextOrder)
        if (!tracksAfter.contains(id)) return diverged(tr("The track order lists an unknown track"));
    for (const auto &change : changes.changes) {
        if (change.kind != TimelineChange::Kind::Clip) continue;
        const auto &next = reverse ? change.clipBefore : change.clipAfter;
        if (next && !tracksAfter.contains(next->trackId))
            return diverged(tr("A clip would reference a missing track"));
    }
    for (const auto &trackId : removedTracks) {
        for (const auto &entry : trackIndex(trackId))
            if (!seenClips.contains(entry.id)) return diverged(tr("A removed track still holds clips"));
    }
    return {};
}

OperationError TimelineModel::applyChangeSet(const TimelineChangeSet &changes, bool reverse)
{
    if (auto failure = preflight(changes, reverse)) return failure;
    if (m_session) {
        invalidateSession(tr("The timeline changed"));
        endSession();
    }
    bool structure = false, clips = false, markers = false;
    QSet<QUuid> touchedTracks, changedClips;
    QStringList removed;
    for (const auto &change : changes.changes) {
        switch (change.kind) {
        case TimelineChange::Kind::Track: {
            const auto &next = reverse ? change.trackBefore : change.trackAfter;
            structure = true;
            if (next) m_tracks.insert(change.id, *next);
            else {
                m_tracks.remove(change.id);
                m_indexes.remove(change.id);
            }
            break;
        }
        case TimelineChange::Kind::TrackOrder:
            structure = true;
            m_trackOrder = reverse ? change.orderBefore : change.orderAfter;
            break;
        case TimelineChange::Kind::Clip: {
            const auto &previous = reverse ? change.clipAfter : change.clipBefore;
            const auto &next = reverse ? change.clipBefore : change.clipAfter;
            clips = true;
            changedClips.insert(change.id);
            if (previous) touchedTracks.insert(previous->trackId);
            if (next) {
                touchedTracks.insert(next->trackId);
                m_clips.insert(change.id, withDerivedStart(*next));
            } else {
                m_clips.remove(change.id);
                removed.push_back(change.id.toString(QUuid::WithoutBraces));
            }
            break;
        }
        case TimelineChange::Kind::Marker: {
            const auto &next = reverse ? change.markerBefore : change.markerAfter;
            markers = true;
            if (next) m_markers.insert(change.id, withDerivedPosition(*next));
            else m_markers.remove(change.id);
            break;
        }
        }
    }
    for (const auto &trackId : m_trackOrder)
        if (!m_indexes.contains(trackId)) m_indexes.insert(trackId, {});
    for (const auto &trackId : touchedTracks) {
        auto index = m_indexes.find(trackId);
        if (index == m_indexes.end()) continue;
        auto &entries = index->entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const IndexEntry &e) { return changedClips.contains(e.id); }),
                      entries.end());
        for (const auto &id : changedClips) {
            const auto it = m_clips.constFind(id);
            if (it != m_clips.cend() && it->trackId == trackId)
                entries.push_back({it->start, it->end(), it->id});
        }
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return a.start != b.start ? a.start < b.start : a.id < b.id;
        });
        index->maxDuration = 0;
        index->maxEnd = std::numeric_limits<qint64>::min();
        for (const auto &entry : entries) {
            index->maxDuration = std::max(index->maxDuration, entry.end - entry.start);
            index->maxEnd = std::max(index->maxEnd, entry.end);
        }
    }
    if (clips) m_clipEdgesDirty = true;
    if (markers) m_markerEdgesDirty = true;
    recomputeContentEnd();
    ++m_revision;
    emitAfterApply(structure, clips, markers, removed);
    return {};
}

void TimelineModel::emitAfterApply(bool structure, bool clips, bool markers,
                                   const QStringList &removed)
{
    if (structure) emit structureChanged();
    if (clips) emit clipsChanged();
    if (markers) emit markersChanged();
    if (!removed.isEmpty()) emit clipsRemoved(removed);
    emit revisionChanged();
}

void TimelineModel::recordHistory(const TimelineChangeSet &changes, quint64 bytes)
{
    if (m_externalUndo) {
        m_externalSteps.insert(changes.transactionId, changes);
        m_externalOrder.push_back(changes.transactionId);
        while (m_externalOrder.size() > kMaxExternalSteps)
            m_externalSteps.remove(m_externalOrder.takeFirst());
        return;
    }
    while (m_history.size() > m_cursor) {
        m_historyBytes -= m_history.last().bytes;
        m_history.removeLast();
    }
    m_history.push_back({changes, bytes});
    m_historyBytes += bytes;
    m_cursor = m_history.size();
    while (!m_history.isEmpty()
           && (m_history.size() > m_historyLimit || m_historyBytes > m_historyBudget)) {
        m_historyBytes -= m_history.first().bytes;
        m_history.removeFirst();
        m_cursor = std::max<qsizetype>(0, m_cursor - 1);
    }
    emit historyChanged();
}

bool TimelineModel::canUndo() const
{
    return !m_externalUndo && m_cursor > 0;
}

bool TimelineModel::canRedo() const
{
    return !m_externalUndo && m_cursor < m_history.size();
}

QString TimelineModel::undoText() const
{
    return canUndo() ? m_history.at(m_cursor - 1).changes.label : QString();
}

QString TimelineModel::redoText() const
{
    return canRedo() ? m_history.at(m_cursor).changes.label : QString();
}

// AGENT-GUARD: a lower limit drops the oldest undo steps first and only then
// the redo steps farthest from the cursor. Dropping the nearest redo step
// would leave later redo steps whose preconditions can never match.
void TimelineModel::setHistoryLimit(int steps)
{
    steps = std::clamp(steps, 1, 10000);
    if (m_historyLimit == steps) return;
    m_historyLimit = steps;
    while (m_history.size() > m_historyLimit && m_cursor > 0) {
        m_historyBytes -= m_history.first().bytes;
        m_history.removeFirst();
        --m_cursor;
    }
    while (m_history.size() > m_historyLimit) {
        m_historyBytes -= m_history.last().bytes;
        m_history.removeLast();
    }
    emit historyChanged();
}

void TimelineModel::setExternalUndo(bool enabled)
{
    if (m_externalUndo == enabled) return;
    m_externalUndo = enabled;
    m_history.clear();
    m_cursor = 0;
    m_historyBytes = 0;
    m_externalSteps.clear();
    m_externalOrder.clear();
    emit historyChanged();
}

void TimelineModel::clearHistory()
{
    m_history.clear();
    m_cursor = 0;
    m_historyBytes = 0;
    m_externalSteps.clear();
    m_externalOrder.clear();
    emit historyChanged();
}

bool TimelineModel::undo()
{
    if (m_externalUndo) return fail(tr("Undo belongs to the host history"));
    if (m_cursor <= 0) return false;
    if (m_session) {
        invalidateSession(tr("Undo"));
        endSession();
    }
    if (auto failure = applyChangeSet(m_history.at(m_cursor - 1).changes, true)) {
        // AGENT-GUARD: a precondition mismatch means the history no longer
        // describes this document; replaying it could edit the wrong state.
        clearHistory();
        return fail(failure.message);
    }
    --m_cursor;
    emit historyChanged();
    return true;
}

bool TimelineModel::redo()
{
    if (m_externalUndo) return fail(tr("Redo belongs to the host history"));
    if (m_cursor >= m_history.size()) return false;
    if (m_session) {
        invalidateSession(tr("Redo"));
        endSession();
    }
    if (auto failure = applyChangeSet(m_history.at(m_cursor).changes, false)) {
        clearHistory();
        return fail(failure.message);
    }
    ++m_cursor;
    emit historyChanged();
    return true;
}

const TimelineChangeSet *TimelineModel::externalStep(const QString &transactionId) const
{
    const auto it = m_externalSteps.constFind(transactionId);
    return it == m_externalSteps.cend() ? nullptr : &it.value();
}

bool TimelineModel::applyHistoryChanges(const QString &transactionId, bool reverse,
                                        const QString &expectedRevision)
{
    if (!m_externalUndo) return fail(tr("History replay requires externalUndo"));
    const auto *step = externalStep(transactionId);
    if (!step) return fail(tr("Unknown or released timeline transaction"));
    const auto expected = RevisionId::parseDecimal(expectedRevision);
    if (!expected || expected->value != m_revision)
        return fail(tr("The timeline revision does not match the history request"));
    const TimelineChangeSet copy = *step;
    if (auto failure = applyChangeSet(copy, reverse)) return fail(failure.message);
    return true;
}

bool TimelineModel::releaseHistoryTransaction(const QString &transactionId)
{
    if (!m_externalSteps.remove(transactionId)) return false;
    m_externalOrder.removeAll(transactionId);
    return true;
}

QVariantList TimelineModel::changePayload(const TimelineChangeSet &changes) const
{
    QVariantList payload;
    payload.reserve(changes.changes.size());
    for (const auto &change : changes.changes) payload.push_back(TimelineCodec::changeToMap(change));
    return payload;
}

} // namespace QindaTK
