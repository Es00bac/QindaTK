<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Media and timeline controls

These are the controls an editing application needs and a form-oriented
toolkit does not have: a picture that may not have arrived, an audio envelope,
the frames of a clip, a time scale, a band of values, and a keyboard shortcut
drawn as keys.

This page is the media-control companion to [controls.md](controls.md), which
covers the general controls and their shared conventions.

## Thumbnail

A media tile that is honest about not having a picture yet.

| Property | Default | Meaning |
|---|---|---|
| `source` | `""` | image url; empty means "none yet", not "failed" |
| `placeholderIcon` | `"image"` | drawn while empty or loading |
| `caption` | `""` | short overlay on the bottom edge; empty draws no band |
| `crop` | `true` | crop fills the tile, fit shows the whole frame |
| `selected` | `false` | accent border at focus-ring width |
| `loadState` | — | read-only: `empty` \| `loading` \| `ready` \| `failed` |

`loadState` is **not** called `state`: `Item.state` already exists and drives
States/Transitions, and shadowing it breaks any caller that declares one.

The tile keeps its size in every state, so a list row does not jump when a
frame arrives. A failure draws a muted alert glyph; an empty tile draws the
kind icon, because an empty tile is not an error and marking it as one trains
the reader to ignore the mark.

objectNames: `thumbnail`, `thumbnailImage`, `thumbnailPlaceholder`,
`thumbnailCaptionBand`, `thumbnailCaption`.

## Waveform

An audio envelope: peak magnitude per column, mirrored about a centre line.

| Property | Default | Meaning |
|---|---|---|
| `peaks` | `[]` | magnitudes in 0..1, one per column, already reduced |
| `color` | `Theme.color.info` | envelope ink |
| `showCentreLine` | `true` | keeps a silent passage reading as audio |
| `empty` | — | read-only |

**The scale is fixed, not per-clip.** A quiet clip renders quiet. Rescaling
each clip to its own peak would make every clip look equally loud, which is
exactly the judgement an editor reads a waveform to make.

`peaks` must already be summarised at the drawn width. Handing it a million
samples to reduce on every repaint is the mistake its contract exists to
prevent.

A single peak draws a full-width plateau at that magnitude. Empty peaks draw
only the centre line; multiple peaks retain their per-column mirrored shape.

This draws its own shape rather than wrapping `Graph`: `GraphMesh::baseline()`
returns the plot edge, so two mirrored series fill to the top and bottom
instead of meeting at zero. A fill-to-zero mode on `Graph` would be the better
toolkit answer but changes two rendering paths existing consumers depend on.

objectNames: `waveform`, `waveformShape`, `waveformCentreLine`.

## Filmstrip

The frames of a clip, in order — most usefully its first and its last.

| Property | Default | Meaning |
|---|---|---|
| `frames` | `[]` | list of `{source, caption}` in display order |
| `crop` | `true` | passed to each frame |
| `minimumFrameWidth` | `size.thumbnail / 2` | below this, fewer frames are drawn |
| `shown` | — | read-only: how many fit |

**The tail frame is always the last cell.** As the strip narrows it drops
middle frames, never the final one, because a cut is two frames — what the
outgoing clip ends on and what the incoming one starts on — and a strip that
dropped the tail would hide half of every cut.

objectNames: `filmstrip`, `filmstripRow`, `filmstripFrame<n>`.

## TimeRuler

A time scale whose tick density follows the zoom.

| Property | Default | Meaning |
|---|---|---|
| `pixelsPerUnit` | `24` | caller's zoom |
| `originUnits` | `0` | caller's scroll position |
| `playheadUnits` | `-1` | negative hides it |
| `formatUnit` | seconds | `function(units) -> string` |
| `scrubbed(units)` | — | signal, in units — never pixels |

When enabled, the ruler is tab-focusable, has an accessible slider role, and
describes its value and visible range. It also exposes read-only `value`,
`minimumValue`, `maximumValue`, and `stepSize` properties to QML callers.
Native screen-reader value-interface behavior is not yet qualified. Left and
Right scrub by one unit; Home and End scrub to
the visible range limits. Assistive increase/decrease actions use the same
one-unit step. Pointer, keyboard, and assistive scrubs all clamp to
`originUnits..originUnits + width / pixelsPerUnit` and emit `scrubbed(units)`;
the caller remains responsible for updating `playheadUnits`. External changes
to the playhead or visible range update the read-only value and accessible
description without emitting `scrubbed`. With no visible playhead, `value` is
the visible range start and the description says no playhead is set. Disabled
rulers ignore every scrub input.

The step is chosen from a 1/2/5/10 ladder against the width a label needs, so
the same ruler is readable at 4 and at 120 pixels per unit. It draws the
caller's scroll state and never owns it: a ruler keeping its own scroll
position drifts from the track area beneath it, and the drift is invisible
until someone measures a cut against it.

objectNames: `timeRuler`, `timeRulerTicks`, `timeRulerPlayhead`.

## RangeSlider

Two handles on one track, for a band rather than a point. Matches `Slider`'s
metrics so a range and a value sit on the same inspector grid. Its full
control height follows `size.sliderTarget` (16px base) or `size.sliderTargetSm`
(14px small); both scale with density and reach at least 48px in Touch, while
the track and handles stay compact.

| Property | Default | Meaning |
|---|---|---|
| `showValues` | `false` | mono readout, `first–second` |
| `decimals`, `suffix`, `small` | | as `Slider` |
| `rangeModified(first, second)` | — | user moves from either handle |

The range is one tab stop. Left/Right changes the upper value; Shift+Left/
Shift+Right changes the lower value. Home/End collapses or extends the selected
end, and assistive increase/decrease actions step the upper value. If `stepSize`
is zero, keyboard and assistive steps use one hundredth of the value span,
including spans smaller than one. The accessible
name uses `tooltip`, falling back to “Range slider”; its description reports
both current values. Programmatic value assignments do not emit
`rangeModified`; disabled ranges ignore keyboard and assistive actions.

Only the band between the handles is accented: a range control shows what is
included, not where it starts.

objectNames: `rangeSlider`, `rangeSliderTrack`, `rangeSliderBand`,
`rangeSliderFirstHandle`, `rangeSliderSecondHandle`, `rangeSliderFirstFocus`,
`rangeSliderSecondFocus`, `rangeSliderValue`.

## KeyCap

A keyboard shortcut drawn as keys rather than written as a string.

| Property | Default | Meaning |
|---|---|---|
| `sequence` | `""` | portable Qt key string, `"Ctrl+Shift+K"` |
| `symbolic` | `false` | ⌃ ⇧ ⌥ ⌘ instead of words |
| `keys` | — | read-only, split and normalised |

"Ctrl+Shift+K" in running text is read as prose and skimmed past; the same
shortcut as three caps is read as keys. An empty sequence draws nothing rather
than an empty box.

objectNames: `keyCap`, `keyCapRow`, `keyCapKey`, `keyCapLabel`.

## Theme roles added

`size.thumbnail` 72, `size.thumbnailHeight` 40, `size.waveformHeight` 28,
`size.timeRuler` 22.
