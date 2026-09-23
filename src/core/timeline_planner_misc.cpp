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

void setError(TimelinePlanner::Context &context, OperationError error)
{
    context.plan.error = std::move(error);
    context.plan.changes.changes.clear();
}

QString operationFor(TimelineEditKind kind)
{
    switch (kind) {
    case TimelineEditKind::Move: return QStringLiteral("move");
    case TimelineEditKind::TrimStart:
    case TimelineEditKind::TrimEnd: return QStringLiteral("trim");
    case TimelineEditKind::Split: return QStringLiteral("split");
    case TimelineEditKind::Delete: return QStringLiteral("delete");
    case TimelineEditKind::SetClipTiming: return {};
    case TimelineEditKind::AddMarker:
    case TimelineEditKind::MoveMarker:
    case TimelineEditKind::EditMarker:
    case TimelineEditKind::RemoveMarker: return QStringLiteral("markers");
    case TimelineEditKind::TrackProperty:
    case TimelineEditKind::ReorderTrack:
    case TimelineEditKind::ResizeTrack:
    case TimelineEditKind::AddTrack:
    case TimelineEditKind::RemoveTrack: return QStringLiteral("tracks");
    }
    return {};
}

bool clipKind(TimelineEditKind kind)
{
    return kind == TimelineEditKind::Move || kind == TimelineEditKind::TrimStart
        || kind == TimelineEditKind::TrimEnd || kind == TimelineEditKind::Split
        || kind == TimelineEditKind::Delete || kind == TimelineEditKind::SetClipTiming;
}

void pushTrackChange(TimelinePlanner::Context &context, const TimelineTrack *before,
                     std::optional<TimelineTrack> after)
{
    if (before && after && *before == *after) return;
    TimelineChange change;
    change.kind = TimelineChange::Kind::Track;
    change.id = before ? before->id : after->id;
    if (before) change.trackBefore = *before;
    change.trackAfter = std::move(after);
    context.plan.changes.changes.push_back(change);
}

void pushOrderChange(TimelinePlanner::Context &context, const QVector<QUuid> &after)
{
    if (after == context.model.trackOrder()) return;
    TimelineChange change;
    change.kind = TimelineChange::Kind::TrackOrder;
    change.orderBefore = context.model.trackOrder();
    change.orderAfter = after;
    context.plan.changes.changes.push_back(change);
}

void pushMarkerChange(TimelinePlanner::Context &context, const TimelineMarker *before,
                      std::optional<TimelineMarker> after)
{
    if (before && after && *before == *after) return;
    TimelineChange change;
    change.kind = TimelineChange::Kind::Marker;
    change.id = before ? before->id : after->id;
    if (before) change.markerBefore = *before;
    change.markerAfter = std::move(after);
    context.plan.changes.changes.push_back(change);
}

// A marker record must carry exact, representable values before it is stored.
OperationError checkMarker(const TimelinePlanner::Context &context, const TimelineMarker &marker)
{
    if (marker.duration < 0)
        return TimelinePlanner::error(ErrorCode::Validation, tr("A region cannot have a negative length"),
                                      QStringLiteral("duration"));
    if (!TimelineTime::add(marker.position, marker.duration))
        return TimelinePlanner::error(ErrorCode::InvalidArgument, tr("The marker is out of range"));
    if (!context.policy.allowNegativeTime() && marker.position < 0)
        return TimelinePlanner::error(ErrorCode::Validation, tr("The marker would lie before zero"),
                                      QStringLiteral("position"));
    return {};
}
} // namespace

OperationError TimelinePlanner::error(ErrorCode code, const QString &message, const QString &field)
{
    return {code, message, field, {}};
}

QString TimelinePlanner::clipName(const TimelineClip &clip)
{
    return clip.label.isEmpty() ? clip.id.toString(QUuid::WithoutBraces) : clip.label;
}

TimelineMarker TimelinePlanner::derivedMarker(const TimelineModel &model, TimelineMarker marker)
{
    if (marker.anchor != TimelineMarker::Anchor::Musical) return marker;
    if (const auto tick = model.musicalTickFor(marker.position)) marker.musicalTick = *tick;
    return model.withDerivedPosition(marker);
}

QString TimelinePlanner::defaultLabel(TimelineEditKind kind, bool ripple)
{
    switch (kind) {
    case TimelineEditKind::Move: return ripple ? tr("Ripple move clips") : tr("Move clips");
    case TimelineEditKind::TrimStart: return ripple ? tr("Ripple trim clip start") : tr("Trim clip start");
    case TimelineEditKind::TrimEnd: return ripple ? tr("Ripple trim clip end") : tr("Trim clip end");
    case TimelineEditKind::Split: return tr("Split clips");
    case TimelineEditKind::Delete: return ripple ? tr("Ripple delete clips") : tr("Delete clips");
    case TimelineEditKind::SetClipTiming: return tr("Set clip timing");
    case TimelineEditKind::AddMarker: return tr("Add marker");
    case TimelineEditKind::MoveMarker: return tr("Move marker");
    case TimelineEditKind::EditMarker: return tr("Edit marker");
    case TimelineEditKind::RemoveMarker: return tr("Remove marker");
    case TimelineEditKind::TrackProperty: return tr("Change track");
    case TimelineEditKind::ReorderTrack: return tr("Reorder tracks");
    case TimelineEditKind::ResizeTrack: return tr("Resize track");
    case TimelineEditKind::AddTrack: return tr("Add track");
    case TimelineEditKind::RemoveTrack: return tr("Remove track");
    }
    return tr("Edit timeline");
}

OperationError TimelinePlanner::validateRequest(const TimelineModel &model,
                                                const TimelineEditRequest &request)
{
    const auto &policy = *model.policy();
    const QString operation = operationFor(request.kind);
    if (!operation.isEmpty()) {
        if (auto failure = policy.requireOperation(operation)) return failure;
    }
    if (request.ripple) {
        if (auto failure = policy.requireOperation(QStringLiteral("ripple"))) return failure;
    }
    if (clipKind(request.kind)) {
        if (request.clipIds.isEmpty())
            return error(ErrorCode::InvalidArgument, tr("No clips were selected for this edit"));
        for (const auto &id : request.clipIds) {
            const auto *clip = model.clipRecord(id);
            if (!clip) return error(ErrorCode::InvalidArgument, tr("Unknown clip %1").arg(id.toString(QUuid::WithoutBraces)));
            const auto *track = model.trackRecord(clip->trackId);
            if (clip->locked) return error(ErrorCode::Validation, tr("Clip “%1” is locked").arg(clipName(*clip)));
            if (track && track->locked) return error(ErrorCode::Validation, tr("Track “%1” is locked").arg(track->name));
        }
        if (request.kind == TimelineEditKind::SetClipTiming && request.clipIds.size() != 1)
            return error(ErrorCode::InvalidArgument, tr("Numeric timing edits one clip at a time"));
    }
    switch (request.kind) {
    case TimelineEditKind::AddMarker:
        if (!request.marker) return error(ErrorCode::InvalidArgument, tr("A new marker needs a position"));
        break;
    case TimelineEditKind::MoveMarker:
    case TimelineEditKind::EditMarker:
    case TimelineEditKind::RemoveMarker: {
        const auto *marker = model.markerRecord(request.markerId);
        if (!marker) return error(ErrorCode::InvalidArgument, tr("Unknown marker"));
        if (marker->locked && request.kind != TimelineEditKind::EditMarker)
            return error(ErrorCode::Validation, tr("Marker “%1” is locked").arg(marker->label));
        if (request.kind == TimelineEditKind::EditMarker && !request.marker)
            return error(ErrorCode::InvalidArgument, tr("No marker values were supplied"));
        break;
    }
    case TimelineEditKind::TrackProperty:
    case TimelineEditKind::ReorderTrack:
    case TimelineEditKind::ResizeTrack:
    case TimelineEditKind::RemoveTrack:
        if (!model.trackRecord(request.trackId)) return error(ErrorCode::InvalidArgument, tr("Unknown track"));
        break;
    case TimelineEditKind::AddTrack:
        if (!request.track) return error(ErrorCode::InvalidArgument, tr("A new track needs a template"));
        break;
    default:
        break;
    }
    return {};
}

TimelinePlan TimelinePlanner::plan(const TimelineModel &model, const TimelineEditRequest &request,
                                   const TimelineEditInput &input, const QString &operationId)
{
    Context context{model, *model.policy(), request, input, operationId, {}};
    context.plan.changes.transactionId = operationId;
    context.plan.changes.label = request.label.isEmpty() ? defaultLabel(request.kind, request.ripple)
                                                         : request.label;
    if (auto failure = validateRequest(model, request)) {
        context.plan.error = failure;
        return context.plan;
    }
    switch (request.kind) {
    case TimelineEditKind::Move: planMove(context); break;
    case TimelineEditKind::TrimStart:
    case TimelineEditKind::TrimEnd: planTrim(context); break;
    case TimelineEditKind::Split: planSplit(context); break;
    case TimelineEditKind::Delete: planDelete(context); break;
    case TimelineEditKind::SetClipTiming: planClipTiming(context); break;
    case TimelineEditKind::AddMarker:
    case TimelineEditKind::MoveMarker:
    case TimelineEditKind::EditMarker:
    case TimelineEditKind::RemoveMarker: planMarker(context); break;
    default: planTrack(context); break;
    }
    if (!context.plan.error) finish(context);
    return context.plan;
}

void TimelinePlanner::planMarker(Context &context)
{
    const auto &request = context.request;
    const auto *before = context.model.markerRecord(request.markerId);
    switch (request.kind) {
    case TimelineEditKind::AddMarker: {
        TimelineMarker marker = *request.marker;
        if (marker.id.isNull() || context.model.markerRecord(marker.id)
            || context.model.clipRecord(marker.id) || context.model.trackRecord(marker.id))
            marker.id = QUuid::createUuidV5(QUuid(context.operationId), QByteArray("marker"));
        marker = derivedMarker(context.model, marker);
        if (auto failure = checkMarker(context, marker)) return setError(context, failure);
        pushMarkerChange(context, nullptr, marker);
        return;
    }
    case TimelineEditKind::MoveMarker: {
        auto delta = TimelineTime::subtract(context.input.pointerTicks, request.grabTicks);
        if (!delta) return setError(context, error(ErrorCode::InvalidArgument, tr("The marker is out of range")));
        if (context.input.snap && context.input.snapToleranceTicks > 0) {
            QVector<qint64> candidates;
            if (const auto moved = TimelineTime::add(before->position, *delta)) candidates.push_back(*moved);
            if (before->duration > 0) {
                const auto end = TimelineTime::add(before->position + before->duration, *delta);
                if (end) candidates.push_back(*end);
            }
            TimelineSnapContext snap;
            snap.tolerance = context.input.snapToleranceTicks;
            snap.excludedMarker = before->id;
            snap.playhead = context.input.playheadTicks;
            snap.grid = context.input.grid;
            const auto result = TimelineSnapper::snap(context.model, candidates, snap);
            if (result.snapped) {
                *delta += result.adjustment;
                context.plan.snap = result;
            }
        }
        TimelineMarker marker = *before;
        const auto position = TimelineTime::add(before->position, *delta);
        if (!position) return setError(context, error(ErrorCode::InvalidArgument, tr("The marker is out of range")));
        marker.position = *position;
        if (!context.policy.allowNegativeTime() && marker.position < 0) {
            marker.position = 0;
            context.plan.snap = {};
        }
        marker = derivedMarker(context.model, marker);
        if (auto failure = checkMarker(context, marker)) return setError(context, failure);
        context.plan.appliedDelta = marker.position - before->position;
        pushMarkerChange(context, before, marker);
        return;
    }
    case TimelineEditKind::EditMarker: {
        TimelineMarker marker = *request.marker;
        marker.id = before->id;
        if (before->locked && marker.locked)
            return setError(context, error(ErrorCode::Validation,
                                           tr("Marker “%1” is locked").arg(before->label)));
        if (marker.anchor == TimelineMarker::Anchor::Musical
            && (marker.anchor != before->anchor || marker.position != before->position))
            marker = derivedMarker(context.model, marker);
        else
            marker = context.model.withDerivedPosition(marker);
        if (auto failure = checkMarker(context, marker)) return setError(context, failure);
        pushMarkerChange(context, before, marker);
        return;
    }
    case TimelineEditKind::RemoveMarker:
        pushMarkerChange(context, before, std::nullopt);
        return;
    default:
        return;
    }
}

void TimelinePlanner::planTrack(Context &context)
{
    const auto &request = context.request;
    const auto &model = context.model;
    const auto *before = model.trackRecord(request.trackId);
    switch (request.kind) {
    case TimelineEditKind::TrackProperty: {
        TimelineTrack after = *before;
        const QString &key = request.property;
        const QVariant &value = request.value;
        const bool isBool = value.metaType().id() == QMetaType::Bool;
        const bool isString = value.metaType().id() == QMetaType::QString;
        if (key == QLatin1String("name") && isString) after.name = value.toString();
        else if (key == QLatin1String("color") && isString) after.color = value.toString();
        else if (key == QLatin1String("muted") && isBool) after.muted = value.toBool();
        else if (key == QLatin1String("solo") && isBool) after.solo = value.toBool();
        else if (key == QLatin1String("armed") && isBool) after.armed = value.toBool();
        else if (key == QLatin1String("locked") && isBool) after.locked = value.toBool();
        else if (key == QLatin1String("hidden") && isBool) after.hidden = value.toBool();
        else if (key == QLatin1String("height") && value.canConvert<int>()) {
            const int height = value.toInt();
            if (height < 0 || height > 4096)
                return setError(context, error(ErrorCode::Validation, tr("Track height must be 0 to 4096")));
            after.height = height;
        } else {
            return setError(context, error(ErrorCode::InvalidArgument,
                                           tr("Unsupported track property “%1”").arg(key), key));
        }
        pushTrackChange(context, before, after);
        return;
    }
    case TimelineEditKind::ResizeTrack: {
        TimelineTrack after = *before;
        after.height = std::clamp(context.input.trackHeight, 0, 4096);
        pushTrackChange(context, before, after);
        return;
    }
    case TimelineEditKind::ReorderTrack: {
        const QUuid before = context.input.beforeTrackId.isNull() ? request.beforeTrackId
                                                                  : context.input.beforeTrackId;
        if (before == request.trackId) return;
        if (!before.isNull() && !model.trackRecord(before))
            return setError(context, error(ErrorCode::InvalidArgument, tr("Unknown destination track")));
        QVector<QUuid> order = model.trackOrder();
        order.removeAll(request.trackId);
        const qsizetype at = before.isNull() ? order.size() : order.indexOf(before);
        order.insert(at, request.trackId);
        pushOrderChange(context, order);
        return;
    }
    case TimelineEditKind::AddTrack: {
        if (model.trackCount() >= TimelineModel::kMaxTracks)
            return setError(context, error(ErrorCode::Validation, tr("The timeline has too many tracks")));
        TimelineTrack track = *request.track;
        if (track.id.isNull() || model.trackRecord(track.id) || model.clipRecord(track.id)
            || model.markerRecord(track.id))
            track.id = QUuid::createUuidV5(QUuid(context.operationId), QByteArray("track"));
        track.height = std::clamp(track.height, 0, 4096);
        pushTrackChange(context, nullptr, track);
        QVector<QUuid> order = model.trackOrder();
        const qsizetype at = request.beforeTrackId.isNull() || !model.trackRecord(request.beforeTrackId)
                                 ? order.size() : order.indexOf(request.beforeTrackId);
        order.insert(at, track.id);
        pushOrderChange(context, order);
        return;
    }
    case TimelineEditKind::RemoveTrack: {
        if (before->locked)
            return setError(context, error(ErrorCode::Validation, tr("Track “%1” is locked").arg(before->name)));
        for (const auto &entry : model.trackIndex(before->id)) {
            const auto *clip = model.clipRecord(entry.id);
            if (clip->locked)
                return setError(context, error(ErrorCode::Validation,
                    tr("Clip “%1” is locked").arg(clipName(*clip))));
            TimelineChange change;
            change.kind = TimelineChange::Kind::Clip;
            change.id = clip->id;
            change.clipBefore = *clip;
            context.plan.changes.changes.push_back(change);
        }
        QVector<QUuid> order = model.trackOrder();
        order.removeAll(before->id);
        pushOrderChange(context, order);
        pushTrackChange(context, before, std::nullopt);
        return;
    }
    default:
        return;
    }
}

void TimelinePlanner::finish(Context &context)
{
    auto &changes = context.plan.changes.changes;
    const auto &policy = context.policy;
    if (changes.size() > policy.maxChangesPerEdit())
        return setError(context, error(ErrorCode::Validation,
            tr("This edit changes %1 objects; the limit is %2")
                .arg(changes.size()).arg(policy.maxChangesPerEdit())));
    QSet<QUuid> touched;
    for (const auto &change : changes) {
        if (change.kind == TimelineChange::Kind::Clip) {
            touched.insert(change.id);
            context.plan.touchedClips.insert(change.id);
        } else if (change.kind == TimelineChange::Kind::Marker) {
            context.plan.touchedMarkers.insert(change.id);
        }
    }
    QHash<QUuid, QVector<TimelineClip>> byTrack;
    for (const auto &change : changes) {
        if (change.kind != TimelineChange::Kind::Clip || !change.clipAfter) continue;
        const TimelineClip &clip = *change.clipAfter;
        if (!policy.allowNegativeTime() && clip.start < 0)
            return setError(context, error(ErrorCode::Validation,
                tr("Clip “%1” would start before zero").arg(clipName(clip))));
        if (policy.overlapMode() != TimelineEditPolicy::RejectOverlap) continue;
        for (const auto &other : context.model.clipsInRange(clip.trackId, clip.start, clip.end())) {
            if (touched.contains(other)) continue;
            return setError(context, error(ErrorCode::Conflict,
                tr("Clip “%1” would overlap “%2”")
                    .arg(clipName(clip), clipName(*context.model.clipRecord(other)))));
        }
        byTrack[clip.trackId].push_back(clip);
    }
    for (auto it = byTrack.begin(); it != byTrack.end(); ++it) {
        auto &list = it.value();
        std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
        for (qsizetype i = 1; i < list.size(); ++i) {
            if (list.at(i - 1).end() > list.at(i).start)
                return setError(context, error(ErrorCode::Conflict,
                    tr("Clip “%1” would overlap “%2”")
                        .arg(clipName(list.at(i)), clipName(list.at(i - 1)))));
        }
    }
    if (auto failure = policy.validate(context.model, context.plan.changes)) setError(context, failure);
}

} // namespace QindaTK
