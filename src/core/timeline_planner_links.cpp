// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_planner.h"

#include "timeline_model.h"
#include "timeline_snap.h"

#include <QCoreApplication>
#include <algorithm>

// Link groups, source switches (multicam cuts) and clip insertion
// (WP-071, docs/sequence-edits.md, D-119 and D-121).
namespace QindaTK {
namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("TimelineModel", text);
}

void setError(TimelinePlanner::Context &context, OperationError error)
{
    context.plan.error = std::move(error);
    context.plan.changes.changes.clear();
}

// A selected clip that may change: present, unlocked, on an unlocked track.
const TimelineClip *editable(TimelinePlanner::Context &context, const QUuid &id)
{
    const TimelineClip *clip = context.model.clipRecord(id);
    if (!clip) {
        setError(context, TimelinePlanner::error(ErrorCode::StaleRevision,
                                                 tr("A clip in this edit no longer exists")));
        return nullptr;
    }
    const TimelineTrack *track = context.model.trackRecord(clip->trackId);
    if (clip->locked || (track && track->locked)) {
        setError(context, TimelinePlanner::error(ErrorCode::Validation,
            tr("Clip “%1” is locked").arg(TimelinePlanner::clipName(*clip))));
        return nullptr;
    }
    return clip;
}
} // namespace

QVector<QUuid> TimelinePlanner::withLinked(const TimelineModel &model, const QVector<QUuid> &ids)
{
    QVector<QUuid> result;
    QSet<QUuid> seen;
    for (const QUuid &id : ids) {
        if (seen.contains(id)) continue;
        seen.insert(id);
        result.push_back(id);
    }
    for (const QUuid &id : ids) {
        const TimelineClip *clip = model.clipRecord(id);
        if (!clip) continue;
        for (const QUuid &partner : model.linkedClips(clip->linkId)) {
            if (seen.contains(partner)) continue;
            seen.insert(partner);
            result.push_back(partner);
        }
    }
    return result;
}

QUuid TimelinePlanner::derivedLinkId(const QString &operationId, const QUuid &linkId)
{
    if (linkId.isNull()) return {};
    return QUuid::createUuidV5(QUuid(operationId), linkId.toRfc4122() + QByteArray("link"));
}

void TimelinePlanner::planLink(Context &context)
{
    const bool link = context.request.kind == TimelineEditKind::Link;
    const QVector<QUuid> ids = !link && context.request.linked
        ? withLinked(context.model, context.request.clipIds) : context.request.clipIds;
    if (link && ids.size() < 2)
        return setError(context, error(ErrorCode::InvalidArgument, tr("Linking needs at least two clips")));
    const QUuid group = link ? QUuid::createUuidV5(QUuid(context.operationId), QByteArray("link")) : QUuid();
    QHash<QUuid, TimelineClip> after;
    for (const QUuid &id : ids) {
        const TimelineClip *clip = editable(context, id);
        if (!clip) return;
        TimelineClip next = *clip;
        next.linkId = group;
        after.insert(id, next);
    }
    emitClips(context, after);
}

// AGENT-NOTE: a switch is the multicam cut (D-121). The part of each target
// from the switch time on shows the new source from an absolute source
// position the caller derives from its sync map, so repeated switches never
// accumulate drift from earlier slips or rounding.
void TimelinePlanner::planSwitchSource(Context &context)
{
    const auto &request = context.request;
    const auto &policy = context.policy;
    if (request.resourceId.isEmpty())
        return setError(context, error(ErrorCode::InvalidArgument, tr("A source switch needs a resource")));
    if (!request.sourceOffset)
        return setError(context, error(ErrorCode::InvalidArgument,
                                       tr("A source switch needs the source position at the switch time"),
                                       QStringLiteral("sourceOffset")));
    qint64 at = context.input.pointerTicks;
    if (request.frameExact) {
        const auto frame = framePoint(context.model, at);
        if (!frame) return setError(context, error(ErrorCode::InvalidArgument, tr("The switch time is out of range")));
        at = *frame;
    }
    const QSet<QUuid> primary(request.clipIds.cbegin(), request.clipIds.cend());
    const QVector<QUuid> ids = request.linked ? withLinked(context.model, request.clipIds) : request.clipIds;
    const qint64 minimum = policy.minimumClipTicks();
    const qint64 available = request.sourceDuration.value_or(-1);
    const QUuid ns(context.operationId);
    QHash<QUuid, TimelineClip> after;
    for (const QUuid &id : ids) {
        const TimelineClip *clip = editable(context, id);
        if (!clip) return;
        if (at < clip->start || at >= clip->end()) {
            if (!primary.contains(id)) continue; // a partner that ends earlier keeps its source
            return setError(context, error(ErrorCode::Validation,
                tr("Clip “%1” does not play at the switch time").arg(clipName(*clip))));
        }
        const QString resource = primary.contains(id) || request.linkedResourceId.isEmpty()
            ? request.resourceId : request.linkedResourceId;
        TimelineClip right = *clip;
        if (at > clip->start) {
            TimelineClip left = *clip;
            left.duration = at - clip->start;
            right.id = QUuid::createUuidV5(ns, clip->id.toRfc4122());
            right.start = at;
            right.duration = clip->end() - at;
            right.linkId = derivedLinkId(context.operationId, clip->linkId);
            right.transition = {};
            if (left.duration < minimum || right.duration < minimum)
                return setError(context, error(ErrorCode::Validation,
                    tr("The switch would leave a part of “%1” shorter than the minimum length").arg(clipName(*clip))));
            after.insert(left.id, left);
        }
        right.resourceId = resource;
        right.sourceOffset = *request.sourceOffset;
        right.sourceDuration = available;
        const auto sourceEnd = TimelineTime::add(right.sourceOffset, right.duration);
        if (!policy.allowBeyondSource()
            && (right.sourceOffset < 0 || !sourceEnd || (available >= 0 && *sourceEnd > available)))
            return setError(context, error(ErrorCode::Validation,
                tr("The new source has no media for all of “%1” after the switch").arg(clipName(*clip)),
                QStringLiteral("sourceOffset")));
        after.insert(right.id, right);
    }
    if (after.isEmpty())
        return setError(context, error(ErrorCode::InvalidArgument, tr("No clips were selected for this edit")));
    context.plan.appliedDelta = at;
    emitClips(context, after);
}

void TimelinePlanner::planInsert(Context &context)
{
    const auto &model = context.model;
    const QVector<QUuid> order = model.visibleTrackOrder();
    const qsizetype base = order.indexOf(context.request.trackId);
    if (base < 0)
        return setError(context, error(ErrorCode::Validation, tr("The destination track is hidden")));
    qint64 at = context.input.pointerTicks;
    if (context.input.snap && context.input.snapToleranceTicks > 0) {
        TimelineSnapContext snap;
        snap.tolerance = context.input.snapToleranceTicks;
        snap.playhead = context.input.playheadTicks;
        snap.grid = context.input.grid;
        const auto result = TimelineSnapper::snap(model, {at}, snap);
        if (result.snapped) {
            at = result.target;
            context.plan.snap = result;
        }
    }
    const QUuid ns(context.operationId);
    for (const auto &item : context.request.templates) {
        const qsizetype index = base + item.trackOffset;
        if (item.trackOffset < 0 || index >= order.size())
            return setError(context, error(ErrorCode::Validation,
                                           tr("There are not enough tracks below the destination")));
        const auto *track = model.trackRecord(order.at(index));
        if (track->locked)
            return setError(context, error(ErrorCode::Validation, tr("Track “%1” is locked").arg(track->name)));
        TimelineClip clip = item.clip;
        if (context.policy.requireMatchingTrackKind() && !clip.kind.isEmpty() && !track->kind.isEmpty()
            && clip.kind != track->kind)
            return setError(context, error(ErrorCode::Validation,
                tr("A %1 clip cannot be placed on the %2 track “%3”").arg(clip.kind, track->kind, track->name)));
        clip.id = QUuid::createUuidV5(ns, item.clip.id.toRfc4122());
        clip.trackId = track->id;
        clip.locked = false;
        // Pasted copies form their own link groups and carry no transition
        // (D-119, D-120): their cuts are new.
        clip.linkId = derivedLinkId(context.operationId, item.clip.linkId);
        clip.transition = {};
        const auto start = TimelineTime::add(at, item.clip.start);
        if (!start || clip.duration < 1 || !TimelineTime::add(*start, clip.duration))
            return setError(context, error(ErrorCode::InvalidArgument, tr("An inserted clip is out of range")));
        clip.start = *start;
        if (model.clipRecord(clip.id) || model.trackRecord(clip.id) || model.markerRecord(clip.id))
            return setError(context, error(ErrorCode::Conflict, tr("An inserted clip would reuse an ID")));
        TimelineChange change;
        change.kind = TimelineChange::Kind::Clip;
        change.id = clip.id;
        change.clipAfter = clip;
        context.plan.changes.changes.push_back(change);
    }
    context.plan.appliedDelta = at;
}

} // namespace QindaTK
