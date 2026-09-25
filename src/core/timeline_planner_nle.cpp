// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_planner.h"

#include "timeline_model.h"

#include <QCoreApplication>
#include <algorithm>
#include <limits>

// Sequence edits (WP-071, docs/sequence-edits.md): roll, slip and slide
// (transitions: timeline_planner_transitions.cpp). AGENT-NOTE (D-118): every bound comes from the clip records
// (sourceOffset, sourceDuration, neighbours), never from decoded media, so an
// offline clip edits exactly like an online one and keeps its source offsets.
namespace QindaTK {
namespace {
constexpr qint64 kUnbounded = std::numeric_limits<qint64>::max();

QString tr(const char *text)
{
    return QCoreApplication::translate("TimelineModel", text);
}

void setError(TimelinePlanner::Context &context, OperationError error)
{
    context.plan.error = std::move(error);
    context.plan.changes.changes.clear();
}

// Bounds on a shared delta, tightened by each constraint.
struct Range {
    qint64 low = std::numeric_limits<qint64>::min();
    qint64 high = kUnbounded;
    void atLeast(qint64 value) { low = std::max(low, value); }
    void atMost(qint64 value) { high = std::min(high, value); }
};

// Negation that saturates instead of overflowing (for bounds only).
qint64 negated(qint64 value)
{
    return value == std::numeric_limits<qint64>::min() ? kUnbounded : -value;
}

std::optional<qint64> requestDelta(TimelinePlanner::Context &context)
{
    const auto delta = TimelineTime::subtract(context.input.pointerTicks, context.request.grabTicks);
    if (!delta) setError(context, TimelinePlanner::error(ErrorCode::InvalidArgument,
                                                         tr("The edit distance is out of range")));
    return delta;
}

// Collects the selection (plus link partners) as editable records.
bool collect(TimelinePlanner::Context &context, QVector<TimelineClip> &targets)
{
    const QVector<QUuid> ids = context.request.linked
        ? TimelinePlanner::withLinked(context.model, context.request.clipIds) : context.request.clipIds;
    for (const QUuid &id : ids) {
        const TimelineClip *clip = context.model.clipRecord(id);
        if (!clip) {
            setError(context, TimelinePlanner::error(ErrorCode::StaleRevision,
                                                     tr("A clip in this edit no longer exists")));
            return false;
        }
        if (!TimelinePlanner::editableClip(context, *clip)) return false;
        targets.push_back(*clip);
    }
    return !targets.isEmpty();
}

bool shift(TimelineClip &clip, qint64 startBy, qint64 durationBy, qint64 offsetBy)
{
    const auto start = TimelineTime::add(clip.start, startBy);
    const auto duration = TimelineTime::add(clip.duration, durationBy);
    const auto offset = TimelineTime::add(clip.sourceOffset, offsetBy);
    if (!start || !duration || !offset || *duration < 1 || !TimelineTime::add(*start, *duration)) return false;
    clip.start = *start;
    clip.duration = *duration;
    clip.sourceOffset = *offset;
    return true;
}

} // namespace

bool TimelinePlanner::editableClip(Context &context, const TimelineClip &clip)
{
    const TimelineTrack *track = context.model.trackRecord(clip.trackId);
    if (!clip.locked && !(track && track->locked)) return true;
    setError(context, error(ErrorCode::Validation, tr("Clip “%1” is locked").arg(clipName(clip))));
    return false;
}

const TimelineClip *TimelinePlanner::endingAt(const TimelineModel &model, const QUuid &trackId, qint64 tick,
                                              const QSet<QUuid> &exclude)
{
    if (tick == std::numeric_limits<qint64>::min()) return nullptr;
    for (const QUuid &id : model.clipsInRange(trackId, tick - 1, tick)) {
        const TimelineClip *clip = model.clipRecord(id);
        if (clip && clip->end() == tick && !exclude.contains(id)) return clip;
    }
    return nullptr;
}

const TimelineClip *TimelinePlanner::startingAt(const TimelineModel &model, const QUuid &trackId, qint64 tick,
                                                const QSet<QUuid> &exclude)
{
    if (tick == std::numeric_limits<qint64>::max()) return nullptr;
    for (const QUuid &id : model.clipsInRange(trackId, tick, tick + 1)) {
        const TimelineClip *clip = model.clipRecord(id);
        if (clip && clip->start == tick && !exclude.contains(id)) return clip;
    }
    return nullptr;
}

qint64 TimelinePlanner::headRoom(const Context &context, const TimelineClip &clip)
{
    return context.policy.allowBeyondSource() ? kUnbounded : clip.sourceOffset;
}

std::optional<qint64> TimelinePlanner::framePoint(const TimelineModel &model, qint64 ticks)
{
    const auto frame = model.ticksToFrames(ticks, TimeMap::NearestAway);
    return frame ? model.frameStartTicks(*frame) : std::nullopt;
}

std::optional<qint64> TimelinePlanner::legalDelta(const Context &context, qint64 point, qint64 delta,
                                                  qint64 low, qint64 high)
{
    if (low > high) return std::nullopt;
    const qint64 clamped = std::clamp(delta, low, high);
    if (!context.request.frameExact) return clamped;
    const auto target = TimelineTime::add(point, clamped);
    const auto frame = target ? context.model.ticksToFrames(*target, TimeMap::NearestAway) : std::nullopt;
    std::optional<qint64> best;
    if (frame) {
        for (const qint64 candidate : {*frame, *frame - 1, *frame + 1}) {
            const auto start = context.model.frameStartTicks(candidate);
            const auto moved = start ? TimelineTime::subtract(*start, point) : std::nullopt;
            if (!moved || *moved < low || *moved > high) continue;
            const auto distance = [clamped](qint64 value) {
                return value > clamped ? quint64(value) - quint64(clamped) : quint64(clamped) - quint64(value);
            };
            if (!best || distance(*moved) < distance(*best)) best = moved;
        }
    }
    if (best) return best;
    if (low <= 0 && high >= 0) return 0; // no frame start is reachable; the edit point stays
    return std::nullopt;
}

qint64 TimelinePlanner::tailRoom(const Context &context, const TimelineClip &clip)
{
    if (context.policy.allowBeyondSource() || clip.sourceDuration < 0) return kUnbounded;
    const auto used = TimelineTime::add(clip.sourceOffset, clip.duration);
    const auto room = used ? TimelineTime::subtract(clip.sourceDuration, *used) : std::nullopt;
    return room.value_or(std::numeric_limits<qint64>::min());
}

// Roll: the cut between clipIds[0] (outgoing) and clipIds[1] (incoming)
// moves by the delta; the sequence length and everything else stay put.
void TimelinePlanner::planRoll(Context &context)
{
    const auto &request = context.request;
    if (request.clipIds.size() != 2)
        return setError(context, error(ErrorCode::InvalidArgument, tr("A roll edits the cut between two clips")));
    const TimelineClip *outgoing = context.model.clipRecord(request.clipIds.at(0));
    const TimelineClip *incoming = context.model.clipRecord(request.clipIds.at(1));
    if (!outgoing || !incoming || outgoing->trackId != incoming->trackId || outgoing->end() != incoming->start)
        return setError(context, error(ErrorCode::Validation,
                                       tr("A roll needs two adjacent clips on one track")));
    auto delta = requestDelta(context);
    if (!delta) return;
    // Link partners of the outgoing clip move their ends, partners of the
    // incoming clip their starts.
    QVector<TimelineClip> left, right;
    QSet<QUuid> seen;
    for (const auto &[group, anchor] : {std::pair{&left, outgoing}, std::pair{&right, incoming}}) {
        const QVector<QUuid> ids = request.linked ? withLinked(context.model, {anchor->id}) : QVector<QUuid>{anchor->id};
        for (const QUuid &id : ids) {
            if (seen.contains(id)) continue;
            seen.insert(id);
            const TimelineClip *clip = context.model.clipRecord(id);
            if (!clip)
                return setError(context, error(ErrorCode::StaleRevision, tr("A clip in this edit no longer exists")));
            if (!editableClip(context, *clip)) return;
            group->push_back(*clip);
        }
    }
    const qint64 minimum = context.policy.minimumClipTicks();
    Range range;
    for (const TimelineClip &clip : left) {
        range.atLeast(minimum - clip.duration);
        range.atMost(tailRoom(context, clip));
    }
    for (const TimelineClip &clip : right) {
        range.atMost(clip.duration - minimum);
        range.atLeast(negated(headRoom(context, clip)));
        if (!context.policy.allowNegativeTime()) range.atLeast(negated(clip.start));
    }
    const auto legal = legalDelta(context, outgoing->end(), *delta, range.low, range.high);
    if (!legal)
        return setError(context, error(ErrorCode::Validation,
                                       tr("The cut cannot move within the clips' source handles")));
    QHash<QUuid, TimelineClip> after;
    for (TimelineClip clip : left) {
        if (!shift(clip, 0, *legal, 0))
            return setError(context, error(ErrorCode::InvalidArgument, tr("The roll is out of range")));
        after.insert(clip.id, clip);
    }
    const qint64 back = negated(*legal);
    for (TimelineClip clip : right) {
        if (!shift(clip, *legal, back, *legal))
            return setError(context, error(ErrorCode::InvalidArgument, tr("The roll is out of range")));
        after.insert(clip.id, clip);
    }
    context.plan.appliedDelta = *legal;
    emitClips(context, after);
}

// Slip: each clip shows a different part of its source in the same place.
void TimelinePlanner::planSlip(Context &context)
{
    QVector<TimelineClip> targets;
    if (!collect(context, targets)) return;
    auto delta = requestDelta(context);
    if (!delta) return;
    Range range;
    for (const TimelineClip &clip : targets) {
        range.atLeast(negated(headRoom(context, clip)));
        range.atMost(tailRoom(context, clip));
    }
    const auto legal = legalDelta(context, targets.first().start, *delta, range.low, range.high);
    if (!legal)
        return setError(context, error(ErrorCode::Validation, tr("The clip has no source handles to slip into")));
    QHash<QUuid, TimelineClip> after;
    for (TimelineClip clip : targets) {
        if (!shift(clip, 0, 0, *legal))
            return setError(context, error(ErrorCode::InvalidArgument, tr("The slip is out of range")));
        after.insert(clip.id, clip);
    }
    context.plan.appliedDelta = *legal;
    emitClips(context, after);
}

// Slide: each clip moves; the clip ending at its start takes up the change
// at its end and the clip starting at its end at its start (a roll on both
// sides), so the clip's own source range and the sequence length stay.
void TimelinePlanner::planSlide(Context &context)
{
    QVector<TimelineClip> targets;
    if (!collect(context, targets)) return;
    auto delta = requestDelta(context);
    if (!delta) return;
    QSet<QUuid> moving;
    for (const TimelineClip &clip : targets) moving.insert(clip.id);
    const qint64 minimum = context.policy.minimumClipTicks();
    Range range;
    QHash<QUuid, TimelineClip> before, after; // neighbours: shortened/lengthened
    QVector<QUuid> lefts, rights;
    for (const TimelineClip &clip : targets) {
        if (!context.policy.allowNegativeTime()) range.atLeast(negated(clip.start));
        if (const TimelineClip *left = endingAt(context.model, clip.trackId, clip.start, moving)) {
            if (!editableClip(context, *left)) return;
            range.atLeast(minimum - left->duration);
            range.atMost(tailRoom(context, *left));
            before.insert(left->id, *left);
            lefts.push_back(left->id);
        } else {
            // Without a neighbour the clip may slide back only into the gap.
            qint64 previousEnd = std::numeric_limits<qint64>::min();
            for (const auto &entry : context.model.trackIndex(clip.trackId)) {
                if (entry.start >= clip.start) break;
                if (!moving.contains(entry.id)) previousEnd = std::max(previousEnd, entry.end);
            }
            if (previousEnd != std::numeric_limits<qint64>::min()) range.atLeast(previousEnd - clip.start);
        }
        if (const TimelineClip *right = startingAt(context.model, clip.trackId, clip.end(), moving)) {
            if (!editableClip(context, *right)) return;
            range.atMost(right->duration - minimum);
            range.atLeast(negated(headRoom(context, *right)));
            before.insert(right->id, *right);
            rights.push_back(right->id);
        } else {
            for (const auto &entry : context.model.trackIndex(clip.trackId)) {
                if (entry.start < clip.end() || moving.contains(entry.id)) continue;
                range.atMost(entry.start - clip.end());
                break;
            }
        }
    }
    const auto legal = legalDelta(context, targets.first().start, *delta, range.low, range.high);
    if (!legal)
        return setError(context, error(ErrorCode::Validation,
                                       tr("The clip cannot slide within its neighbours' source handles")));
    const qint64 back = negated(*legal);
    for (TimelineClip clip : targets) {
        if (!shift(clip, *legal, 0, 0))
            return setError(context, error(ErrorCode::InvalidArgument, tr("The slide is out of range")));
        after.insert(clip.id, clip);
    }
    for (const QUuid &id : lefts) {
        TimelineClip clip = before.value(id);
        if (!shift(clip, 0, *legal, 0))
            return setError(context, error(ErrorCode::InvalidArgument, tr("The slide is out of range")));
        after.insert(id, clip);
    }
    for (const QUuid &id : rights) {
        TimelineClip clip = before.value(id);
        if (!shift(clip, *legal, back, *legal))
            return setError(context, error(ErrorCode::InvalidArgument, tr("The slide is out of range")));
        after.insert(id, clip);
    }
    context.plan.appliedDelta = *legal;
    emitClips(context, after);
}

} // namespace QindaTK
