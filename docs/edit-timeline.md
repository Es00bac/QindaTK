<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Edit timeline

The shared multitrack edit timeline (WP-048: AUD-04 EditTimeline, AUD-05
TrackHeader, AUD-06 ClipItem, AUD-07 TimeSelection, AUD-08 MarkerLane). It is
the timeline UI and model layer for audio, video and animation editors. It is
not an audio engine, a media decoder or an Audacity port: playback, recording,
peak pyramids, crossfades and time stretch belong to the host or later
packages. Decision [D-045](decisions-application.md#d-045--keep-timeline-placement-in-an-exact-tick-model-and-views-delegate-free)
records why the layers are split this way.

```
TimelineModel ── TimelinePlanner + TimelineEditPolicy ── TimelineSnapper
      │ committed state, one preview, history, JSON
TimelineViewport ── TimelineVisibleList ×3 (clips, tracks, markers)
      │ exact origin tick, zoom, scroll, bounded rows, hit tests
EditTimeline ── EditTimelineLanes · EditTimelineHeaders · MarkerLane
                ClipItem · TrackHeader · TimeSelection · TimelineTimingEditor
TimelineMediaProvider ── TimelineMediaBackend (host)      [peaks, frames]
TimelineClipboard / TimelineDropTarget ── WP-016 transfer [copy, paste, drop]
```

## Exact time

Every canonical position is a signed 64-bit tick count in the model's
declared timebase: `timebaseNumerator/timebaseDenominator` ticks per second,
decimal strings in QML. The default, 705,600,000 ticks per second, divides
evenly into 8–192 kHz sample rates and the frame durations of 24, 25, 30, 48,
50, 60, 90, 100 and 120 fps and their 1000/1001 forms, so a 29.97 fps frame
is exactly 23,543,520 ticks and a 48 kHz sample exactly 14,700. A host may
choose another timebase (`setTimebase` while empty, or `loadContent`).
Pixels, `double` seconds and QML Numbers are presentation values only.

`TimelineTime` (C++) supplies the arithmetic: `mulDiv` (128-bit intermediate
from portable 32-bit limbs, one rounding with a `TimeMap::Rounding` mode,
overflow reported), checked `add`/`subtract`, strict `parseDecimal`
(optional `-`, digits, no `+`), `rescale` between rates, `formatSeconds` and
`parseSeconds` (at most nine fractional digits) and `gridBracket`, which puts
grid line *k* at `origin + ceil(k · step)` for a rational step. Rate parts are
limited to 1..2³¹−1 so every product fits the 2⁶² factor bound. Positions stay
exact to ±2⁶³ ticks (about 414 years at the default timebase); QML receives
them as decimal strings, so values beyond 2⁵³ survive the UI.

Sample and frame conversions use the model's `timeMap` (a default `TimeMap`
until the host supplies its own, which should be the one its `TransportModel`
uses): samples at `sampleRate`; frames at the rational frame rate relative to
the TimeMap origin, where frame *f* starts at `ceil(f · ticksPerFrame)`.
Musical ticks go through `TimeMap` tempo segments (sample-quantized there).

| QML invokable | Result |
| --- | --- |
| `addTicks`, `subtractTicks`, `compareTicks` | exact; `""` on overflow, `2` when an input is invalid |
| `ticksToSamplesText`, `samplesToTicksText`, `ticksToFramesText`, `frameStartTicksText` | exact with an explicit rounding enum |
| `formatTicks(ticks, mode, dropFrame)` / `parseTicks(text, mode, dropFrame)` | positions; modes `timecode`, `seconds`, `frames`, `samples`, `ticks`; timecode via `TimeMap` |
| `formatDuration` / `parseDuration` | lengths: no TimeMap origin; timecode lengths are non-drop frame counts |
| `stepTicks(from, grid, steps)` | the next/previous grid line (`frames`, `seconds`, `samples`, `none` = ticks); an off-grid position is quantized by its first step |
| `snapTicks(ticks, options)` | one exact position snapped as below; `{ticks, snapped, kind, targetId}` |
| `ticksToSeconds`, `secondsToTicks` | display only (double) |

## Records and identity

Tracks, clips and markers have UUID IDs that are unique across all three
kinds and never reused by an edit. Row numbers and delegates are never
identity. Limits: 10,000 tracks, 2,000,000 clips, 200,000 markers.

| Record | Fields |
| --- | --- |
| Track | `id`, `name`, `kind` (`audio`, `video`, `midi`, …; empty accepts any clip), `height` (logical px, 0 = view default, ≤ 4096), `muted`, `solo`, `armed`, `locked`, `hidden`, `routing` (host summary), `color` |
| Clip | `id`, `trackId`, `start`, `duration` (≥ 1 tick), `sourceOffset`, `sourceDuration` (−1 unbounded), `resourceId` (media key), `label`, `color`, `kind`, `locked` |
| Marker | `id`, `position`, `duration` (0 point, > 0 region), `anchor` (`time` or `musical`), `musicalTick`, `label`, `color`, `kind`, `locked` |

A **musical** marker's canonical value is its `musicalTick`; its position is
derived from the TimeMap and follows tempo changes (a tempo change that moves
one advances the revision). A derived position saturates so that position +
duration stays representable. A **time** marker keeps its tick position
through tempo and frame-rate changes. Both anchors survive save and reopen.
Document colours are data and are drawn as given.

The model indexes clips per track, sorted by `(start, id)` with the longest
duration, so an interval query is a binary search plus the hits; clip and
marker edges are kept in lazily rebuilt sorted arrays for snapping. Queries
exposed to QML are bounded (`clipIdsInRange`, `clipIdsInRect` ≤ 4096).

## Edit transactions

An edit is one typed request, then any number of previews, then one commit or
cancel. The C++ API is `begin(TimelineEditRequest)`, `update(operationId,
TimelineEditInput)` → `TimelinePlan`, `commit`, `cancel` and `apply` (all
four in one call). QML uses the same shapes as maps: `beginEdit(request)`,
`updateEdit(operationId, input)`, `commitEdit`, `cancelEdit`, `applyEdit`.
Ticks in these maps are decimal strings; a JS Number is rejected.

| Request `kind` | Targets and values | Operation |
| --- | --- | --- |
| `move` | `clipIds`, `anchorClipId`, `grabTicks`, `ripple`; input `pointerTicks`, `trackDelta` | move |
| `trimStart`, `trimEnd` | `clipIds`, `anchorClipId`, `grabTicks`, `ripple`; input `pointerTicks` | trim |
| `split` | `clipIds`; input `pointerTicks` is the split point | split |
| `delete` | `clipIds`, `ripple` | delete |
| `setClipTiming` | one clip; absolute `start`, `duration`, `sourceOffset` | move/trim |
| `insertClips` | `trackId`, `clips` (start relative to the point, `trackOffset`); input `pointerTicks` | insert |
| `addMarker`, `moveMarker`, `editMarker`, `removeMarker` | `marker` map, `markerId`, `grabTicks` | markers |
| `trackProperty`, `resizeTrack`, `reorderTrack`, `addTrack`, `removeTrack` | `trackId`, `property`/`value`, input `trackHeight` or `beforeTrackId`, `track` | tracks |

Input fields: `pointerTicks` (delta = pointer − `grabTicks`), `trackDelta`
(visible tracks), `snap`, `snapToleranceTicks`, `grid`, `playheadTicks`,
`trackHeight`, `beforeTrackId`. A request can carry `expectedRevision` (and
`expectedGeneration`): `begin` refuses a stale guard with `StaleRevision` or
`StaleGeneration`. `commitEdit`/`applyEdit` return `{ok, transactionId,
changed, createdIds, revision}`; `createdIds` names the records the
transaction created (new clips, split right parts, markers, tracks), so a
caller never has to find a new record by position.

**Rules.** One session at a time (`Conflict` otherwise). A preview never
changes committed records or the revision; views read committed state plus
the preview (`effectiveClip`, `editPreview`). Any commit, undo, reload,
time-map change (tempo, meter, frame or sample rate, origin) or policy change
while a session is active retires it (`editInvalidated(operationId,
reason)`), because its preview was snapped and derived under the old state;
later `update`/`commit` for that ID return `StaleRevision`. A refused
placement stays a preview flagged `invalid`, so the view draws where the clip
would go, and cannot commit: an overlap, negative time or policy veto for any
edit, and a locked or kind-mismatched destination track for a move. Other
errors leave no preview: a locked source clip, a missing target, an arithmetic
overflow, and an insert whose destination is hidden, locked, mismatched or
short of tracks. A commit with no preview (straight after `begin`) is refused,
so views begin a session only once a gesture has moved. A commit re-runs the
policy veto, checks the history budget and applies atomically; a rejected,
stale or cancelled edit leaves every record, the revision and the history
unchanged. An accepted commit is exactly one history step with
`transactionId` = the operation ID; an empty change set commits no step.

**Semantics.** Move shifts the selection rigidly: the track offset is clamped
so every clip stays on a visible track; hidden tracks keep their clips; a
kind mismatch or locked destination is an error. Trims keep media aligned
(head trims move `sourceOffset`) and clamp at the policy minimum length, at
source bounds unless `allowBeyondSource`, and at zero unless
`allowNegativeTime`, so a dragged edge never crosses the other edge. Split
keeps the left part's ID; the right part's ID is UUIDv5(operation, clip ID),
stable across previews and redo. Ripple shifts every clip whose committed
start is at or after an edit point on the rippled tracks (the targets' tracks,
or all tracks with `RippleAllTracks`): a move's point is the earliest target
start, a trim's is the original end, a delete closes the **union** of removed
intervals. One gesture inserts or removes time at a point once, however many
selected clips share it (a parallel trim of clips that end together shifts
downstream clips by one delta, not one per clip); distinct points accumulate.
Locked tracks are not rippled; a locked clip in the way is an error. A pointer
so far out that a position would overflow is refused, never wrapped or
clamped. `reorderTrack` inserts before `beforeTrackId`; naming the dragged
track itself (or the track after it) plans no change, and an empty ID means
last. `insertClips` templates need distinct IDs, because each new ID is
UUIDv5(operation, template ID).

**Snapping** is one-dimensional over exact ticks (`TimelineSnapper`).
Candidates are the moving edges (both edges of the anchor clip for a move).
Object targets are committed clip edges (moving clips excluded), marker
positions and region ends, the playhead, the edit range and zero; the nearest
within tolerance wins, ties preferring clip edges, markers, playhead, range,
zero. Grid lines (`frames`, `seconds`, `samples`) are used only when no object
target is in reach, because a 29.97 fps grid is denser than any useful pixel
tolerance. The tolerance comes from the view as pixels converted to ticks.

**Policy.** `TimelineEditPolicy` holds `overlapMode` (`AllowOverlap`,
`RejectOverlap`), `rippleScope`, `minimumClipTicks`, `allowNegativeTime`,
`allowBeyondSource`, `requireMatchingTrackKind`, `maxChangesPerEdit`
(100,000) and per-operation availability with reasons (`capabilities`,
`setUnavailable(operation, reason)`; operations `move`, `moveAcrossTracks`,
`trim`, `split`, `delete`, `ripple`, `markers`, `tracks`, `range`, `insert`).
A C++ host subclasses it and overrides `validate(model, changeSet)` to veto
complete change sets (overwrite rules, crossfades, engine limits).

## History and the host boundary

By default the model keeps its own history: 256 steps and 64 MiB of inverse
data, oldest first out; an edit too large for the budget is refused before it
commits. Lowering `historyLimit` drops the oldest undo steps first and then
the redo steps farthest from the cursor, so every remaining redo step still
applies. Undo and redo check that every touched record still equals the
recorded state; a mismatch clears the history instead of replaying against a
different document. Loading content clears history and bumps
`resetGeneration`.

With `externalUndo`, the host's command history owns navigation (the pattern
of D-040): each commit emits `committed(transactionId, label, payload)` with
before/after records as decimal-string maps, and the host replays with
`applyHistoryChanges(transactionId, reverse, expectedRevision)` and retires
steps with `releaseHistoryTransaction`. The model rejects a replay with a
wrong revision or diverged records. It keeps the change sets of at most 4,096
unreleased transactions, oldest first out; replaying an evicted one fails
with "Unknown or released timeline transaction", so a host with a deeper
history keeps the `committed` payloads itself. A production session model can therefore
either use `TimelineModel` as its projection (loading content, observing
`committed`) or own the history while the model enforces preconditions.

**Persistence.** `toJson`/`loadJson` use schema `qindatk.timeline/1`
(`documentId`, `timebase`, `tracks`, `clips`, `markers`, `editRange`), with
every tick a decimal string. `loadContent` validates everything first and
changes nothing on failure. `contentMap` is deterministic (track order, clip
start, marker position).

## Viewport and virtualization

`TimelineViewport` owns `originTicks` (exact), `pixelsPerSecond` (clamped to
`minimumPixelsPerSecond..maximumPixelsPerSecond`), `scrollY`,
`viewportWidth/Height`, `defaultTrackHeight`, `minimumTrackHeight`,
`overscanPixels`, `summaryPixels`, `handlePixels` and `maximumClipRows`
(default 1024). `x = (ticks − origin) · zoom` is computed from an exact
integer difference; `ticksForX(x, rounding)` maps back once. It resolves
visible tracks from prefix sums and clips per visible track from the interval
index, then publishes three `TimelineVisibleList` models whose `update()`
diffs by key: rows that stay visible keep their delegate and only report
changed roles, departing rows are removed and entering rows appended. During
a preview it adds the moved clips at their preview places and a `ghost` row at
the committed place. Clips narrower than `summaryPixels` merge into `summary`
rows (the threshold doubles until the rows fit `maximumClipRows`); `truncated`
reports a hard cut. `hitTest(x, y)` returns `clip`, `trimStart`, `trimEnd`,
`track` or `none`, with trim handles reachable just outside a clip edge.
`zoomAround(x, factor)`, `scrollByPixels`, `ensureClipVisible`,
`ensureTrackVisible`, `ensureTicksVisible` and `fitToContent` move the view.

Row roles avoid Item property names so delegates declare them `required`:
clips `key, clipId, trackId, trackIndex, clipX, laneY, clipWidth, laneHeight,
label, clipColor, kind, resourceId, locked, startTicks, durationTicks,
endTicks, sourceOffsetTicks, sourceDurationTicks, visibleSourceStartTicks,
visibleSourceEndTicks, clippedStart, clippedEnd, preview, ghost, invalid,
summary, summaryCount, stackOrder`; tracks `key, trackId, trackIndex,
orderIndex, trackY, trackHeight, name, kind, muted, solo, armed, locked,
hidden, routing, trackColor, clipCount, preview`; markers `key, markerId,
markerX, markerWidth, positionTicks, durationTicks, endTicks, label,
markerColor, kind, locked, anchorMode, preview, ghost, clippedStart,
clippedEnd`.

## Media seam

`TimelineMediaProvider` issues `requestPeaks(ownerKey, resourceId,
sourceStart, sourceEnd, ticksPerSecondNumerator, ticksPerSecondDenominator,
columns)` and `requestFrames(…, count)`; each returns a request ID and later
`ready(requestId, ownerKey)` or `failed(…)`, read with `result(requestId)`.
A newer request from the same owner supersedes and cancels the older one;
`cancel`/`cancelOwner` release it; `invalidateResource` makes in-flight
results stale. Only the running request that asked can publish; everything
else is counted in `staleDropped` and dropped. Backend work starts from a
queued pump, so requests superseded in the same event-loop turn never reach
the backend; at most `maxInFlight` (4) run, 1,024 wait, 256 results are
cached by exact key. An owner's newest result stays until the owner
supersedes or cancels it; requests without an owner key keep only the newest
1,024 finished results, each released after its notification. Replacing the
backend fails every pending request (`failed`, "The media backend changed")
and then emits `backendChanged`, on which `ClipItem` asks again. A host
implements `TimelineMediaBackend::start(request, CancellationToken,
completion)` on any thread (a peak pyramid, a decoder).
`referenceBackend: true` installs the labeled **synthetic reference**
backend: a deterministic envelope-and-carrier signal per resource ID, reduced
analytically on the thread pool. It decodes no media and reports frame
requests as unsupported rather than inventing pictures.

## Transfers (WP-016)

`TimelineClipboard` copies clips as `TransferPayloadCodec` object items in the
internal MIME format (schema `qindatk.timeline.clip/1`, exact start offsets
and visible-track offsets) and pastes them as one `insertClips` transaction
with fresh IDs (`paste(model, trackId, atTicks)` returns the new IDs). A
different timebase is refused, not rounded; clips from another document that
name media need `allowForeignResources` from a host that resolved them.
`TimelineDropTarget` is the `DragDropService` endpoint: Copy only, a
side-effect-free preview, and a commit refused as `StaleRevision` if the
timeline changed after the preview.

## Views

See [controls.md](controls.md#edit-timeline) for properties, signals and
objectNames. `EditTimeline` routes every clip gesture through one pointer
controller in `EditTimelineLanes` that hit-tests the viewport, so a delegate
recycled during autoscroll never owns a gesture. The header column
(`EditTimelineHeaders`) scrolls with the lanes from the same track rows and
works the same way: its own pointer area under the headers (which run with
`gestures: false`) owns track resize and reorder, so a reorder previewed out
of view keeps its session. A resize begins on the first movement, so a click
on the seam is not an edit. Dragging near an edge (`size.dropEdge`)
autoscrolls and keeps updating the same preview (clips, ranges, markers and
track reorder). Every gesture is one session that ends exactly once: release
commits; Escape, a lost grab, invalidation, a replaced `model` or destruction
cancels, and the rest of that press does nothing. The numeric timing editor
applies with the revision it opened at, so it never writes back values the
user did not see. `TimeSelection` endpoints normalize on crossing (the fixed end
is the anchor) and an empty range commits only with `allowEmpty`; cancel
restores the committed range. The edit range lives in the model (not
undoable, persisted); the loop band on the ruler shows the `TransportModel`'s
acknowledged loop, and a drag only requests a new one.

Keyboard (timeline focused): Up/Down change track, Left/Right the clip
(Shift extends, Home/End first/last), Alt+Left/Right move by grid lines
(Shift ×10), Alt+Up/Down move to the adjacent track, Alt+Shift+Left/Right trim
the end, Ctrl+Shift+Left/Right trim the start, Ctrl+Up/Down reorder the
track, S splits at the playhead, Delete deletes (Shift+Delete ripples),
Ctrl+C/X/V copy, cut and paste, M adds a marker, Return/F2 opens numeric
timing, Escape cancels a drag or clears the selection, Ctrl+Z/Ctrl+Shift+Z/
Ctrl+Y undo and redo (local history only), Ctrl+Plus/Minus/0 zoom and fit.

## Remaining work and limits

- WP-041 (spatial SnapModel, SelectionOverlay, TransformHandles) was not
  available: snapping uses the time-axis `TimelineSnapper` described above and
  there is no marquee selection yet (Shift+click selects a track×time
  rectangle). Reconcile both when WP-041 lands.
- Real media: a host backend for peaks/frames, and an audio/video engine
  behind `TransportAdapter` (WP-060/070); the reference backend is synthetic.
- Overwrite/insert edit modes, crossfades, slip/slide, beat-locked clips
  (WP-049), linked audio/video clips and multi-view selection sync.
- Native screen-reader delivery of the virtual clip items (WP-092) and Touch
  density gesture qualification.
