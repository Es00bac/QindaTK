<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Tempo and automation editing

WP-049 (AUD-09 TempoLane, AUD-19 AutomationLane) adds editable tempo and
meter events, beat-locked timeline content, parameter automation with
read/write/touch/latch modes, and an engine-facing schedule that an audio
engine reads without a GUI clock. Decision
[D-054](decisions-application.md#d-054--one-tempo-authority-a-derived-time-map-and-engine-snapshots)
records the layering.

```
TempoMapModel ──publishes──▶ TimeMap.tempoSegments ──▶ TimelineModel (beat-locked clips, musical markers)
      │ events, transactions, history, qindatk.tempo/1
AutomationModel ── lanes keyed by parameter ID, points, modes, record passes, qindatk.automation/1
      │                                   │
TempoLane · AutomationLane (CurveEditor)  EngineSchedule ──▶ EngineScheduleSnapshot (immutable, any thread)
      └──── TimelineViewport coordinates                 └──▶ MockAutomationEngine (reference consumer)
```

## Tempo map

`TempoMapModel` owns tempo and meter events. Each `TempoEvent` has a stable
UUID, a kind (`tempo` or `meter`), an exact musical position in TimeMap PPQ
ticks, and either integer **microseconds per quarter note** (the unit TimeMap
and MIDI store) or a meter `numerator/denominator`. BPM is presentation:
`formatBpm` shows 60,000,000/µs to three decimals and `parseBpm` rounds a
typed BPM once to the nearest microsecond.

Validation (`validate`, run on every preview, commit, undo and load):

- exactly one tempo and one meter at tick 0; neither can move or be removed
  (they can be edited);
- ticks ≥ 0, unique per kind; at most 4,096 events and merged segments;
- tempo within `minimumBpm..maximumBpm` (default 10–999);
- meter 1–64 beats of a power-of-two note value that divides `4 × PPQ`;
- a meter change falls on a bar line of the meter before it.

The model is the single authority: it writes the merged segments to
`TimeMap.tempoSegments` on commit, undo, redo, load and while a gesture
previews, and republishes the committed map on cancel. Nothing else may write
the segments of a TimeMap it drives. Constant tempo steps only:
`capabilities.tempoRamps` is unavailable because TimeMap segments are
constant.

Musical helpers take and return decimal strings: `formatPosition` /
`parsePosition` (`bar.beat.tick`, 1-based, following meter changes),
`snapTick` and `stepTick` on the `bar` or `beat` grid (optionally ignoring
one meter event, for moving it), `tempoAt`, `meterAt`, `adjacentEvent`.

**Transactions.** `beginEdit({kind, eventId?, expectedRevision?, label?,
tick?, bpm?|microsecondsPerQuarter?, numerator?, denominator?})` with kinds
`addTempo`, `addMeter`, `move`, `edit`, `remove`; then `updateEdit(id,
input)` any number of times, then `commitEdit` or `cancelEdit`; `applyEdit`
does all four. One session at a time. A preview never changes committed
events or the revision. An invalid candidate is reported
(`{ok:true, valid:false, error}`, `previewError`) and never published; a
valid one is published so the timeline shows its effect live. Commit is one
history step (256 kept) and returns `createdIds`; undo/redo check that every
touched event still equals the recorded state.

**Placement check.** With `timeline` set, every published candidate asks the
timeline for `musicalPlacementError()`: under `RejectOverlap` a tempo edit
that would push a beat-locked clip onto another clip, or before zero, stays
visible as an invalid preview and cannot commit (the committed map is
restored).

**Persistence.** `toJson`/`loadJson` (schema `qindatk.tempo/1`:
`ticksPerQuarter`, events with decimal-string ticks and tempo). A file with a
different PPQ than the TimeMap is refused, never rescaled. Loading clears the
history.

## Beat-locked and time-locked clips

`TimelineClip.anchor` is `time` (default) or `musical`. A time-locked clip
keeps its exact tick `start` through every tempo, meter, frame-rate and
origin change. A **beat-locked** clip's canonical start is `musicalTick` (the
TimeMap PPQ tick nearest its start) plus `musicalOffset` (the exact timeline
ticks from that tick's derived position to the start). When the time map
changes, `start` is re-derived as `position(musicalTick) + musicalOffset`, so:

- the clip follows the beat it sits on; the sub-PPQ residual (below one tick,
  about 0.5 ms at 120 BPM and 960 PPQ) stays constant in absolute time;
- under an unchanged map the round trip is exact, so undoing a tempo edit
  restores every clip bit-exactly;
- duration stays absolute: audio is not time-stretched and a MIDI clip's
  length does not follow the beat (WP-064 and a later stretch engine).

The planner re-anchors a beat-locked clip whenever its start or anchor
changes (move, trim, split, insert, numeric timing), in the one pass every
planned change goes through. Switching is a `setClipTiming` edit with
`anchor: "time" | "musical"`. History preconditions compare beat-locked clips
by their canonical fields, like musical markers, so a timeline undo stays
valid after a tempo edit moved the clip. A tempo change moves beat-locked
clips without a timeline history step (the tempo step owns it) and advances
the timeline revision. JSON carries `anchor` and, for musical clips,
`musicalTick`/`musicalOffset`; a musical clip saved without them is anchored
from its start on load. Clip rows expose `anchorMode`; `ClipItem` shows a
music icon and says "Beat-locked" to assistive technology.

## Automation

`AutomationModel` holds one lane per parameter ID. An `AutomationParameter`
has a name and a canonical domain (`UnitDimension`, unit ID, minimum <
maximum, reusing `CurveAxisDomain`), a default value, an optional `step` grid
and the interpolation for new points. Values are stored in canonical units;
the normalized 0–1 projection exists only for the CurveEditor. A point has a
stable UUID, exact timeline ticks (in the model's timebase, default
705,600,000/s), a value within range, the interpolation of its outgoing
segment (`step`, `linear`, `cubic`) and optional in/out handles in the
segment's local frame (x = fraction of the exact tick span, y = normalized
value). Points are time-locked; they do not follow tempo in this package.

Evaluation (`AutomationCurve::valueAt`) holds the first/last value outside
the points, returns stored values exactly at point ticks and inside step
segments, lerps linear segments in canonical units from exact integer tick
differences, and evaluates cubic segments with `CurveEvaluator` on a
segment-local two-knot curve, so normalized x never spans the whole
timeline. No points: the default value.

**Edits** use the same session pattern (`beginEdit/updateEdit/commitEdit/
cancelEdit/applyEdit`, kinds `movePoints`, `addPoint`, `removePoints`,
`setPoint`, `moveSegment`, `setHandle`, `setInterpolation`,
`editParameter`). A move is rigid and clamped so no point crosses an
unmoved neighbour or zero (overlapping moves stop one tick short, never
reorder); a single point takes its exact target value, so a vertical move
never changes its time. `setPoint` (numeric entry) refuses a time outside its
neighbours or a value outside the range. `setHandle` makes the segment cubic
and keeps its controls ordered in x. `editParameter` changes the range and
clamps only out-of-range values, in the same undo step. Every commit is one
history step; `toJson`/`loadJson` use schema `qindatk.automation/1`, values
as JSON numbers (Qt writes the shortest exact representation, so values
survive save/reopen bit-exactly).

## Modes and the record path

| Mode | Transport running, control touched | Released | Transport stops |
| --- | --- | --- | --- |
| `off` | live value only | — | — |
| `read` | curve plays; gestures are live only | — | — |
| `touch` | writes from the touch | pass ends, returns to the curve | pass ends |
| `latch` | writes from the touch | holds the last value | pass ends |
| `write` | writes from transport start, touched or not | keeps writing | pass ends |

The caller supplies every timestamp as exact ticks from its transport or
engine clock: `transportStarted/Stopped/Moved(ticks)` (QML
`setTransportState`, `setTransportTicks`) and `beginGesture`,
`recordGesture`, `endGesture(parameterId, ticks, value)`. A pass keeps the
curve before its start exactly (an entry point holds the original value),
replaces points inside it with the recorded samples (constant runs are
thinned), holds to the pass end and ramps back to the original curve over
`returnTicks` (default 1/50 s). A timestamp that goes backwards (a loop wrap)
ends the pass and starts another. One pass is one undo step. Starting a pass
invalidates a point edit on that parameter; a point edit cannot begin while
it is written; undo waits for recording to stop.

`capabilities` lists every mode with `available` and `reason`; a host (or the
engine) declares a mode unsupported with `setModeAvailability(mode, false,
reason)`. `setModeName` then fails with `Unsupported` and the reason, and a
lane already in that mode falls back to `read`. `off` and `read` are always
available. `writeState(id)` reports mode, touched, latched, writing, live
value and pass start.

## Engine schedule

`EngineSchedule` republishes an immutable `EngineScheduleSnapshot` whenever
committed tempo or automation changes (tempo `revisionChanged`, never a
preview), a mode changes, a write pass changes a live value, or the TimeMap's
sample rate, PPQ or origin changes. `acquire()` returns a
`std::shared_ptr<const EngineScheduleSnapshot>` from an atomic and is safe on
any thread. A snapshot holds its own TimeMap copy detached from every thread
(`moveToThread(nullptr)`), the committed tempo events and, per parameter, the
committed points (implicitly shared, never mutated), mode, and an override
(`writing` or `off`) with the live value. Queries take exact timestamps only:
`ticksAtSample`, `sampleAtTicks`, `musicalTickAtSample`,
`sampleAtMusicalTick` (the same rounding as `TimelineModel::musicalTickFor`),
`microsecondsPerQuarterAt`, `valueAtSample`/`valueAtTicks` (`{known,
automated, value}`) and the sample-accurate `renderBlock`. Cubic segments
allocate a small curve per evaluation, and dropping the last reference frees
memory: a realtime engine renders into its own buffers and releases retired
snapshots off its audio thread (WP-060).

`MockAutomationEngine` is the labeled reference consumer: a deterministic
sample clock (`play`, `stop`, `locate`, `process(frames)`), values at
requested timestamps (`valuesAt`, `render`, thread-safe `renderBlock`) and
control gestures stamped with its own clock (`touch`, `move`, `release`). It
opens no device and is not realtime; the example advances it from a timer
that stands in for audio callbacks.

## Lanes

`Tk.TempoLane` and `Tk.AutomationLane` share the `TimelineViewport` of an
`EditTimeline` (`timeline.viewport`), so they scroll and zoom with it; place
them in the timeline's lane column (see `examples/tempo-automation`).
Properties, keys and objectNames are in
[controls.md](controls.md#tempo-and-automation). The automation lane hosts
`Tk.CurveEditor` (with `referenceLine: false`): knots map to exact ticks
through the viewport and to canonical values through the model, and a
coordinate the gesture did not move is sent back as the stored value.

## Verification and limits

`tst_tempo_automation` (C++) checks tempo recomputation, beat-locked versus
time-locked clips (including a sub-PPQ offset) before/after undo and redo,
preview/cancel, meter and initial-event validation, BBT text, tempo/timeline
save and reopen, the overlap refusal, automation edits/clamping/handles/range
changes before and after undo and JSON round trip, a touch pass recorded
through the mock engine with exact playback values and immutable old
snapshots, write-mode transitions, and a snapshot evaluated on a worker
thread. `tst_tempo_automation_lanes.qml` drives both lanes by pointer and
keyboard. Not covered here: tempo ramps, beat-locked automation or clip
lengths, time-stretching, a real engine and realtime memory reclamation
(WP-060), one host undo stack across the tempo, automation and timeline
histories (each model keeps its own and emits `committed`; only the timeline
has an `externalUndo` replay seam), and native screen-reader delivery
(WP-092).
