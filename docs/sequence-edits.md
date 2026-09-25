<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Sequence edits: trims, links, transitions and multicam

WP-071 turns `TrimView`, `TransitionEditor` and `MulticamView` into real
editing tools. Every edit is a `TimelineModel` transaction
([edit-timeline.md](edit-timeline.md)): one typed request, previews that never
touch committed records, and one undoable commit. Pictures and sound come from
the WP-070 media stack ([media-playback.md](media-playback.md)): the same
`MediaProgram` renders the monitor and writes the export, so a previewed edit
and an exported file show the same frames. Decisions D-118 to D-121.

## Roles

| Role | Meaning |
| --- | --- |
| Source time | Ticks into a clip's media: `sourceOffset` is the in point; `sourceDuration` is how much media exists (−1 unknown). |
| Program time | Timeline ticks; frame *f* starts at `ceil(f × timebase / frameRate)` from the TimeMap origin. |
| Edit point (cut) | The tick where an outgoing clip ends and an incoming clip starts on one track. |
| Head handle | Media before a clip's in point: `sourceOffset`. |
| Tail handle | Media after a clip's out point: `sourceDuration − sourceOffset − duration` (unbounded when the length is unknown or `allowBeyondSource`). |
| Link group | Clips sharing a `linkId` (a picture and its sound). |

## Edit kinds

| `kind` | Targets | Effect | Bounds |
| --- | --- | --- | --- |
| `roll` | `clipIds` = [outgoing, incoming], adjacent on one track | The cut moves by the delta: outgoing `duration += d`; incoming `start += d`, `duration −= d`, `sourceOffset += d`. Nothing else moves. | Outgoing tail handle, incoming head handle, minimum length, zero. |
| `trimStart`/`trimEnd` with `ripple` | one side of a cut | Ripple trim: the in or out point moves and later clips close up or make room (WP-048 semantics). | Handles, minimum length. |
| `slip` | clips | `sourceOffset += d`; placement and length stay. | Head and tail handles. |
| `slide` | clips | The clip moves; the clip ending at its start lengthens or shortens, the clip starting at its end rolls (a roll on both sides). Length of the sequence stays. | Left neighbour's tail handle, right neighbour's head handle and minimum length; with no neighbour, the gap on that side. |
| `setTransition` | one incoming clip | Sets or removes (`transition` absent or empty `type`) the transition into it. | A clip must end at its start; handles (below). |
| `switchSource` | clips playing at `pointerTicks` | Cuts each at that time; the part from there on shows `resourceId` from source position `sourceOffset` with `sourceDuration` available; linked partners take `linkedResourceId` from `linkedSourceOffset` with `linkedSourceDuration` (each falls back to the picture's value). At a clip's start the clip switches whole. | The new source must cover the whole switched part. |
| `link`, `unlink` | clips | Give the clips a new common `linkId`, or clear it. | — |

Deltas are `pointerTicks − grabTicks`. The TrimView's ripple mode maps side
`left` to `trimEnd` of the outgoing clip and `right` to `trimStart` of the
incoming clip; slip and slide act on the chosen side's clip.

**Frame-exact (D-118).** With `frameExact` the moved edit point (the cut for
a roll, the clip start for slip and slide, the trimmed edge for a trim, the
switch time) lands on the program frame start nearest the request inside the
legal range; when no frame start is in range the edit does not move. Bounds
come from the clip records, never from decoded media, so offline media edits
exactly like online media and keeps its exact source offsets; the views show
it as offline.

**Linked (D-119).** With `linked` the selection grows by every clip that
shares a link group with a selected clip, and all of them take the same edit:
a roll's partners of the outgoing clip move their ends and partners of the
incoming clip their starts; a move changes tracks only for the selected clips
(their partners keep their tracks); a locked partner refuses the edit. Split,
paste and source switches give the new parts a new group derived from the
operation (UUIDv5), so the left and right parts of a linked pair stay paired
with each other.

## Transitions

A transition belongs to its **incoming** clip (`TimelineClip::transition`,
D-120): `{type, beforeCut, afterCut, alignment, parameters}`. Its window is
`[cut − beforeCut, cut + afterCut)` in program ticks, fixed relative to the
cut, so a roll moves it with the cut. The incoming clip lends `beforeCut`
ticks of head handle and the outgoing clip `afterCut` ticks of tail handle.

| Request field | Placement |
| --- | --- |
| length = `beforeCut + afterCut` | The requested length; `frameExact` rounds it to whole frames (at least one). |
| `alignment: "start"` | All after the cut. |
| `alignment: "end"` | All before the cut. |
| `alignment: "center"` | Split around the cut; an odd frame (or tick) goes before the cut. |

Types and parameters (values are strings):

| Type | Picture | Parameters |
| --- | --- | --- |
| `dissolve` | Channel mix by the weight. | `curve` (sound) |
| `wipe` | The incoming picture covers `round(extent × w / 65536)` columns or rows. | `direction`: `left` (default), `right`, `up`, `down`; `curve` |
| `dip` | Outgoing mixes to the colour by 2w, then the colour to the incoming picture. | `color` (default `#000000`); `curve` |

`curve` is `linear` (default) or `equalPower`. The same window on an audio
clip is a crossfade.

**Legality (AGENT-GUARD in `timeline_planner_transitions.cpp`).** Setting a
transition needs an outgoing clip ending exactly at the incoming clip's start,
enough head and tail handle, and a window inside both clips. A clip's two
windows share its length: its own lead-in (`afterCut` of its transition) plus
the lead-out into the next clip (`beforeCut` of that clip's transition) must
not exceed its duration, so a 10-frame clip cannot take an 8-frame
start-aligned transition in and an 8-frame end-aligned one out (`Validation`,
field `duration`). Every other edit
is checked after planning: if the cut no longer exists (the outgoing clip was
deleted or moved away), the transition is dropped in the same change set, so
undo restores it; if the cut still exists but the edit leaves too little
handle or clip around it, the edit is refused (`Validation`, field
`handles`), shown as an invalid preview; an edit that shortens a clip until
its two windows overlap is refused the same way (field `duration`). Split parts and pasted clips carry no
transition.

### Evaluation

`MediaProgram::fromTimeline` extends the outgoing span to the window end and
pulls the incoming span back to the window start (its source offset moves
back by `beforeCut`), and marks both with the window. A timeline loaded from
elsewhere with overlapping windows on one clip keeps the later cut, so the
earlier dissolve still shows both of its clips. For a program frame
`[start, end)` in the window the incoming weight is taken at the frame's
midpoint and rounded once:

```
w = round(65536 × (2 × (start − windowStart) + (end − start)) / (2 × duration))
out = (a × (65536 − w) + b × w + 32768) >> 16      per premultiplied channel
```

so a 4-frame dissolve uses 1/8, 3/8, 5/8 and 7/8 and never repeats a source
picture. Each side is drawn letterboxed into its own layer, blended by
`MediaTransition::blend`, and the result is composited in the track's place.
Sound gains are per program sample `s` of the window `[first, first + count)`:
`g = (s − first + 0.5) / count` for the incoming side and `1 − g` for the
outgoing side (`sin`/`cos` of `g × π/2` for `equalPower`). Playback,
`MediaExport` and queued exports all call this one evaluator, and the program
JSON used by the render queue carries the windows.

## SequenceEditor

`Tk.SequenceEditor` (C++) is the backend of `TrimView` and
`TransitionEditor`. Properties: `timeline`, optional `resolver` (offline
state; without one a resource ID is a local path), optional `player`
(transition preview), and the policies `linked` and `frameExact` (both on by
default).

- `selectEditPoint(outgoing, incoming)` or `selectCut(trackId, ticks)` picks a
  cut; `trimEdit` is the TrimView snapshot, built from the preview overlay so
  the filmstrips show the frames the preview would produce: clip and resource
  IDs, `cutTicks`, first/last source ticks each side, `leftTailHandleTicks`,
  `rightHeadHandleTicks`, `legalHead`/`legalTail`, `missingLeft`/`missingRight`,
  `leftLinked`/`rightLinked`, `previewing` and the frame rate.
- `previewTrim(mode, side, deltaTicks)` begins or updates one session per
  (mode, side, policy); `trimStatus` is `idle`, `ready`, `rejected` or
  `committed`, with `trimError` and the frame-exact `trimAppliedDelta`.
  `commitTrim()` makes it one history step; `cancelTrim()` leaves nothing. A
  reload or competing commit retires the session (`rejected`).
- `transitionClipId` selects an incoming clip; `transition` is the
  TransitionEditor descriptor (`hasTransition`, type, duration, before/after,
  alignment, head/tail handle ticks, `missingMedia`, window, parameter
  descriptors). `editTransition("type" | "durationTicks" | "alignment" |
  "parameter:<id>", value)` applies one `setTransition` transaction (a new
  transition starts as a one-second centred dissolve); `setTransition`,
  `removeTransition` and `transitionWindow` are also available.
  `previewTransition()` loops the bound player over the window and plays it;
  `stopTransitionPreview()` ends that.
- `linkClips(ids)` and `unlinkClips(ids)`.

## Multicam

`Tk.MulticamModel` (C++, D-121) is the backend of `MulticamView`. Angles are
`{angleId, label, resourceId, audioResourceId, syncOffsetTicks,
sourceDurationTicks, audioSyncOffsetTicks, audioSourceDurationTicks}`: at
program tick T, angle *a* shows source ticks
`(T − syncStartTicks) + a.syncOffsetTicks`. When the sound is a separate
recording (`audioResourceId`), it plays from
`(T − syncStartTicks) + a.audioSyncOffsetTicks` of that file and is bounded by
`audioSourceDurationTicks` (defaults: the picture's offset, unknown length). The program is ordinary clips on
`trackId` (with linked sound on `audioTrackId` when set):

- `insertProgram(angleId, start, duration)` places a synchronized program clip
  (and its linked sound);
- `switchAngle(angleId, atTicks)` (the playhead when empty) is one frame-exact
  `switchSource` transaction whose source position comes from the sync map,
  never from the clip being cut, so switching cannot drift even after a slip.
  With `audioFollowsVideo` the linked sound is cut and switched too (to
  `audioResourceId` at its own sync position and length, or the picture's
  resource at the picture's position); without it the sound stays;
- `playheadTicks` follows `player.presentedTicks` when a player is bound, so a
  switch during playback cuts at the frame on screen;
- `angles` (for the view: `frameTimeTicks` is each angle's source time at the
  playhead, `available` needs media there and online), `activeAngleId`,
  `angleAt(ticks)`, `sourceTicksAt`, `unsyncedClipIds()` (program clips and,
  when `audioTrackId` is set, angle sound clips away from their sync position);
- `contentMap()`/`loadContent()` (`qindatk.multicam/1`) save the angle table;
  with the timeline JSON they reopen the same cuts, and the program angle is
  recovered from each clip's resource.

## Views

- `TrimView.editor`: the snapshot, preview, commit and cancel go through the
  editor; a `side` selector appears for ripple, slip and slide. A move needs
  source handle only in its own direction; offline media is shown but does
  not block.
- `TransitionEditor.editor`: fields show the committed, frame-exact values;
  type, duration, alignment and parameters are transactions; Preview plays
  the window; Remove clears the transition.
- `MulticamView.multicam`: angles, playhead and program angle come from the
  model, and a click switches the program at once (`switchAcknowledged`
  reports the result).

In host mode (no backend bound) all three keep their WP-071 request
contracts ([controls.md](controls.md)).

## Tests

`tst_sequence_edits` (planner, no media), `tst_sequence_editor` (editor and
multicam backends), `tst_sequence_render` (rendered and exported frames and
sound; QSKIPs without FFmpeg) and QML `tst_video_edit_backend.qml`.

## Limits

- Transitions join two clips on one track. VID-05's purpose also names
  transitions between composition states; no package provides them yet and
  the plan assigns VID-05 only to WP-071
  ([evidence](universal-toolkit/implementation/evidence/WP-071.md)).
- A multicam group is a table of angles over one program track; nested
  multicam clips and automatic sync by audio waveform are not provided (the
  host supplies sync offsets).
- Performance under load, other platforms, screen-reader runs and
  output-fidelity comparison are qualified in WP-088 to WP-094.
