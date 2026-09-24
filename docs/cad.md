<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# CAD sketch editing

`Tk.CadDocument` is a separate model from diagram nodes and ports. It stores
2D point, segment and circle entities with stable IDs, geometric constraints,
undo/redo and a local `qindatk.cad/1` file. `Tk.CadViewport` draws and edits
that model. This is a precise 2D sketch slice, not a solid-modeling kernel.

## Units and geometry

Coordinates, radii and distance constraints are stored as double-precision
millimeters. `units` is `mm` or `in`; `changeUnits()` changes display and input
units without changing physical geometry. API coordinates and measurements
use the current units. A file retains canonical millimeters and the chosen
display unit. Coordinates and radii are bounded to one million millimeters.
Zero-length segments and zero-radius circles are rejected. A saved sketch is
validated before it replaces the current document; a failed load is atomic.

| Operation | Effect |
| --- | --- |
| `addPoint(x,y)` | Add a stable point in current units. |
| `addSegment(startPointId,endPointId)` | Add a segment referencing two distinct points. |
| `addCircle(centerPointId,radius)` | Add a circle referencing a center point. |
| `movePoint(id,x,y)`, `setCircleRadius(id,r)` | Edit geometry in one undo step. A failed constraint solve leaves it unchanged. |
| `addConstraint(kind,firstId,secondId,value)` | Add `coincident` or `distance` between points, or `horizontal`/`vertical` on a segment. Distance uses current units. |
| `removeEntity(id)` | Remove an entity and its dependent geometry/constraints in one step. |
| `measure(id)` | Return point coordinates, segment length/angle, or circle radius/diameter/area in current units. |
| `snap(x,y,tolerance,gridStep,excludeId)` | Prefer an existing point in range, then the display-unit grid, then the input coordinate. |
| `undo`, `redo`, `saveToFile`, `loadFromFile` | Restore bounded local history or write/reopen a validated atomic JSON file. |

`points`, `segments`, `circles` and `constraints` are read-only snapshots.
Segment snapshots include resolved endpoints and measured length; circle
snapshots include resolved center and radius. IDs stay stable through moves,
unit changes and save/reopen. Removing a point removes incident segments,
circles and their constraints. Undo restores the complete snapshot.

## Constraint solving and diagnostics

The small solver projects coincident, horizontal, vertical and distance
relations in canonical millimeters. It applies to a candidate copy and
commits only after residuals fall below 0.000001 mm and every segment and
circle remains nondegenerate. A moved point stays at the requested position
while connected points adjust. The original sketch is preserved when a
constraint set is contradictory; `lastError` includes the unsatisfied kind,
constraint ID and residual in millimeters. Invalid references, out-of-range
values and degenerate entities also produce a diagnostic. This solver is
intended for small sketches; tangency, angles, splines and large assemblies
remain outside its contract.

## Viewport

`CadViewport.document` accepts a `CadDocument`. Its `mode` is `select`,
`point`, `line` or `circle`. Click to select/place a point; two clicks create
a segment or define a circle center/radius. Existing-point and grid snapping
work in each creation mode. Drag a selected point to solve the sketch at the
new location. Delete removes the selected entity; Escape cancels a pending
two-click tool. Middle drag pans and wheel zooms about the pointer. `zoom`
is screen pixels per millimeter, so switching display units does not resize
the sketch. `panX`/`panY`, `selectedEntityId` and `statusText` describe local
view state. Stable objectNames are `cadViewport` and `cadViewportDrawing`.

The [mounting plate example](../examples/cad-sketch/Main.qml) exercises a
120 × 80 mm constrained rectangle and four 12 mm bores. The gallery embeds
the same reusable viewport with a smaller sketch.

## Remaining CAD scope

Arc/spline/polygon entities, tangency and angle constraints, dimension edit
controls, multi-selection, DXF/SVG export, precision qualification on large
models, 3D solids and topology are not part of this first slice. See
[WP-083](universal-toolkit/implementation/phase-07-integrations.md#wp-083).
