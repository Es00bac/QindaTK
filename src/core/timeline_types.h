// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "timeline_time.h"

#include <QSet>
#include <QUuid>
#include <QVariantMap>
#include <QVector>
#include <optional>

namespace QindaTK {

// Records of the reference timeline document. IDs are UUIDs, never row
// numbers or delegate pointers; every time value is an exact tick count in
// the owning TimelineModel's timebase.
struct TimelineTrack {
    QUuid id;
    QString name;
    QString kind;          // "audio", "video", "midi", ... ; empty accepts any clip
    int height = 0;        // logical px; 0 uses the view's density default
    bool muted = false;
    bool solo = false;
    bool armed = false;
    bool locked = false;
    bool hidden = false;
    QString routing;       // read-only summary supplied by the host
    QString color;         // document data; empty uses the theme
    friend bool operator==(const TimelineTrack &, const TimelineTrack &) = default;
};

struct TimelineClip {
    QUuid id;
    QUuid trackId;
    qint64 start = 0;           // timeline ticks
    qint64 duration = 1;        // ticks, > 0; start + duration never overflows
    qint64 sourceOffset = 0;    // ticks into the source media
    qint64 sourceDuration = -1; // available source ticks; -1 is unbounded
    QString resourceId;         // media provider key for peaks and frames
    QString label;
    QString color;
    QString kind;               // must match a non-empty track kind when required
    bool locked = false;
    qint64 end() const { return start + duration; }
    friend bool operator==(const TimelineClip &, const TimelineClip &) = default;
};

struct TimelineMarker {
    enum class Anchor { Time, Musical };
    QUuid id;
    qint64 position = 0;     // ticks; derived from musicalTick for Musical anchors
    qint64 duration = 0;     // 0 is a point cue; > 0 is a region
    qint64 musicalTick = 0;  // TimeMap PPQ tick, canonical for Musical anchors
    Anchor anchor = Anchor::Time;
    QString label;
    QString color;
    QString kind;
    bool locked = false;
    friend bool operator==(const TimelineMarker &, const TimelineMarker &) = default;
};

// One object's state before and after a transaction. A missing before is a
// creation and a missing after is a removal. TrackOrder carries full orders.
struct TimelineChange {
    enum class Kind { Track, TrackOrder, Clip, Marker };
    Kind kind = Kind::Clip;
    QUuid id;
    std::optional<TimelineTrack> trackBefore, trackAfter;
    std::optional<TimelineClip> clipBefore, clipAfter;
    std::optional<TimelineMarker> markerBefore, markerAfter;
    QVector<QUuid> orderBefore, orderAfter;
};

struct TimelineChangeSet {
    QString transactionId;
    QString label;
    QVector<TimelineChange> changes;
    bool isEmpty() const { return changes.isEmpty(); }
};

enum class TimelineEditKind {
    Move, TrimStart, TrimEnd, Split, Delete, SetClipTiming,
    AddMarker, MoveMarker, EditMarker, RemoveMarker,
    TrackProperty, ReorderTrack, ResizeTrack, AddTrack, RemoveTrack
};

enum class TimelineSnapGrid { None, Frames, Seconds, Samples };

// What a gesture intends. Stable targets and the revision guard are fixed at
// begin; TimelineEditInput carries the values that change while dragging.
struct TimelineEditRequest {
    TimelineEditKind kind = TimelineEditKind::Move;
    QVector<QUuid> clipIds;
    QUuid anchorClipId;          // clip whose edges snap; defaults to clipIds[0]
    QUuid markerId;
    QUuid trackId;
    QUuid beforeTrackId;         // ReorderTrack/AddTrack destination; null = last
    bool ripple = false;
    QString label;               // history text; a per-kind default when empty
    qint64 grabTicks = 0;        // pointer time at gesture start
    std::optional<SnapshotHeader> expected;
    // SetClipTiming absolute values
    std::optional<qint64> start, duration, sourceOffset;
    // AddMarker template / EditMarker replacement fields
    std::optional<TimelineMarker> marker;
    // TrackProperty / AddTrack
    QString property;
    QVariant value;
    std::optional<TimelineTrack> track;
};

struct TimelineEditInput {
    qint64 pointerTicks = 0;     // delta = pointerTicks - request.grabTicks
    int trackDelta = 0;          // visible-track offset for Move
    int trackHeight = 0;         // ResizeTrack preview height
    QUuid beforeTrackId;         // ReorderTrack destination while dragging (overrides the request)
    bool snap = true;
    qint64 snapToleranceTicks = 0;
    TimelineSnapGrid grid = TimelineSnapGrid::None;
    std::optional<qint64> playheadTicks;
};

struct TimelineSnapResult {
    bool snapped = false;
    qint64 target = 0;
    qint64 adjustment = 0;
    QString kind;   // clipStart, clipEnd, marker, markerEnd, playhead, rangeStart, rangeEnd, grid, origin
    QUuid targetId;
};

struct TimelinePlan {
    TimelineChangeSet changes;
    qint64 appliedDelta = 0;
    int appliedTrackDelta = 0;
    TimelineSnapResult snap;
    OperationError error;    // non-empty: the preview is invalid and cannot commit
    QSet<QUuid> touchedClips;
    QSet<QUuid> touchedMarkers;
};

} // namespace QindaTK
