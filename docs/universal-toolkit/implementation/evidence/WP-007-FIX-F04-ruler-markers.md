<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 + FND-07 — Ruler marker hit targeting

**Scope:** the existing horizontal `Ruler` marker pointer/touch path. This is
a control-family slice and does not close F04 or certify native accessibility.

The previous hit test accepted pointer positions only within the painted
`markerSize`. In Touch, the marker hit radius now follows
`Theme.size.controlSm / 2` (24 logical pixels) while `Canvas#rulerMarker_<id>`
continues to paint at `Theme.size.rulerMarker`. Hit testing chooses the
nearest marker on the same ruler half; exact-distance ties keep the first
entry in the owner's marker list. A gesture already accepted on a marker
continues to drag that marker even if the pointer crosses another marker's
target. Double-clicking an empty point still emits `tabAdded(position)`.

`tests/qml/tst_ruler_marker_targets.qml` sends a synthetic touch near the edge
of the Touch hit radius and verifies `markerMoved(id, position)`. It also
checks selection when two marker target intervals overlap, stable exact-tie
resolution, unchanged painted marker size, and double-click tab insertion in
an empty region. Existing `tst_controls_structure.qml` continues to exercise
Compact marker dragging and empty-region double-click insertion.

**F04 residual:** marker delegates are Canvas visuals. They have no individual
Accessible role/name/value, focus stop, keyboard movement, or assistive
increase/decrease action. The single `MouseArea#rulerMarkerArea` accepts
pointer/touch gestures only. Adding accessible marker controls requires a
separate per-marker interaction representation that shares the owner's
marker model and keeps Canvas paint geometry separate; this slice records but
does not implement that behavior. Do not treat this as F04 closure.

On Qt 6.11.1, `ctest --test-dir build/wp007-ruler-markers -R '^tst_qml$'`
passes; `all_qmllint` exits successfully with the repository's existing
nonfatal warnings. The preview dump reports Compact
`Ruler#markerRulerReview 8,8 300x22` and
`CanvasItem#rulerMarker_left 76,12 9x9`; Touch reports
`Ruler#markerRulerReview 12,12 300x48` and
`CanvasItem#rulerMarker_left 73,32 14x14`. Thus the authored, density-scaled
marker artwork is unchanged by the larger hit radius. Both captures were
opened and inspected. Dump/grab artifacts are
`build/wp007-ruler-markers-compact.{dump,png}` and
`build/wp007-ruler-markers-touch.{dump,png}` in the ruler-marker worktree.
