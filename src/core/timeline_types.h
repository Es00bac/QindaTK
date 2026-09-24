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

// A transition into a clip from the clip that ends where it starts on the
// same track (WP-071, docs/sequence-edits.md "Transitions", D-120). The
// region is [cut - beforeCut, cut + afterCut) in timeline ticks, fixed
// relative to the cut; the incoming clip lends beforeCut ticks of head handle
// and the outgoing clip afterCut ticks of tail handle. An empty type is none.
struct TimelineTransition {
    QString type;            // "dissolve", "wipe", "dip"; empty = no transition
    qint64 beforeCut = 0;    // ticks
    qint64 afterCut = 0;     // ticks
    QString alignment;       // "start", "center", "end" (how the length was placed)
    QVariantMap parameters;  // type parameters (wipe direction, dip colour, audio curve)
    bool isNull() const { return type.isEmpty(); }
    qint64 duration() const { return beforeCut + afterCut; }
    friend bool operator==(const TimelineTransition &, const TimelineTransition &) = default;
};

struct TimelineClip {
    // AGENT-CONTRACT: a Musical (beat-locked) clip's canonical start is its
    // musicalTick beat anchor plus an exact musicalOffset in timeline ticks;
    // `start` is derived from the TimeMap and follows tempo edits. Duration
    // stays absolute (no time-stretch). Time clips keep `start` (WP-049,
    // docs/tempo-automation.md).
    enum class Anchor { Time, Musical };
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
    Anchor anchor = Anchor::Time;
    qint64 musicalTick = 0;     // TimeMap PPQ tick of the start's beat anchor (Musical)
    qint64 musicalOffset = 0;   // exact timeline ticks from that anchor to start (Musical)
    // AGENT-CONTRACT: clips sharing a non-null linkId (a picture and its
    // sound) move, trim, split and switch together when a request sets
    // `linked` (D-119). Split and paste give the new parts a new group.
    QUuid linkId;
    TimelineTransition transition; // into this clip at its start (D-120)
    qint64 end() const { return start + duration; }
    friend bool operator==(const TimelineClip &, const TimelineClip &) = default;
};

struct TimelineMarker {
    enum class Anchor { Time, Musical };
    QUuid id;
    qint64 position = 0;     // ticks; derived from musicalTick for Musical anchors
    qint64 duration = 0;     // 0 is a point cue; > 0 is a region; end() never overflows
    qint64 musicalTick = 0;  // TimeMap PPQ tick, canonical for Musical anchors
    Anchor anchor = Anchor::Time;
    QString label;
    QString color;
    QString kind;
    bool locked = false;
    qint64 end() const { return position + duration; }
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
    TrackProperty, ReorderTrack, ResizeTrack, AddTrack, RemoveTrack, InsertClips,
    // WP-071 sequence edits (docs/sequence-edits.md)
    Roll, Slip, Slide, SetTransition, SwitchSource, Link, Unlink
};

// A clip to insert: clip.start is relative to the insertion point and
// trackOffset counts visible tracks below the destination track.
struct TimelineClipTemplate {
    TimelineClip clip;
    int trackOffset = 0;
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
    // Include every clip that shares a linkId with a selected clip (D-119).
    bool linked = false;
    // Round the moved edit point, slip distance or switch time to a program
    // frame start of the TimeMap frame rate (D-118).
    bool frameExact = false;
    QString label;               // history text; a per-kind default when empty
    qint64 grabTicks = 0;        // pointer time at gesture start
    std::optional<SnapshotHeader> expected;
    // SetClipTiming absolute values; anchor switches beat-locked/time-locked
    std::optional<qint64> start, duration, sourceOffset;
    std::optional<TimelineClip::Anchor> clipAnchor;
    // AddMarker template / EditMarker replacement fields
    std::optional<TimelineMarker> marker;
    // TrackProperty / AddTrack
    QString property;
    QVariant value;
    std::optional<TimelineTrack> track;
    // InsertClips: new clips placed at input.pointerTicks on trackId and below
    QVector<TimelineClipTemplate> templates;
    // SetTransition: the transition into clipIds[0]; std::nullopt or an empty
    // type removes it. beforeCut + afterCut is the requested length and
    // alignment places it ("start": after the cut, "end": before it).
    std::optional<TimelineTransition> transition;
    // SwitchSource at input.pointerTicks: the part from there on shows
    // resourceId from source ticks `sourceOffset` (at the switch time) with
    // `sourceDuration` available; linked clips of another kind take
    // linkedResourceId (resourceId when empty).
    QString resourceId;
    QString linkedResourceId;
    std::optional<qint64> sourceDuration;
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
