<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# CAD sketch editing

`Tk.CadDocument` is a separate model from diagram nodes and ports. It stores
2D point, segment, circle and arc entities with stable IDs, geometric constraints,
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
| `addCircle(centerPointId,radius)`, `addArc(centerPointId,radius,start,sweep)` | Add a circle or partial arc referencing a center point. Arc angles are degrees clockwise from +X in the viewport's Y-down coordinates. Sweep is strictly between 0 and 360 degrees. |
| `movePoint(id,x,y)`, `setCircleRadius(id,r)`, `setArc(id,r,start,sweep)` | Edit geometry in one undo step. A failed constraint solve leaves it unchanged. |
| `addConstraint(kind,firstId,secondId,value)` | Add `coincident` or `distance` between points, `horizontal`, `vertical` or absolute `angle` on a segment, or `tangent` between a segment and circle. Distance uses current units; angle uses degrees. |
| `setDimension(constraintId,value)` | Edit a distance or angle constraint by stable ID and solve atomically. |
| `removeEntity(id)` | Remove an entity and its dependent geometry/constraints in one step. |
| `measure(id)` | Return point coordinates, segment length/angle, circle radius/diameter/area, or arc radius/sweep/length in current units. |
| `snap(x,y,tolerance,gridStep,excludeId)` | Prefer an existing point in range, then the display-unit grid, then the input coordinate. |
| `undo`, `redo`, `saveToFile`, `loadFromFile` | Restore bounded local history or write/reopen a validated atomic JSON file. |
| `exportSvg(path)`, `exportDxf(path)` | Export native millimeter geometry to atomic SVG and basic ASCII DXF files. |

`points`, `segments`, `circles`, `arcs` and `constraints` are read-only snapshots.
Segment snapshots include resolved endpoints and measured length; circle
snapshots include resolved center and radius. IDs stay stable through moves,
unit changes and save/reopen. Removing a point removes incident segments,
circles, arcs and their constraints. Undo restores the complete snapshot.

## Constraint solving and diagnostics

The small solver projects coincident, horizontal, vertical, distance,
absolute segment-angle and finite-segment/circle tangency
relations in canonical millimeters. It applies to a candidate copy and
commits only after length residuals fall below 0.000001 mm, angle residuals
fall below 0.000001 degree, and every segment, circle and arc remains
nondegenerate. A moved point stays at the requested position
while connected points adjust. The original sketch is preserved when a
constraint set is contradictory; `lastError` includes the unsatisfied kind,
constraint ID and residual with its units. Tangency uses the finite segment:
the contact projection must lie between its endpoints. Invalid references, out-of-range
values and degenerate entities also produce a diagnostic. This solver is
intended for small sketches; curve tangency, relative angles, splines and large assemblies
remain outside its contract.

## Viewport

`CadViewport.document` accepts a `CadDocument`. Its `mode` is `select`,
`point`, `line`, `circle` or `arc`. Click to select/place a point; two clicks create
a segment or define a circle center/radius. Three clicks define an arc center,
start and end. Existing-point and grid snapping
work in each creation mode. Drag a selected point to solve the sketch at the
new location. Delete removes the selected entity; Escape cancels a pending
two-click tool. Middle drag pans and wheel zooms about the pointer. `zoom`
is screen pixels per millimeter, so switching display units does not resize
the sketch. `panX`/`panY`, `selectedEntityId` and `statusText` describe local
view state. Selecting a constrained segment replaces its length label with a
`Tk.NumberField` that calls `setDimension` on commit. Stable objectNames are
`cadViewport`, `cadViewportDrawing` and `cadDimensionEditor_<entityId>`.

The [mounting plate example](../examples/cad-sketch/Main.qml) exercises a
120 × 80 mm constrained rectangle and four 12 mm bores. The gallery embeds
the same reusable viewport with a smaller sketch.

## Output contracts

SVG uses `mm` physical width/height, a millimeter `viewBox`, native Y-down
coordinates, line/circle/arc paths and point markers. Basic ASCII DXF uses
`$INSUNITS=4` (millimeters) with POINT, LINE, CIRCLE and ARC records. The
export negates model Y to preserve the sketch's visual orientation in DXF's
Y-up coordinates; DXF ARC start/end are reversed to preserve the arc locus.
Constraint relationships and IDs remain in `qindatk.cad/1` only. Exports are
geometry output, not native-document roundtrips. The focused known-geometry
test compares complete SVG and DXF files byte for byte; the native file has
a separate exact save/reopen check. Older version-1 native files without an
`arcs` array reopen with an empty arc collection.

## Domain canvas contract and scope

`CadDocument` shares the domain canvas contract with `DiagramDocument`
(`revision`, `canUndo`, `canRedo`, `lastError`, `undo`, `redo`,
`saveToFile`, `loadFromFile`, `exportSvg`; see
[diagram.md](diagram.md#domain-canvas-contract) and D-122). The declared CAD
operations are the 2D sketch set above (D-125). 3D presentation of
engineering geometry uses the WP-076 scene viewport; a solid-modeling kernel,
splines, relative-angle and curve-curve tangency constraints and DXF/SVG
import are outside the declared set. Large-model precision and performance
qualification belong to the deferred packages WP-088 to WP-094. See
[WP-083](universal-toolkit/implementation/phase-07-integrations.md#wp-083).
