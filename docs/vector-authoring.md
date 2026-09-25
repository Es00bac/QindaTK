<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Vector authoring

`StrokeStyleEditor`, `FillStyleEditor`, `ShapePalette` and `AppearanceStack`
expose host-owned vector appearance requests. Their full descriptors retain
unsupported settings so an application can reopen a document without dropping
effects it cannot currently render. The reference fixture is
`examples/vector-authoring/Main.qml`.

`Tk.CanvasStyleModel` connects appearance fields to a `CanvasDocumentAdapter`.
`apply(selectedIds, style)` accepts decimal-string object IDs and any
combination of `fill`, `stroke`, `strokeWidth`, `dash`, `cap`
(`butt`/`round`/`square`), `join` (`miter`/`round`/`bevel`), `fillRule`
(`evenodd`/`nonzero`), `alignment` (`center`/`inside`/`outside`),
`miterLimit` (1–100), `opacity` (0–1), `blend` (a `blendModes` keyword) and
`pattern` keys (see [appearance](#appearance)). Dash values are positive,
even-count stroke-width multiples, matching the `StrokeStyleEditor`
descriptor. Colours may be QML colours or colour strings; null clears a
channel. A style command changes every visible, unlocked selected object in
one `CanvasEdit`, so one undo restores the complete selection. Invalid IDs,
locked/hidden objects, malformed values and unsupported properties reject
the whole edit. The canvas native codec persists every field. The painter
honors them, and the scene-graph path uploads a composited tile for paths,
custom strokes and any non-default appearance to keep output consistent.

<a id="style-binding"></a>
### Binding panels to the selection

Set `CanvasStyleModel.selectedIds` and bind the panels to it:
`StrokeStyleEditor.style: styles.strokeStyle`,
`FillStyleEditor.fill: styles.fillStyle` and
`FillStyleEditor.patterns: styles.patterns`. `selectionStyle` (and
`styleOf(ids)` for any list) reports the first selected object's values plus
`count` and `mixed`, the keys whose values differ across the selection; the
panel descriptors carry the `mixed` keys that concern them. All three follow
every document change, undo and redo included. Route panel requests through
`applyStrokeStyle(descriptor)` and `applyFillStyle(descriptor)`: only fields
that differ from the published descriptor become style keys, so changing the
alignment of a mixed selection leaves each object's own width alone. Choosing
the `custom` dash mode before typing values is kept as presentation state
until a dash is entered. A fill kind `pattern` applies the descriptor's
swatch, `solid` its colour (a neutral grey when none was set) and `none`
clears both; gradients go through `applyGradient`. The
[style scene](../examples/vector-authoring/StyleWorkingScene.qml) wires the
stroke, fill, opacity and blend controls, an `AppearanceStack` and the path
operations this way; `tests/qml/tst_vector_appearance_example.qml` drives it.

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
flat RGBA fill/stroke, fill rule, cap, join, miter limit and dash pattern, plus
the [appearance](#appearance) (D-109): a pattern becomes a `<pattern>` cell in
the object's local units (the fill colour behind the ink); an aligned stroke
becomes the fill and a doubled stroke under a document-space `<mask>`;
effects become SVG 1.1 filter primitives (`feGaussianBlur`, `feOffset`,
`feFlood`, `feComposite`, `feMerge`) chained in stack order with
`color-interpolation-filters="sRGB"`; opacity becomes `opacity` and blend the
CSS `mix-blend-mode` on a `<g>`. Stroke widths are document units: a
similarity transform stays an attribute with the width and dashes divided by
its scale, and any other transform is written into document-space path
geometry. It reports an explicit unsupported error for image objects, typed
ICC/gradient fills, non-sRGB output ICC and enabled effects of a kind the
reference canvas does not draw, so export cannot silently lose them (a
bypassed effect does not block export). A zero-width canvas hairline exports
as one SVG document unit at nominal zoom. Native `qtkc` version 6 saves path
nodes, stroke styles, extra contours, parametric shape definitions and the
appearance block; the reader also accepts versions 1 through 5.
`Tk.ClipboardService.copyCanvasPaths(document,objectIds)` publishes the
bounded native path snapshot and SVG alternative;
`pasteCanvasPaths(document, offsetX, offsetY)` inserts one undoable edit with
fresh stable object IDs into the active layer. The clipboard accepts paths
only and does not copy image resources. The session schema ID is unchanged.

<a id="appearance"></a>
## Appearance

`CanvasObject::appearance` holds everything beyond flat fill and stroke
(D-106). It is an immutable, implicitly shared `CanvasAppearanceRef`, null for
the default: read it with `->` or `*`, and change it by assigning a
`CanvasAppearance` or with `update([](CanvasAppearance &a) { ... })`, which
copies rather than writing a record other objects or undo steps share.
Commits, the native decoder and both models share its rules
(`CanvasAppearanceRules`):

- **Stroke alignment.** `center` strokes straddle the outline; `inside` and
  `outside` paint the whole width on one side. They need a closed vector
  shape (rectangle, ellipse, or a path with a closed contour). The renderer
  strokes twice the width and masks the unwanted half with the fill area, so
  dashes stay multiples of the visible width. `documentBounds` grows by the
  full width outside and not at all inside; picking reaches the full width.
- **Miter limit** uses SVG `stroke-miterlimit` semantics (1–100, default 4).
- **Opacity and blend.** Opacity (0–1) and the separable blend modes
  `normal`, `multiply`, `screen`, `overlay`, `darken`, `lighten`,
  `color-dodge`, `color-burn`, `hard-light`, `soft-light`, `difference` and
  `exclusion` apply to the object as a whole, after its effects.
- **Patterns** draw ink over the fill area (the fill colour, if any, shows
  between the ink). Kinds are `lines`, `grid` (one band each way, crossings
  inked once), `dots` (diameter `weight`) and `checker`, on cells `spacing`
  wide in the object's local units, rotated by `angle` degrees about the local
  origin. `weight` must be positive and at most `spacing` (0.05–10000). The
  style key takes a swatch id from `patterns` (`hatch`, `crosshatch`, `grid`,
  `dots`, `checker`), a `{kind, ink, spacing, angle, weight}` map, or null.
  Without an ink the object's current pattern ink, else its stroke colour,
  else black is used. A pattern-only object still fills its area for
  picking. On screen, cells under two device pixels draw as their average
  coverage.
- **Effects** are an ordered stack applied top first to the painted fill,
  pattern and stroke. Each has a stable token `id` (unique in the object), a
  `kind`, an `enabled` flag and canonical JSON parameters. `drop-shadow`
  takes `offsetX`/`offsetY` (±1000), `blur` (Gaussian standard deviation,
  0–250) and `color`; `gaussian-blur` takes `radius` (standard deviation).
  Offsets and blurs are document units, like stroke widths. Any other kind is
  kept with its parameters, reported unsupported and neither drawn nor
  exported. The blur is the SVG three-box approximation; shadow offsets snap
  to whole device pixels on screen.

Effects, opacity, blend and aligned strokes paint through an offscreen layer
that covers the tile plus the stack's reach, so tiles show no seams (D-107).
`CanvasGeometry::paintBounds` adds the effect reach to `documentBounds`; the
document indexes and damages objects by it, while selection boxes,
alignment and snapping keep `documentBounds`. A host adapter's `candidates()`
must index by `paintBounds` too. Very large blurs at high zoom render their
layer at a lower resolution rather than exceeding 4096 px.

`Tk.CanvasAppearanceModel` binds one object's stack (`objectId`, a
decimal-string id) to `AppearanceStack`: `effects` is the stack in
AppearanceStack's record shape, `editable` is false for hidden or locked
objects, and `addEffect(kind, parameters)` (ids `<kind>-<n>`),
`removeEffect`, `moveEffect(id, index)`, `setEffectEnabled` and
`setEffectParameters` each commit one undoable edit. Wire
`reorderRequested` to `moveEffect` and `enabledChangeRequested` to
`setEffectEnabled`.

<a id="path-booleans"></a>
## Path operations

`CanvasDocument.combinePaths(ids, operation)` combines two or more visible,
unlocked rectangles, ellipses and paths whose contours are all closed
(D-108). `union`, `intersect`, `subtract` (the backmost operand minus all the
others) and `exclude` run Qt's path clipper, which honours each operand's
fill rule and resolves self-intersections. Operands are first flattened to
straight segments within 1e-4 of their combined extent (a circle becomes
about 160 segments at any size), so results are straight-segment contours
with the clipper's fill rule. `combine` merges every contour unchanged, cubic
handles included, into one compound path with the backmost operand's fill
rule. The result replaces the backmost operand: it keeps that object's id,
layer, paint order, style, appearance and transform (its nodes are expressed
in that local space), and the other operands are removed in the same
undoable edit. An empty result (for example two disjoint shapes
intersected), fewer than two operands, open or hidden/locked operands and
unknown operations return 0 with `lastError` set and change nothing.

Still outside this reference backend: curve-preserving boolean geometry,
other parametric families, effect masks and layer- or group-level
transforms, opacity and blend (see the WP-044 evidence for their owners).
