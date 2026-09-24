// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_model.h"

#include <algorithm>
#include <numeric>

namespace QindaTK {
namespace {
const std::vector<TimelineModel::IndexEntry> &emptyIndex()
{
    static const std::vector<TimelineModel::IndexEntry> empty;
    return empty;
}

qint64 saturatingSubtract(qint64 left, qint64 right)
{
    const auto value = TimelineTime::subtract(left, right);
    if (value) return *value;
    return right > 0 ? std::numeric_limits<qint64>::min() : std::numeric_limits<qint64>::max();
}
} // namespace

TimelineModel::TimelineModel(QObject *parent)
    : QObject(parent),
      m_documentId(DocumentId::create()),
      m_timebase{TimelineTime::kDefaultTicksPerSecond, 1},
      m_defaultTimeMap(new TimeMap(this)),
      m_defaultPolicy(new TimelineEditPolicy(this))
{
    m_timeMapConnection = connect(m_defaultTimeMap, &TimeMap::changed, this,
                                  &TimelineModel::resolveMusicalAnchors);
    connect(m_defaultPolicy, &TimelineEditPolicy::changed, this, [this] {
        if (!m_policy) invalidateSession(tr("The edit policy changed"));
    });
}

TimelineModel::~TimelineModel()
{
    m_session.reset();
}

SnapshotHeader TimelineModel::snapshotHeader() const
{
    return {schemaId(), m_documentId, RevisionId{m_revision}, m_generation};
}

TimeMap *TimelineModel::timeMap() const
{
    return m_timeMap ? m_timeMap.data() : m_defaultTimeMap;
}

void TimelineModel::setTimeMap(TimeMap *map)
{
    if (m_timeMap == map) return;
    disconnect(m_timeMapConnection);
    m_timeMap = map;
    m_timeMapConnection = connect(timeMap(), &TimeMap::changed, this,
                                  &TimelineModel::resolveMusicalAnchors);
    if (map) {
        connect(map, &QObject::destroyed, this, [this] {
            if (m_timeMap) return; // a later map replaced the destroyed one
            disconnect(m_timeMapConnection);
            m_timeMapConnection = connect(m_defaultTimeMap, &TimeMap::changed, this,
                                          &TimelineModel::resolveMusicalAnchors);
            emit timeMapChanged();
            resolveMusicalAnchors();
        });
    }
    emit timeMapChanged();
    resolveMusicalAnchors();
}

TimelineEditPolicy *TimelineModel::policy() const
{
    return m_policy ? m_policy.data() : m_defaultPolicy;
}

void TimelineModel::setPolicy(TimelineEditPolicy *policy)
{
    if (m_policy == policy) return;
    if (m_policy) disconnect(m_policy, nullptr, this, nullptr);
    m_policy = policy;
    if (policy) {
        connect(policy, &TimelineEditPolicy::changed, this,
                [this] { invalidateSession(tr("The edit policy changed")); });
        connect(policy, &QObject::destroyed, this, [this] {
            if (m_policy) return;
            invalidateSession(tr("The edit policy changed"));
            emit policyChanged();
        });
    }
    invalidateSession(tr("The edit policy changed"));
    emit policyChanged();
}

QVector<QUuid> TimelineModel::visibleTrackOrder() const
{
    QVector<QUuid> result;
    result.reserve(m_trackOrder.size());
    for (const auto &id : m_trackOrder) {
        const auto it = m_tracks.constFind(id);
        if (it != m_tracks.cend() && !it->hidden) result.push_back(id);
    }
    return result;
}

const TimelineTrack *TimelineModel::trackRecord(const QUuid &id) const
{
    const auto it = m_tracks.constFind(id);
    return it == m_tracks.cend() ? nullptr : &it.value();
}

const TimelineClip *TimelineModel::clipRecord(const QUuid &id) const
{
    const auto it = m_clips.constFind(id);
    return it == m_clips.cend() ? nullptr : &it.value();
}

const TimelineMarker *TimelineModel::markerRecord(const QUuid &id) const
{
    const auto it = m_markers.constFind(id);
    return it == m_markers.cend() ? nullptr : &it.value();
}

const std::vector<TimelineModel::IndexEntry> &TimelineModel::trackIndex(const QUuid &trackId) const
{
    const auto it = m_indexes.constFind(trackId);
    return it == m_indexes.cend() ? emptyIndex() : it->entries;
}

qint64 TimelineModel::trackMaxDuration(const QUuid &trackId) const
{
    const auto it = m_indexes.constFind(trackId);
    return it == m_indexes.cend() ? 0 : it->maxDuration;
}

QVector<QUuid> TimelineModel::clipsInRange(const QUuid &trackId, qint64 start, qint64 end,
                                           int limit) const
{
    QVector<QUuid> result;
    const auto it = m_indexes.constFind(trackId);
    if (it == m_indexes.cend() || end <= start || limit <= 0) return result;
    const auto &entries = it->entries;
    // A clip ending after `start` began after start - maxDuration.
    const qint64 lowest = saturatingSubtract(start, it->maxDuration);
    auto cursor = std::upper_bound(entries.cbegin(), entries.cend(), lowest,
                                   [](qint64 value, const IndexEntry &entry) {
                                       return value < entry.start;
                                   });
    for (; cursor != entries.cend() && cursor->start < end; ++cursor) {
        if (cursor->end <= start) continue;
        result.push_back(cursor->id);
        if (result.size() >= limit) break;
    }
    return result;
}

const std::vector<TimelineModel::EdgeEntry> &TimelineModel::clipEdges() const
{
    if (m_clipEdgesDirty) {
        m_clipEdges.clear();
        m_clipEdges.reserve(size_t(m_clips.size()) * 2);
        for (auto it = m_clips.cbegin(); it != m_clips.cend(); ++it) {
            m_clipEdges.push_back({it->start, it->id, false});
            m_clipEdges.push_back({it->end(), it->id, true});
        }
        std::sort(m_clipEdges.begin(), m_clipEdges.end(), [](const auto &a, const auto &b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            if (a.end != b.end) return !a.end;
            return a.id < b.id;
        });
        m_clipEdgesDirty = false;
    }
    return m_clipEdges;
}

const std::vector<TimelineModel::EdgeEntry> &TimelineModel::markerEdges() const
{
    if (m_markerEdgesDirty) {
        m_markerEdges.clear();
        m_markerIndex.clear();
        m_markerMaxDuration = 0;
        for (auto it = m_markers.cbegin(); it != m_markers.cend(); ++it) {
            m_markerEdges.push_back({it->position, it->id, false});
            if (it->duration > 0) m_markerEdges.push_back({it->end(), it->id, true});
            m_markerIndex.push_back({it->position, it->end(), it->id});
            m_markerMaxDuration = std::max(m_markerMaxDuration, it->duration);
        }
        std::sort(m_markerEdges.begin(), m_markerEdges.end(), [](const auto &a, const auto &b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            if (a.end != b.end) return !a.end;
            return a.id < b.id;
        });
        std::sort(m_markerIndex.begin(), m_markerIndex.end(), [](const auto &a, const auto &b) {
            return a.start != b.start ? a.start < b.start : a.id < b.id;
        });
        m_markerEdgesDirty = false;
    }
    return m_markerEdges;
}

QVector<QUuid> TimelineModel::markersInRange(qint64 start, qint64 end, int limit) const
{
    QVector<QUuid> result;
    markerEdges();
    if (end <= start || limit <= 0) return result;
    const qint64 lowest = saturatingSubtract(start, m_markerMaxDuration);
    auto cursor = std::lower_bound(m_markerIndex.cbegin(), m_markerIndex.cend(), lowest,
                                   [](const IndexEntry &entry, qint64 value) {
                                       return entry.start < value;
                                   });
    for (; cursor != m_markerIndex.cend() && cursor->start < end; ++cursor) {
        if (cursor->end < start) continue;
        result.push_back(cursor->id);
        if (result.size() >= limit) break;
    }
    return result;
}

void TimelineModel::rebuildTrackIndex(const QUuid &trackId)
{
    if (!m_tracks.contains(trackId)) {
        m_indexes.remove(trackId);
        return;
    }
    TrackIndexData &index = m_indexes[trackId];
    index.entries.clear();
    index.maxDuration = 0;
    index.maxEnd = std::numeric_limits<qint64>::min();
    for (auto it = m_clips.cbegin(); it != m_clips.cend(); ++it) {
        if (it->trackId != trackId) continue;
        index.entries.push_back({it->start, it->end(), it->id});
    }
    std::sort(index.entries.begin(), index.entries.end(), [](const auto &a, const auto &b) {
        return a.start != b.start ? a.start < b.start : a.id < b.id;
    });
    for (const auto &entry : index.entries) {
        index.maxDuration = std::max(index.maxDuration, entry.end - entry.start);
        index.maxEnd = std::max(index.maxEnd, entry.end);
    }
    m_clipEdgesDirty = true;
}

void TimelineModel::rebuildAllIndexes()
{
    m_indexes.clear();
    for (const auto &id : m_trackOrder) m_indexes.insert(id, {});
    for (auto it = m_clips.cbegin(); it != m_clips.cend(); ++it) {
        auto index = m_indexes.find(it->trackId);
        if (index != m_indexes.end()) index->entries.push_back({it->start, it->end(), it->id});
    }
    for (auto it = m_indexes.begin(); it != m_indexes.end(); ++it) {
        auto &entries = it->entries;
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return a.start != b.start ? a.start < b.start : a.id < b.id;
        });
        for (const auto &entry : entries) {
            it->maxDuration = std::max(it->maxDuration, entry.end - entry.start);
            it->maxEnd = std::max(it->maxEnd, entry.end);
        }
    }
    m_clipEdgesDirty = true;
    rebuildMarkerIndex();
    recomputeContentEnd();
}

void TimelineModel::rebuildMarkerIndex()
{
    m_markerEdgesDirty = true;
}

void TimelineModel::recomputeContentEnd()
{
    qint64 end = 0;
    for (auto it = m_indexes.cbegin(); it != m_indexes.cend(); ++it)
        if (!it->entries.empty()) end = std::max(end, it->maxEnd);
    for (auto it = m_markers.cbegin(); it != m_markers.cend(); ++it)
        end = std::max(end, it->end());
    m_contentEnd = end;
}

// A plan with an error can still carry its attempted placement (a soft
// refusal such as an overlap); it previews as invalid and cannot commit.
bool TimelineModel::hasPreview() const
{
    return m_session && !m_session->stale && m_session->hasPlan
        && !m_session->plan.changes.isEmpty();
}

const TimelinePlan *TimelineModel::previewPlan() const
{
    return hasPreview() ? &m_session->plan : nullptr;
}

const TimelineChange *TimelineModel::previewClipChange(const QUuid &id) const
{
    if (!hasPreview()) return nullptr;
    const auto it = m_session->clipChanges.constFind(id);
    return it == m_session->clipChanges.cend() ? nullptr : &m_session->plan.changes.changes.at(*it);
}

const TimelineChange *TimelineModel::previewMarkerChange(const QUuid &id) const
{
    if (!hasPreview()) return nullptr;
    const auto it = m_session->markerChanges.constFind(id);
    return it == m_session->markerChanges.cend() ? nullptr : &m_session->plan.changes.changes.at(*it);
}

std::optional<TimelineClip> TimelineModel::effectiveClip(const QUuid &id) const
{
    if (const auto *change = previewClipChange(id)) return change->clipAfter;
    if (const auto *record = clipRecord(id)) return *record;
    return std::nullopt;
}

std::optional<TimelineTrack> TimelineModel::effectiveTrack(const QUuid &id) const
{
    if (hasPreview()) {
        const auto it = m_session->trackChanges.constFind(id);
        if (it != m_session->trackChanges.cend())
            return m_session->plan.changes.changes.at(*it).trackAfter;
    }
    if (const auto *record = trackRecord(id)) return *record;
    return std::nullopt;
}

std::optional<TimelineMarker> TimelineModel::effectiveMarker(const QUuid &id) const
{
    if (const auto *change = previewMarkerChange(id)) return change->markerAfter;
    if (const auto *record = markerRecord(id)) return *record;
    return std::nullopt;
}

QVector<QUuid> TimelineModel::effectiveVisibleTrackOrder() const
{
    QVector<QUuid> order = m_trackOrder;
    if (hasPreview()) {
        if (m_session->orderChange >= 0)
            order = m_session->plan.changes.changes.at(m_session->orderChange).orderAfter;
    }
    QVector<QUuid> result;
    result.reserve(order.size());
    for (const auto &id : order) {
        const auto track = effectiveTrack(id);
        if (track && !track->hidden) result.push_back(id);
    }
    return result;
}

std::optional<qint64> TimelineModel::ticksToSamples(qint64 ticks, TimeMap::Rounding rounding) const
{
    return TimelineTime::rescale(ticks, m_timebase, {timeMap()->sampleRate(), 1}, rounding);
}

std::optional<qint64> TimelineModel::samplesToTicks(qint64 samples, TimeMap::Rounding rounding) const
{
    return TimelineTime::rescale(samples, {timeMap()->sampleRate(), 1}, m_timebase, rounding);
}

std::optional<qint64> TimelineModel::ticksToFrames(qint64 ticks, TimeMap::Rounding rounding) const
{
    const auto originSample = TimelineTime::parseDecimal(timeMap()->originSample());
    const auto origin = originSample ? samplesToTicks(*originSample, TimeMap::NearestAway)
                                     : std::nullopt;
    const auto relative = origin ? TimelineTime::subtract(ticks, *origin) : std::nullopt;
    if (!relative) return std::nullopt;
    const TimelineRate rate{timeMap()->frameRateNumerator(), timeMap()->frameRateDenominator()};
    return TimelineTime::rescale(*relative, m_timebase, rate, rounding);
}

std::optional<qint64> TimelineModel::frameStartTicks(qint64 frame) const
{
    const auto originSample = TimelineTime::parseDecimal(timeMap()->originSample());
    const auto origin = originSample ? samplesToTicks(*originSample, TimeMap::NearestAway)
                                     : std::nullopt;
    const TimelineRate rate{timeMap()->frameRateNumerator(), timeMap()->frameRateDenominator()};
    const auto offset = TimelineTime::rescale(frame, rate, m_timebase, TimeMap::Ceil);
    return origin && offset ? TimelineTime::add(*origin, *offset) : std::nullopt;
}

std::optional<qint64> TimelineModel::musicalTickFor(qint64 ticks) const
{
    const auto samples = ticksToSamples(ticks, TimeMap::NearestAway);
    if (!samples) return std::nullopt;
    const auto tick = timeMap()->toTick(SamplePosition{*samples}, TimeMap::NearestAway);
    return tick ? std::optional<qint64>(tick->value) : std::nullopt;
}

std::optional<qint64> TimelineModel::ticksForMusicalTick(qint64 musicalTick) const
{
    const auto sample = timeMap()->fromTick(MusicalTick{musicalTick}, TimeMap::NearestAway);
    return sample ? samplesToTicks(sample->value, TimeMap::NearestAway) : std::nullopt;
}

bool TimelineModel::gridStep(TimelineSnapGrid grid, quint64 &numerator, quint64 &denominator,
                             qint64 &origin) const
{
    TimelineRate unit;
    origin = 0;
    switch (grid) {
    case TimelineSnapGrid::None:
        return false;
    case TimelineSnapGrid::Seconds:
        unit = {1, 1};
        break;
    case TimelineSnapGrid::Samples:
        unit = {timeMap()->sampleRate(), 1};
        break;
    case TimelineSnapGrid::Frames: {
        unit = {timeMap()->frameRateNumerator(), timeMap()->frameRateDenominator()};
        const auto start = frameStartTicks(0);
        if (!start) return false;
        origin = *start;
        break;
    }
    }
    if (!unit.isValid()) return false;
    // ticks per unit = (tb.n / tb.d) / (u.n / u.d) = tb.n * u.d / (tb.d * u.n)
    numerator = quint64(m_timebase.numerator) * quint64(unit.denominator);
    denominator = quint64(m_timebase.denominator) * quint64(unit.numerator);
    const quint64 common = std::gcd(numerator, denominator);
    numerator /= common;
    denominator /= common;
    return true;
}

// AGENT-GUARD: a derived position saturates so position + duration stays
// representable (TimelineMarker::end() must never overflow). A negative
// duration is not clamped here; checkMarker refuses it before storage.
bool TimelineModel::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    emit rejected(message);
    return false;
}

OperationError TimelineModel::failWith(OperationError error)
{
    fail(error.message);
    return error;
}

} // namespace QindaTK
