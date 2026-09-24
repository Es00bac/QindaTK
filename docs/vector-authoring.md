<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Vector authoring

`StrokeStyleEditor`, `FillStyleEditor`, `ShapePalette` and `AppearanceStack`
expose host-owned vector appearance requests. Their full descriptors retain
unsupported settings so an application can reopen a document without dropping
effects it cannot currently render. The reference fixture is
`examples/vector-authoring/Main.qml`.

`Tk.CanvasStyleModel` connects supported appearance fields
to a `CanvasDocumentAdapter`. `apply(selectedIds, style)` accepts decimal-string
object IDs and any combination of `fill`, `stroke`, `strokeWidth`, `dash`,
`cap` (`butt`/`round`/`square`), `join` (`miter`/`round`/`bevel`) and
`fillRule` (`evenodd`/`nonzero`) keys. Dash values are positive, even-count
stroke-width multiples, matching the `StrokeStyleEditor` descriptor.
Colours may be QML colours or colour strings; null clears a channel. A style
command changes every visible, unlocked selected object in one `CanvasEdit`,
so one undo restores the complete selection. Invalid IDs, locked/hidden
objects, malformed values and unsupported properties reject the whole edit.
The canvas native codec persists the supported fields. The painter honors
them, and the scene-graph path uploads a composited tile for paths or custom
strokes to keep output consistent.

The stroke panel's `color`, `width`, `dash`, `cap` and `join` map to the matching
canvas fields after the host converts its chosen unit to document units. The
solid fill panel's
`settings.solid.color` maps to `fill`; kind `none` clears it. The reference
canvas has editable cubic paths: each node stores a local-space anchor plus
absolute incoming and outgoing controls. `CanvasDocument.addPath` takes
`[{x,y,inX,inY,outX,outY}, ...]`, with omitted controls collapsing to the
anchor. `movePathPart(id,index,"anchor"|"in"|"out",x,y)` commits one undoable
edit; moving an anchor preserves both handle offsets. `insertPathNode`,
`removePathNode` and `setPathClosed` are also undoable. A closed path needs
three nodes and an open path two. `pathNodes(id)` returns the first contour.
`addPathContour(id,nodes,closed)` appends a subpath; `removePathContour`,
`movePathContourPart`, `insertPathContourNode`, `removePathContourNode` and
`setPathContourClosed` commit reversible edits. `pathContourCount` and
`pathContourNodes` expose zero-based contours. Even-odd fill makes a nested
closed contour a hole; nonzero fill follows each contour's winding. Both
painter and scene-graph composite render them, and hit testing respects the
hole. `Tk.PathEditor.contourIndex` chooses which contour's handles to edit.

<a id="parametric-shapes"></a>
`CanvasDocument.addParametricShape(kind,x,y,width,height,parameters,fill,stroke,
strokeWidth)` creates a `polygon`, `star` or `rounded-rectangle` as a closed
one-contour path. Besides `x`/`y`/`width`/`height`, each kind takes only its
own keys: polygon `sides` and star `points` are integers 3–64, star
`innerRadius` is a fraction strictly between 0 and 1, and rounded rectangle
`radius` is at most half the shorter side. Polygon and star vertices lie on
the ellipse inscribed in the bounds, clockwise from the top centre; star inner
vertices sit at `innerRadius` of the outer radius. Rounded corners are cubic
quarter arcs. At full radius the two ends of a zero-length side merge into one
smooth node (a full-radius square is a 4-node circle), so generated paths hold
no zero-length segment. `setParametricParameters(id,patch)` changes count,
ratio, radius or bounds as one undoable path regeneration.
`parametricParameters(id)` returns `kind`, the bounds and the kind's
parameters, and that map is accepted back unchanged. Invalid values, another
kind's keys and freeform, missing or locked objects return 0/false without an
edit. All three render, pick and SVG-export through the path backend. Native
files retain both the definition and the generated nodes. Commits and decoding
reject a definition whose nodes no longer match it beyond platform rounding,
so a host edit that moves nodes must call `CanvasObject::clearParametric()`.
`movePathPart`, `insertPathNode`, `removePathNode`, `setPathClosed` and
`addPathContour` do so: a direct node/handle edit converts the shape to a
freeform path, and undo restores the parametric definition. The real
[ShapePalette scene](../examples/vector-authoring/ParametricWorkingScene.qml)
creates and edits these shapes; `tests/qml/tst_vector_parametric_example.qml`
drives it through the palette, the parameter fields, a node drag and undo.

`Tk.PathEditor` overlays a `CanvasView`, maps the selected path through its
object transform and view pose, previews node/handle drags, then calls the
document edit once on release. Escape cancels the preview.

`CanvasDocument.exportSvg(filePath)` atomically writes the supported SVG 1.1
subset: page background, rect, ellipse, polyline, cubic path, object transform,
flat RGBA fill/stroke, fill rule, cap, join and dash pattern. It reports an
explicit unsupported error for image objects, typed ICC/gradient fills and
non-sRGB output ICC, so export cannot silently lose them. A zero-width canvas
hairline exports as one SVG document unit at nominal zoom. Native `qtkc`
version 5 saves path nodes, stroke styles, extra contours and parametric shape
definitions; the reader also accepts versions 1 through 4.
`Tk.ClipboardService.copyCanvasPaths(document,objectIds)` publishes the
bounded native path snapshot and SVG alternative;
`pasteCanvasPaths(document, offsetX, offsetY)` inserts one undoable edit with
fresh stable object IDs into the active layer. The clipboard accepts paths
only and does not copy image resources. The session schema ID is unchanged.

Still outside this reference backend: arbitrary boolean geometry operations,
stroke inside/outside alignment, other parametric families, pattern fills,
and appearance-stack effects.
The panels keep these descriptors for a richer host and the reference model
rejects unsupported style keys rather than dropping them.
