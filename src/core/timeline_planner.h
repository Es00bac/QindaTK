// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_types.h"

#include <QHash>

namespace QindaTK {

class TimelineModel;
class TimelineEditPolicy;

// Resolves an edit intent against the committed model into a complete
// change set. Pure with respect to the model: it reads committed state and
// never mutates. The same request and input always produce the same plan,
// including the UUIDs of clips created by a split (UUIDv5 of the operation).
class TimelinePlanner {
public:
    static OperationError validateRequest(const TimelineModel &model,
                                          const TimelineEditRequest &request);
    static TimelinePlan plan(const TimelineModel &model, const TimelineEditRequest &request,
                             const TimelineEditInput &input, const QString &operationId);
    static QString defaultLabel(TimelineEditKind kind, bool ripple);

    // Shared by the clip and marker/track halves of the implementation.
    struct Context {
        const TimelineModel &model;
        const TimelineEditPolicy &policy;
        const TimelineEditRequest &request;
        const TimelineEditInput &input;
        QString operationId;
        TimelinePlan plan;
    };
    static void planMove(Context &context);
    static void planTrim(Context &context);
    static void planSplit(Context &context);
    static void planDelete(Context &context);
    static void planClipTiming(Context &context);
    static void planMarker(Context &context);
    static void planTrack(Context &context);
    static void planInsert(Context &context);
    static void finish(Context &context);
    // Emits one change per clip in `after` that differs from its committed state.
    static void emitClips(Context &context, const QHash<QUuid, TimelineClip> &after);

    // Sequence edits (timeline_planner_nle.cpp, docs/sequence-edits.md).
    static void planRoll(Context &context);
    static void planSlip(Context &context);
    static void planSlide(Context &context);
    static void planTransition(Context &context);
    // Drops transitions whose cut an edit removed and reports ones whose
    // handles it made insufficient (a soft error); called from finish().
    static void reconcileTransitions(Context &context);
    // The frame start nearest `ticks` (TimeMap frame rate, program origin).
    static std::optional<qint64> framePoint(const TimelineModel &model, qint64 ticks);
    // `delta` for an edit point at `point` clamped into [low, high]; with
    // request.frameExact the moved point lands on a frame start in range.
    // std::nullopt when no legal value exists.
    static std::optional<qint64> legalDelta(const Context &context, qint64 point, qint64 delta,
                                            qint64 low, qint64 high);
    // Tail handle of a clip in source ticks (the int64 maximum when unbounded).
    static qint64 tailRoom(const Context &context, const TimelineClip &clip);

    // Links and source switches (timeline_planner_links.cpp).
    static QVector<QUuid> withLinked(const TimelineModel &model, const QVector<QUuid> &ids);
    static QUuid derivedLinkId(const QString &operationId, const QUuid &linkId);
    static void planLink(Context &context);
    static void planSwitchSource(Context &context);
    static OperationError error(ErrorCode code, const QString &message,
                                const QString &field = {});
    static QString clipName(const TimelineClip &clip);
    static TimelineMarker derivedMarker(const TimelineModel &model, TimelineMarker marker);

    // Ripple (timeline_planner_ripple.cpp): clips at or after an edit point
    // move by that point's shift.
    struct RipplePoint {
        qint64 at = 0;
        qint64 shift = 0;
    };
    using RipplePoints = QHash<QUuid, QVector<RipplePoint>>;
    static QVector<QUuid> rippleTracks(const TimelineModel &model, const QSet<QUuid> &sources, bool all);
    static RipplePoints pointsFor(const Context &context, const QVector<QUuid> &tracks,
                                  const QVector<QPair<QUuid, RipplePoint>> &raw);
    static bool applyRipple(Context &context, const QVector<QUuid> &tracks, RipplePoints points,
                            QHash<QUuid, TimelineClip> &after, const QSet<QUuid> &skip);
};

} // namespace QindaTK
