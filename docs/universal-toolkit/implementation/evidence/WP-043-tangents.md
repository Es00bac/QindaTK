<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-043 — Curve tangent presentation

`Tk.CurveEditor` accepts optional `inHandle` and `outHandle` fields on its
host-owned knot records. Each field is an absolute normalized `{x, y}` point
in plot coordinates. A present handle draws a themed connection to its knot,
has a stable `curveTangent_<id>_<side>` target and a matching
`curveTangentLine_<id>_<side>` line object. Pointer, arrow-key and accessible
increment requests carry stable knot ID, side and absolute normalized
position. Finishing and cancellation also carry the proposed position;
cancellation reports the captured start point. The editor does not mutate
knot or handle records.

The root owns the complete pointer press so host array replacement does not
destroy an active request. Escape cancels a preview and suppresses the later
release; pressing and releasing without movement selects the owning knot
without submitting an edit. `tests/qml/tst_curve_editor.qml` covers handle hit
and selection, drag across record replacement, original-position cancellation,
keyboard movement and unchanged host records. The focused suite passed **7/7**
on Qt 6.11.1 in the isolated lane and **7/7** on both Qt 6.11.1 and Qt 6.8.3
after integration at `5de3c62`. Both integrated preview checks passed.

The reproducible visual fixture is
[`CurveEditorScene.qml`](../../../../examples/spatial-canvas/CurveEditorScene.qml).
`qtk-preview --check` passed. The dump places the fixture at
`CurveEditor#curveSceneEditor 12,54 876x534`, the incoming shadow target at
`Item#curveTangent_shadow_in 128,457 20x20` with
`Rectangle#curveTangentLine_shadow_in 8,-3 73x1`, and the outgoing midtone
target at `Item#curveTangent_midtone_out 574,175 20x20` with
`Rectangle#curveTangentLine_midtone_out -104,45 125x1`. The rendered
[compact](WP-043-tangent-compact.png) and
[comfortable](WP-043-tangent-comfortable.png) 900×600 captures were opened
and inspected; both show the sample curve, four knots, and all four optional
handles with their connecting lines inside the plot.
