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
