<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-083 — CAD sketch slice

**State: implemented slice, not accepted.** This lane starts at integration
`ef14e42` in `QindaTK-wp083-cad-core` and leaves the tracker and integration
tree unchanged. It implements the separate CAD model and 2D viewport described
in [cad.md](../../../cad.md).

## Delivered

- Stable typed point, line-segment, circle and partial-arc identities. Segment endpoints
  and circle centers reference stable points. Coordinates, radii and
  constraints are canonical millimeters; input/display can use millimeters
  or inches without changing physical geometry.
- Double-precision measurements, nearest-point/grid snapping, and
  rejection of zero-length segments, zero-radius circles, missing references
  and out-of-range coordinates.
- Coincident, horizontal, vertical and point-distance constraints on a small
  projection solver. An edit is committed only after convergence and geometry
  validation; the diagnostic names an unsatisfied kind, ID and residual.
- Undo/redo, cascading entity deletion and validated atomic `qindatk.cad/1`
  save/reopen. Failed edits and loads leave the live sketch unchanged.
- `Tk.CadViewport` with select, point, segment, circle and arc tools; point drag,
  Delete, Escape, point/grid snapping, middle pan, wheel zoom and displayed
  segment measurements. A 120 × 80 mm four-bore mounting plate is the
  reference example; the gallery shows the reusable viewport.
- Follow-on 2D output slice: stable partial arcs with measured radius/sweep/
  length and native reopen; an absolute segment-angle constraint, a finite
  segment/circle tangency constraint, and typed `setDimension` edits for
  distance and angle constraints. Selected constrained segments offer an
  inline numeric dimension editor. SVG and ASCII DXF export actual geometry
  in canonical millimeters with documented Y-axis conversion.

## Focused evidence

```
cmake -S . -B build/wp083-cad -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQINDATK_BUILD_EXAMPLES=ON -DQINDATK_BUILD_TOOLS=ON -DBUILD_TESTING=ON -DQT_QML_GENERATE_QMLLS_INI=ON
cmake --build build/wp083-cad --target qtk-preview tst_cad_document tst_qml -- -j24 -l24
ctest --test-dir build/wp083-cad -R '^(tst_cad_document|example_cad-sketch|example_gallery)$' --output-on-failure
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_IMPORT_PATH=build/wp083-cad/qml build/wp083-cad/tests/tst_qml -input tests/qml/tst_cad_viewport.qml
```

Qt 6.11.1: focused CTest passed **3/3**; the viewport QML runner passed
**3/3**. The C++ reference sketch checks 100 × 60 mm geometry and a
coincident-point relation after projection, point and grid snapping, unit
conversion, conflict rejection
without mutation, degenerate inputs, constrained dragging, Undo/Redo and
atomic save/reopen. The QML case creates/selects geometry, drags a point,
and verifies unit conversion preserves screen geometry. `gen_catalog.py`
emitted 419 types.

The [inspected screenshot](WP-083-cad.png) shows the 120 × 80 mm mounting
plate, four 12 mm bores, measurements, grid and creation toolbar under the
software renderer. `qtk-preview --dump` reports
`CadViewport#referenceCadViewport 12,67 1156x618` and four visible segment
measurement labels. No visual overlap or clipping is evident at 1180×720.

The follow-on `arcsDimensionsTangencyAndExports` case checks a 90-degree arc
length and native save/reopen, a segment-angle relation, finite segment/circle
tangency, radius and distance edits, and atomic rejection of conflicting
angles. It compares complete SVG and DXF files against an exact, ID-free
known-geometry fixture: one 10 mm line, one 2 mm radius circle, one 3 mm
radius quarter arc, with a millimeter SVG viewBox and `$INSUNITS=4` DXF header.
The viewport QML case creates an arc through three taps, selects a constrained
segment and changes its dimension through the visible `NumberField`.

The [arc screenshot](WP-083-cad-arc.png) shows the new central partial arc and
Arc tool. The [selected-dimension screenshot](WP-083-cad-dimension-final.png)
shows the inline 120.000 mm editor on the highlighted top segment, with the
same unclipped plate geometry at 1180×720.

## Remaining WP-083 scope

Spline/polygon entities, relative angles and curve tangency, multi-selection,
DXF/SVG import, large-sketch solver qualification and 3D
solid/topology work remain. The diagram slice has its own remaining routing,
flow conservation and true Sankey placement gaps. This first CAD slice does
not satisfy the package acceptance gate by itself.

The shared integration tree built the CAD, map and QML targets, then passed
`tst_cad_document`, `tst_geographic_map` and `tst_geographic_tiles` **3/3**;
the focused CAD QML case passed **3/3**. Sol opened the merged CAD capture.
The follow-on CAD output and constraint slice remains in progress.
