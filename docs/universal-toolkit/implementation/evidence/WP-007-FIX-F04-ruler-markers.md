<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / F04 + FND-07 — Ruler marker interaction

## Scope

This slice covers horizontal ruler marker interaction. It keeps the marker
model owner-controlled and the existing Canvas artwork separate from keyboard,
assistive and pointer hit behavior.

## Touch targeting

In Touch, the marker hit radius follows `Theme.size.controlSm / 2` (24 logical
pixels) while `Canvas#rulerMarker_<id>` continues to paint at
`Theme.size.rulerMarker`. Hit testing chooses the nearest marker on the same
ruler half; exact-distance ties keep the first entry in the owner's marker
list. A gesture already accepted on a marker continues to drag that marker
even if the pointer crosses another marker's target. Double-clicking an empty
point still emits `tabAdded(position)`.

## Keyboard and accessibility

Draggable horizontal markers are individual accessible Slider items with
stable `rulerMarker_<id>` object and accessibility ids, localized names, focus
state and a description of their current pixel position. A themed outline
appears while a marker has focus. Left/Right and accessible increase/decrease
adjust by `Tk.Theme.space.unit` pixels, clamp to the ruler bounds and use the
same `markerMoved(id, position)` operation as pointer dragging. Focus follows
the marker when the owner replaces its marker array in response to the signal.

The distinct `rulerTabInsertCommand` Slider is the keyboard and accessible
tab insertion path. It starts at the ruler midpoint, exposes its current pixel
position, and adjusts in `space.unit` steps with Left/Right or accessible
increase/decrease. Enter and accessible press each make one `tabAdded(position)`
request. Its themed focused indicator marks the requested location. The app
still owns `markers` and applies that request; owner replacement does not
generate another insertion signal. Pointer double-click on an empty point in
the bottom half remains available.

Markers with `draggable: false` are exposed as static text and reject keyboard,
assistive and pointer edits. Disabling the ruler blocks user changes while
owner assignment to `markers` remains silent. Delete removes a focused
draggable tab through the existing `tabRemoved(id)` signal; other marker kinds
are not deleted.

## Verification

- `portageq envvar MAKEOPTS` returned `-j24 -l24`; direct Ninja builds used
  those same live job and load values.
- Full CTest passed 19/19 on Qt 6.11.1 and 6.8.3. The focused QML suite was
  rerun after the final marker changes on both versions and passed.
- `all_qmllint` completed successfully on both versions. Qt 6.8 emits existing
  static type-map warnings for ThemeMetrics and the optional QindaQt token
  bridge; these do not fail the lint target.
- The Compact preview dump reported `Ruler 40,70 500x22`,
  `CanvasItem#rulerMarker_left 146,12 9x9`, and a visible `Rectangle -2,-2
  13x13` focus outline. The Touch dump reported `Ruler 40,70 500x48`,
  `CanvasItem#rulerMarker_left 143,32 14x14`, and a visible `Rectangle -3,-3
  20x20` outline. Compact and Touch `--grab` captures were opened and reviewed;
  marker artwork keeps its density-sized geometry and the outline is visible.
- `tst_ruler_marker_accessibility.qml` covers keyboard and assistive changes,
  focus restoration after owner replacement, accessible name/role/position,
  both bounds, read-only/disabled guards, silent owner updates and Delete for
  tabs. It also covers Tab focus to the insertion command, keyboard and
  accessible position changes, Enter/press insertion, one signal per command,
  owner marker assignment and the disabled gate. `tst_ruler_marker_targets.qml` retains synthetic Touch edge coverage,
  nearest-target and exact-tie resolution, unchanged artwork geometry, and
  empty-region double-click insertion. `tst_controls_structure.qml` covers
  Compact marker dragging and insertion.
- After insertion support was added, the full QML suite passed 299/299 on both
  Qt 6.11.1 and Qt 6.8.3. The focused insertion test passed on both versions;
  `all_qmllint` completed successfully (Qt 6.8 reports the existing static
  type-map warnings).
- The Compact focused preview dump reported `Ruler#previewRuler 40,30 420x22`,
  `Item#rulerTabInsertCommand 200,1 20x20`,
  `Rectangle#rulerTabInsertIndicator -2,-2 24x24`, and the accent position
  line `Rectangle 9,1 2x18`. The grab at `/tmp/ruler-tab-insert-preview.png`
  was opened and reviewed; the focused location is visible without changing
  the ruler's 420x22 geometry.

## Limits

Native screen-reader announcements and action names still need testing on
supported desktop accessibility backends. Marker keyboard movement is in
pixel coordinates; applications continue to own snapping and marker storage.
