<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Takes, punch recording and comping

WP-065: loop passes become takes, punch recording keeps whole takes, and a
comp selects ranges of takes without touching their audio. The comp lives in
`session.comps` (`AudioTakeCompModel`); a timeline clip places it; the engine
graph plays it through the same `render()` as every clip
([audio-engine.md](audio-engine.md)). `TakeCompView` shows and edits it.
Decisions [D-174](decisions.md#d-174--a-comp-is-a-selection-over-take-ranges-placed-by-a-timeline-clip)
to [D-177](decisions.md#d-177--loop-passes-are-cut-from-record-jumps-and-punch-selects-inside-whole-takes).

```
 engine: capture ring ─▶ take WAV     jump ring (wraps, seeks) ─▶ recordingFinished{jumps, punch…}
 AudioSession.placeTake ─▶ passes ─▶ comps (AudioTakeCompModel)   timeline clip (resourceId = comp id)
                                        │ playback(compId) = committed or preview, or audition
 AudioRenderGraph::compile ─ comp clip ─▶ one graph clip per piece (take media, range limit, fades)
 TakeCompView ◀─ comps.viewState(compId) ─ swipe/keys ─▶ comps.preview / commitPreview / cancelPreview
```

## Coordinates (D-174)

| Thing | Unit |
| --- | --- |
| Comp sample | a frame at the comp's `sampleRate`, from the comp origin |
| Comp origin | the comp clip's start minus its `sourceOffset` (a comp clip trims a comp exactly as a media clip trims a file) |
| Take | a resource and its usable frames `[sourceStart, sourceEnd)`; `sourceOffset` is the source frame at comp sample 0 (negative when the take starts after the origin) |
| Take coverage | comp samples `[sourceStart − sourceOffset, sourceEnd − sourceOffset)` |
| Segment | `[start, end)` of the comp playing one take; sorted, disjoint, inside `[0, length)` and inside its take's coverage; a gap is silence |

Every take of a comp is at the comp's rate (recorded passes are). A device
at another rate converts comp positions with the timeline's exact rounding
(NearestAway), like clip edges. IDs are stable strings: `comp:<uuid>`,
`take:<uuid>`, `segment:<uuid>`. Positions cross QML as exact decimal
strings.

## Edits (D-176)

| Call | Result |
| --- | --- |
| `select(comp, take, start, end)` | `[start, end)` plays the take; overlapped segments are trimmed or cut in two (the left part keeps its ID); an exactly replaced segment keeps its ID. Refused outside the comp or the take's coverage. |
| `split(comp, at)` | the segment containing `at` becomes two (same take) |
| `merge(comp, segment)` | the segment and the one touching it on the right become one, playing the left one's take (refused when that take does not cover the result) |
| `clear(comp, start, end)` | removes the range (silence) |
| `setFade(comp, samples, shape)` | crossfade length (0 … one second) and shape `linear` or `equalPower` |
| `preview(comp, take, start, end)` | provisional select from the committed comp; heard and shown until `commitPreview()` (one step) or `cancelPreview()` |
| `setAudition(comp, take)` / `compare(comp, take, reference)` / `toggleComparison(comp)` | play one take alone (or A/B two) over the comp; not saved, not undone |
| `undo()` / `redo()` | per-owner history (128 steps), each step the whole comp before and after |

Every edit answers `{ok, error, changed}`; a refused edit changes nothing
and an edit that changes nothing adds no step. Any edit, undo or redo
cancels a running preview first.

## Crossfades (D-175)

Where two different takes meet at `b`, they crossfade over `[b − x, b + y)`:
`x` samples inside the left segment, `y` inside the right one. The fade is
centred (`x = ⌊F/2⌋`) unless a side lacks material:

- `x` ≤ what the right take has before `b`, and ≤ half the left segment;
- `y` ≤ what the left take has after `b`, and ≤ half the right segment;
- whatever one side cannot take moves to the other, so a pass that starts
  exactly at the junction still gets a full-length fade after it.

A sample `s` in a fade `[a, c)` has `t = (s − a + 0.5) / (c − a)`: linear
weights `t` in and `1 − t` out, equal power `sin(t·π/2)` and `cos(t·π/2)`.
A junction inside one take, a gap and the comp edges are cut exactly (a
split never changes a sample). New comps use 5 ms (`rate / 200`) equal
power. `comps.pieces(comp)` lists what plays: each segment with its play
range and fade regions.

## Rendering

`AudioRenderGraph::compile` turns a comp clip into one ordinary graph clip
per piece: the take's media (the edited buffer at the graph rate, WP-063),
the frame limit `[sourceStart, sourceEnd)` and the fade regions, clipped to
the timeline clip. `render()` weights faded samples per sample
(`AudioComp::fadeWeight`: no tables, no allocation), so the device and
`renderToFile` play identical samples. Missing take media plays silence and
is listed by `session.missingMedia`; `relinkMedia` restores it.

## Recording, passes and punch (D-177)

The device thread pushes a *record jump* into its own 1024-entry ring each
time the transport jumps while a take is open (a loop wrap or a seek): the
file frame where the new position starts and that position. The finished
take (`recordingFinished`) carries `jumps`, `jumpsDropped`, the loop and the
punch range. Frame `f` of the file holds what was heard at the transport
sample of frame `f − latency`, so pass *n* starts at file frame
`jumpₙ + latency` and is heard from the jump's position; pass 1 is heard
from `recordedSample − latency`.

`AudioSession.placeTake(take)` places a take on the armed track:

| Recording | Result |
| --- | --- |
| No loop wrap, no punch (or `passTakes` false, dropped frames, lost jumps) | one media clip, as WP-060 |
| Loop wraps | one take per pass; a comp over a comp clip already there (one comp step) or a new comp clip (the timeline step); later passes win |
| Punch `[in, out)` over a comp clip | the new takes are selected only inside the punch range (one comp step) |
| Punch over a media clip | the clip becomes a comp (one timeline step, a source switch at its start) whose first take is the clip's source and whose punch range plays the new take |
| Punch over nothing | a new comp clip that plays only the punch range |

Punch is a transport state (`requestPunch`, `canPunch` true): the device
records the whole pass, so pre- and post-roll stay in the take and the punch
can be widened later by selecting more of it.

## Persistence

`comps` is part `comps` of `qindatk.audio-session/1` (after `midi`):
`{comps: [{compId, sampleRate, length, fadeSamples, fadeShape, takes:
[{takeId, resourceId, label, sourceOffset, sourceStart, sourceEnd}],
segments: [{segmentId, takeId, start, end}]}]}` with decimal-string
numbers. Opening validates every comp (all or nothing, with the other
parts); history, previews and audition are not saved. A comp stays in the
model when timeline undo removes its clip, so redo finds it again.

## QML

```qml
Tk.TakeCompView {
    readonly property var snapshot: session.comps.revision >= 0 ? session.comps.viewState(compId, 256, 64) : ({})
    takes: snapshot.takes; compSegments: snapshot.compSegments; sampleBoundaries: snapshot.sampleBoundaries
    selectionStartSample: host.start; selectionEndSample: host.end
    onSelectionRequested: (start, end) => { host.start = start; host.end = end }
    onCompProposalPreviewRequested: (id, take, start, end) => session.comps.preview(compId, take, start, end)
    onCompProposalPreviewCanceled: id => session.comps.cancelPreview()
    onCompProposalRequested: (id, segment, take, start, end) => {
        if (session.comps.previewCompId !== compId) session.comps.preview(compId, take, start, end)
        proposal = { proposalId: id, status: "pending" }
    }
    onProposalDecisionRequested: (id, accept) => {
        if (accept) session.comps.commitPreview(); else session.comps.cancelPreview()
        proposal = null
    }
    onAuditionRequested: take => session.comps.setAudition(compId, take)
    onCompareRequested: (take, reference) => session.comps.compare(compId, take, reference)
}
```

`viewState` returns `{takes, compSegments, sampleBoundaries, …}`. The
boundary table is `columns` equal cells plus every committed segment and
fade edge, and it stays fixed while a preview runs, because a swipe holds
indices into it. Takes carry `available` and bounded `peaks`. Keys:
Left/Right select a cell, Shift extends, Home/End jump, Up/Down choose the
take, Enter uses it for the selection, Space auditions it, C compares it
with the reference take, Escape cancels a swipe.

## Not covered here

Performance measurement of long comps and many takes, other platforms and
screen-reader runs are WP-088…WP-094. Grouping separately recorded clips
into one comp, time-stretched takes and per-segment gain are not part of the
reference engine; a take at another sample rate cannot join a comp.
