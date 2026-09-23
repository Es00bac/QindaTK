// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_planner.h"

#include "timeline_model.h"
#include "timeline_snap.h"

#include <QCoreApplication>
#include <algorithm>

namespace QindaTK {
namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("TimelineModel", text);
}

struct RipplePoint {
    qint64 at = 0;
    qint64 shift = 0;
};
using RipplePoints = QHash<QUuid, QVector<RipplePoint>>;

void setError(TimelinePlanner::Context &context, OperationError error)
{
    context.plan.error = std::move(error);
    context.plan.changes.changes.clear();
}

std::optional<TimelineClip> editableClip(TimelinePlanner::Context &context, const QUuid &id)
{
    const auto *clip = context.model.clipRecord(id);
    if (!clip) {
        setError(context, TimelinePlanner::error(ErrorCode::StaleRevision,
                                                 tr("A clip in this edit no longer exists")));
        return std::nullopt;
    }
    const auto *track = context.model.trackRecord(clip->trackId);
    if (clip->locked) {
        setError(context, TimelinePlanner::error(ErrorCode::Validation,
            tr("Clip “%1” is locked").arg(TimelinePlanner::clipName(*clip))));
        return std::nullopt;
    }
    if (track && track->locked) {
        setError(context, TimelinePlanner::error(ErrorCode::Validation,
            tr("Track “%1” is locked").arg(track->name)));
        return std::nullopt;
    }
    return *clip;
}

bool collectTargets(TimelinePlanner::Context &context, QVector<TimelineClip> &targets)
{
    QSet<QUuid> seen;
    for (const auto &id : context.request.clipIds) {
        if (seen.contains(id)) continue;
        seen.insert(id);
        const auto clip = editableClip(context, id);
        if (!clip) return false;
        targets.push_back(*clip);
    }
    if (targets.isEmpty()) {
        setError(context, TimelinePlanner::error(ErrorCode::InvalidArgument,
                                                 tr("No clips were selected for this edit")));
        return false;
    }
    return true;
}

const TimelineClip &anchorOf(const TimelinePlanner::Context &context,
                             const QVector<TimelineClip> &targets)
{
    for (const auto &clip : targets)
        if (clip.id == context.request.anchorClipId) return clip;
    return targets.first();
}

QVector<QUuid> rippleTracks(const TimelineModel &model, const QSet<QUuid> &sources, bool all)
{
    QVector<QUuid> result;
    for (const auto &id : model.trackOrder()) {
        if (!all && !sources.contains(id)) continue;
        const auto *track = model.trackRecord(id);
        // AGENT-NOTE: like an NLE with sync lock off, a locked track is never
        // rippled; its clips keep their positions.
        if (track && !track->locked) result.push_back(id);
    }
    return result;
}

// Every clip on a rippled track whose committed start is at or after an edit
// point moves by that point's shift. `after` already holds the targets' edits.
bool applyRipple(TimelinePlanner::Context &context, const QVector<QUuid> &tracks,
                 RipplePoints points, QHash<QUuid, TimelineClip> &after, const QSet<QUuid> &skip)
{
    for (const auto &trackId : tracks) {
        auto list = points.value(trackId);
        if (list.isEmpty()) continue;
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.at < b.at; });
        const auto &entries = context.model.trackIndex(trackId);
        auto it = std::lower_bound(entries.cbegin(), entries.cend(), list.first().at,
                                   [](const TimelineModel::IndexEntry &entry, qint64 value) {
                                       return entry.start < value;
                                   });
        for (; it != entries.cend(); ++it) {
            if (skip.contains(it->id)) continue;
            qint64 shift = 0;
            for (const auto &point : list) {
                if (point.at > it->start) break;
                const auto sum = TimelineTime::add(shift, point.shift);
                if (!sum) {
                    setError(context, TimelinePlanner::error(ErrorCode::InvalidArgument,
                                                             tr("Ripple shift overflows")));
                    return false;
                }
                shift = *sum;
            }
            if (shift == 0) continue;
            TimelineClip clip = after.contains(it->id) ? after.value(it->id)
                                                       : *context.model.clipRecord(it->id);
            if (clip.locked) {
                setError(context, TimelinePlanner::error(ErrorCode::Validation,
                    tr("Ripple would move locked clip “%1”").arg(TimelinePlanner::clipName(clip))));
                return false;
            }
            const auto start = TimelineTime::add(clip.start, shift);
            if (!start || !TimelineTime::add(*start, clip.duration)) {
                setError(context, TimelinePlanner::error(ErrorCode::InvalidArgument,
                                                         tr("Ripple moves a clip out of range")));
                return false;
            }
            clip.start = *start;
            after.insert(clip.id, clip);
        }
    }
    return true;
}

RipplePoints pointsFor(const TimelinePlanner::Context &context, const QVector<QUuid> &tracks,
                       const QVector<QPair<QUuid, RipplePoint>> &raw)
{
    RipplePoints result;
    const bool all = context.policy.rippleScope() == TimelineEditPolicy::RippleAllTracks;
    for (const auto &entry : raw) {
        if (all) {
            for (const auto &track : tracks) result[track].push_back(entry.second);
        } else {
            result[entry.first].push_back(entry.second);
        }
    }
    return result;
}

void emitClipChanges(TimelinePlanner::Context &context, const QHash<QUuid, TimelineClip> &after)
{
    QList<QUuid> ids = after.keys();
    std::sort(ids.begin(), ids.end());
    for (const auto &id : ids) {
        const auto *before = context.model.clipRecord(id);
        const TimelineClip &next = after.value(id);
        if (before && *before == next) continue;
        TimelineChange change;
        change.kind = TimelineChange::Kind::Clip;
        change.id = id;
        if (before) change.clipBefore = *before;
        change.clipAfter = next;
        context.plan.changes.changes.push_back(change);
    }
}

std::optional<qint64> gestureDelta(TimelinePlanner::Context &context)
{
    const auto delta = TimelineTime::subtract(context.input.pointerTicks, context.request.grabTicks);
    if (!delta) setError(context, TimelinePlanner::error(ErrorCode::InvalidArgument,
                                                         tr("The edit distance is out of range")));
    return delta;
}

void snapDelta(TimelinePlanner::Context &context, const QVector<qint64> &edges,
               const QSet<QUuid> &exclude, qint64 &delta)
{
    if (!context.input.snap || context.input.snapToleranceTicks <= 0) return;
    QVector<qint64> candidates;
    for (qint64 edge : edges) {
        const auto moved = TimelineTime::add(edge, delta);
        if (moved) candidates.push_back(*moved);
    }
    TimelineSnapContext snap;
    snap.tolerance = context.input.snapToleranceTicks;
    snap.excludedClips = &exclude;
    snap.playhead = context.input.playheadTicks;
    snap.grid = context.input.grid;
    const auto result = TimelineSnapper::snap(context.model, candidates, snap);
    if (!result.snapped) return;
    if (const auto snapped = TimelineTime::add(delta, result.adjustment)) {
        delta = *snapped;
        context.plan.snap = result;
    }
}
} // namespace

void TimelinePlanner::planMove(Context &context)
{
    const auto &policy = context.policy;
    const bool ripple = context.request.ripple;
    QVector<TimelineClip> targets;
    if (!collectTargets(context, targets)) return;
    auto delta = gestureDelta(context);
    if (!delta) return;
    int trackDelta = context.input.trackDelta;
    if (ripple || !policy.isAvailable(QStringLiteral("moveAcrossTracks"))) trackDelta = 0;

    const QVector<QUuid> order = context.model.visibleTrackOrder();
    QHash<QUuid, int> visibleIndex;
    for (int i = 0; i < order.size(); ++i) visibleIndex.insert(order.at(i), i);
    int lowest = int(order.size()), highest = -1;
    for (const auto &clip : targets) {
        const int index = visibleIndex.value(clip.trackId, -1);
        if (index < 0) trackDelta = 0; // hidden tracks keep their clips
        lowest = std::min(lowest, index);
        highest = std::max(highest, index);
    }
    if (trackDelta != 0) trackDelta = std::clamp(trackDelta, -lowest, int(order.size()) - 1 - highest);
    QHash<QUuid, QUuid> destination;
    QSet<QUuid> moving, sourceTracks;
    for (const auto &clip : targets) {
        moving.insert(clip.id);
        sourceTracks.insert(clip.trackId);
        const QUuid target = trackDelta == 0 ? clip.trackId
                                             : order.at(visibleIndex.value(clip.trackId) + trackDelta);
        destination.insert(clip.id, target);
        if (trackDelta == 0) continue;
        const auto *track = context.model.trackRecord(target);
        if (track->locked)
            return setError(context, error(ErrorCode::Validation,
                                           tr("Track “%1” is locked").arg(track->name)));
        if (policy.requireMatchingTrackKind() && !clip.kind.isEmpty() && !track->kind.isEmpty()
            && clip.kind != track->kind) {
            return setError(context, error(ErrorCode::Validation,
                tr("A %1 clip cannot be placed on the %2 track “%3”")
                    .arg(clip.kind, track->kind, track->name)));
        }
    }

    QVector<QUuid> tracks;
    QVector<QPair<QUuid, RipplePoint>> raw;
    if (ripple) {
        tracks = rippleTracks(context.model, sourceTracks,
                              policy.rippleScope() == TimelineEditPolicy::RippleAllTracks);
        QHash<QUuid, qint64> pivots;
        for (const auto &clip : targets) {
            const qint64 current = pivots.value(clip.trackId, clip.start);
            pivots.insert(clip.trackId, std::min(current, clip.start));
        }
        qint64 globalPivot = std::numeric_limits<qint64>::max();
        for (auto it = pivots.cbegin(); it != pivots.cend(); ++it) globalPivot = std::min(globalPivot, *it);
        for (const auto &trackId : tracks) {
            const bool all = policy.rippleScope() == TimelineEditPolicy::RippleAllTracks;
            if (!all && !pivots.contains(trackId)) continue;
            const qint64 pivot = all ? globalPivot : pivots.value(trackId);
            for (const auto &entry : context.model.trackIndex(trackId))
                if (entry.start >= pivot) moving.insert(entry.id);
        }
    }
    const TimelineClip &anchor = anchorOf(context, targets);
    snapDelta(context, {anchor.start, anchor.end()}, moving, *delta);
    if (!policy.allowNegativeTime()) {
        qint64 minimum = std::numeric_limits<qint64>::max();
        for (const auto &id : moving) minimum = std::min(minimum, context.model.clipRecord(id)->start);
        if (minimum != std::numeric_limits<qint64>::max() && minimum + *delta < 0) {
            *delta = -minimum;
            context.plan.snap = {};
        }
    }
    QHash<QUuid, TimelineClip> after;
    for (const auto &id : moving) {
        TimelineClip clip = *context.model.clipRecord(id);
        if (clip.locked)
            return setError(context, error(ErrorCode::Validation,
                tr("Ripple would move locked clip “%1”").arg(clipName(clip))));
        const auto start = TimelineTime::add(clip.start, *delta);
        if (!start || !TimelineTime::add(*start, clip.duration))
            return setError(context, error(ErrorCode::InvalidArgument,
                                           tr("The clip would move out of range")));
        clip.start = *start;
        clip.trackId = destination.value(id, clip.trackId);
        after.insert(id, clip);
    }
    context.plan.appliedDelta = *delta;
    context.plan.appliedTrackDelta = trackDelta;
    emitClipChanges(context, after);
}

void TimelinePlanner::planTrim(Context &context)
{
    const auto &policy = context.policy;
    const bool head = context.request.kind == TimelineEditKind::TrimStart;
    const bool ripple = context.request.ripple;
    QVector<TimelineClip> targets;
    if (!collectTargets(context, targets)) return;
    auto delta = gestureDelta(context);
    if (!delta) return;
    QSet<QUuid> exclude, sourceTracks;
    qint64 firstEnd = std::numeric_limits<qint64>::max();
    for (const auto &clip : targets) {
        exclude.insert(clip.id);
        sourceTracks.insert(clip.trackId);
        firstEnd = std::min(firstEnd, clip.end());
    }
    const bool allTracks = policy.rippleScope() == TimelineEditPolicy::RippleAllTracks;
    const QVector<QUuid> tracks = ripple ? rippleTracks(context.model, sourceTracks, allTracks)
                                         : QVector<QUuid>{};
    for (const auto &trackId : tracks)
        for (const auto &entry : context.model.trackIndex(trackId))
            if (entry.start >= firstEnd) exclude.insert(entry.id);
    const TimelineClip &anchor = anchorOf(context, targets);
    snapDelta(context, {head ? anchor.start : anchor.end()}, exclude, *delta);

    const qint64 minimum = policy.minimumClipTicks();
    qint64 low = std::numeric_limits<qint64>::min(), high = std::numeric_limits<qint64>::max();
    for (const auto &clip : targets) {
        if (head) {
            high = std::min(high, clip.duration - minimum);
            if (!policy.allowBeyondSource()) low = std::max(low, -clip.sourceOffset);
            if (!ripple && !policy.allowNegativeTime()) low = std::max(low, -clip.start);
        } else {
            low = std::max(low, minimum - clip.duration);
            const auto remaining = TimelineTime::subtract(clip.sourceDuration, clip.sourceOffset);
            const auto room = remaining ? TimelineTime::subtract(*remaining, clip.duration)
                                        : std::nullopt;
            if (!policy.allowBeyondSource() && clip.sourceDuration >= 0 && room)
                high = std::min(high, *room);
        }
    }
    if (low > high)
        return setError(context, error(ErrorCode::Validation,
                                       tr("The clip cannot be trimmed within its source and minimum length")));
    const qint64 clamped = std::clamp(*delta, low, high);
    if (clamped != *delta) context.plan.snap = {};
    *delta = clamped;
    const auto negated = TimelineTime::subtract(0, *delta);
    if (!negated)
        return setError(context, error(ErrorCode::InvalidArgument, tr("The trim is out of range")));

    QHash<QUuid, TimelineClip> after;
    QVector<QPair<QUuid, RipplePoint>> raw;
    for (TimelineClip clip : targets) {
        const qint64 originalEnd = clip.end();
        std::optional<qint64> start = clip.start, duration, offset = clip.sourceOffset;
        if (head) {
            if (!ripple) start = TimelineTime::add(clip.start, *delta);
            duration = TimelineTime::subtract(clip.duration, *delta);
            offset = TimelineTime::add(clip.sourceOffset, *delta);
        } else {
            duration = TimelineTime::add(clip.duration, *delta);
        }
        if (!start || !duration || !offset || !TimelineTime::add(*start, *duration))
            return setError(context, error(ErrorCode::InvalidArgument,
                                           tr("The trimmed clip is out of range")));
        clip.start = *start;
        clip.duration = *duration;
        clip.sourceOffset = *offset;
        if (ripple) raw.push_back({clip.trackId, {originalEnd, head ? *negated : *delta}});
        after.insert(clip.id, clip);
    }
    if (ripple && *delta != 0
        && !applyRipple(context, tracks, pointsFor(context, tracks, raw), after, {}))
        return;
    context.plan.appliedDelta = *delta;
    emitClipChanges(context, after);
}

void TimelinePlanner::planSplit(Context &context)
{
    QVector<TimelineClip> targets;
    if (!collectTargets(context, targets)) return;
    qint64 at = context.input.pointerTicks;
    QSet<QUuid> exclude;
    for (const auto &clip : targets) exclude.insert(clip.id);
    qint64 offset = 0;
    snapDelta(context, {at}, exclude, offset);
    const auto snappedAt = TimelineTime::add(at, offset);
    if (!snappedAt)
        return setError(context, error(ErrorCode::InvalidArgument, tr("The split point is out of range")));
    at = *snappedAt;
    const qint64 minimum = context.policy.minimumClipTicks();
    const QUuid ns(context.operationId);
    QHash<QUuid, TimelineClip> after;
    for (const auto &clip : targets) {
        if (at - clip.start < minimum || clip.end() - at < minimum) continue;
        TimelineClip left = clip;
        left.duration = at - clip.start;
        TimelineClip right = clip;
        right.id = QUuid::createUuidV5(ns, clip.id.toRfc4122());
        right.start = at;
        right.duration = clip.end() - at;
        const auto offsetTicks = TimelineTime::add(clip.sourceOffset, at - clip.start);
        if (!offsetTicks)
            return setError(context, error(ErrorCode::InvalidArgument,
                                           tr("The split source offset is out of range")));
        right.sourceOffset = *offsetTicks;
        after.insert(left.id, left);
        after.insert(right.id, right);
    }
    if (after.isEmpty())
        return setError(context, error(ErrorCode::Validation,
                                       tr("The split point is not inside a selected clip")));
    context.plan.appliedDelta = at;
    emitClipChanges(context, after);
}

void TimelinePlanner::planDelete(Context &context)
{
    QVector<TimelineClip> targets;
    if (!collectTargets(context, targets)) return;
    QSet<QUuid> removed, sourceTracks;
    for (const auto &clip : targets) {
        removed.insert(clip.id);
        sourceTracks.insert(clip.trackId);
        TimelineChange change;
        change.kind = TimelineChange::Kind::Clip;
        change.id = clip.id;
        change.clipBefore = clip;
        context.plan.changes.changes.push_back(change);
    }
    if (!context.request.ripple) return;
    const bool all = context.policy.rippleScope() == TimelineEditPolicy::RippleAllTracks;
    const QVector<QUuid> tracks = rippleTracks(context.model, sourceTracks, all);
    // Ripple closes the union of removed intervals, never their summed lengths.
    QHash<QUuid, QVector<QPair<qint64, qint64>>> intervals;
    for (const auto &clip : targets) intervals[all ? QUuid() : clip.trackId].push_back({clip.start, clip.end()});
    QVector<QPair<QUuid, RipplePoint>> raw;
    for (auto it = intervals.begin(); it != intervals.end(); ++it) {
        auto list = it.value();
        std::sort(list.begin(), list.end());
        qint64 start = list.first().first, end = list.first().second;
        const auto flush = [&] { raw.push_back({it.key(), {end, start - end}}); };
        for (int i = 1; i < list.size(); ++i) {
            if (list.at(i).first <= end) {
                end = std::max(end, list.at(i).second);
            } else {
                flush();
                start = list.at(i).first;
                end = list.at(i).second;
            }
        }
        flush();
    }
    QHash<QUuid, TimelineClip> after;
    if (!applyRipple(context, tracks, pointsFor(context, tracks, raw), after, removed)) return;
    emitClipChanges(context, after);
}

void TimelinePlanner::planClipTiming(Context &context)
{
    QVector<TimelineClip> targets;
    if (!collectTargets(context, targets)) return;
    const auto &policy = context.policy;
    const TimelineClip before = targets.first();
    TimelineClip clip = before;
    if (context.request.start) clip.start = *context.request.start;
    if (context.request.duration) clip.duration = *context.request.duration;
    if (context.request.sourceOffset) clip.sourceOffset = *context.request.sourceOffset;
    if (clip.start != before.start) {
        if (auto failure = policy.requireOperation(QStringLiteral("move"))) return setError(context, failure);
    }
    if (clip.duration != before.duration || clip.sourceOffset != before.sourceOffset) {
        if (auto failure = policy.requireOperation(QStringLiteral("trim"))) return setError(context, failure);
    }
    if (clip.duration < policy.minimumClipTicks())
        return setError(context, error(ErrorCode::Validation,
                                       tr("The clip is shorter than the minimum length"),
                                       QStringLiteral("duration")));
    if (!TimelineTime::add(clip.start, clip.duration))
        return setError(context, error(ErrorCode::InvalidArgument, tr("The clip end is out of range")));
    if (!policy.allowNegativeTime() && clip.start < 0)
        return setError(context, error(ErrorCode::Validation, tr("The clip would start before zero"),
                                       QStringLiteral("start")));
    const auto sourceEnd = TimelineTime::add(clip.sourceOffset, clip.duration);
    if (!policy.allowBeyondSource()
        && (clip.sourceOffset < 0 || !sourceEnd
            || (clip.sourceDuration >= 0 && *sourceEnd > clip.sourceDuration)))
        return setError(context, error(ErrorCode::Validation,
                                       tr("The clip would extend beyond its source media"),
                                       QStringLiteral("sourceOffset")));
    QHash<QUuid, TimelineClip> after{{clip.id, clip}};
    if (context.request.ripple && clip.end() != before.end()) {
        if (auto failure = policy.requireOperation(QStringLiteral("ripple"))) return setError(context, failure);
        const bool all = policy.rippleScope() == TimelineEditPolicy::RippleAllTracks;
        const QVector<QUuid> tracks = rippleTracks(context.model, {clip.trackId}, all);
        const QVector<QPair<QUuid, RipplePoint>> raw{{clip.trackId, {before.end(), clip.end() - before.end()}}};
        if (!applyRipple(context, tracks, pointsFor(context, tracks, raw), after, {clip.id})) return;
    }
    emitClipChanges(context, after);
}

} // namespace QindaTK
