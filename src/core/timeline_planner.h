// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_types.h"

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
    static void finish(Context &context);
    static OperationError error(ErrorCode code, const QString &message,
                                const QString &field = {});
    static QString clipName(const TimelineClip &clip);
    static TimelineMarker derivedMarker(const TimelineModel &model, TimelineMarker marker);
};

} // namespace QindaTK
