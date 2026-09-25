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

Give `CanvasStyleModel.selectionModel` the `CanvasSelectionModel` the
canvas's `ToolController` uses, so a click on the canvas selects what the
panels edit (or set `selectedIds` directly when there is no canvas; with a
selection model, writing `selectedIds` selects there). Bind the panels:
`StrokeStyleEditor.style: styles.strokeStyle`,
`FillStyleEditor.fill: styles.fillStyle` and
`FillStyleEditor.patterns: styles.patterns`. `selectionStyle` (and
`styleOf(ids)` for any list) reports the first selected object's values plus
`count` and `mixed`, the keys whose values differ across the selection; the
panel descriptors carry the `mixed` keys that concern them. All three follow
every document change, undo and redo included. The panel descriptors name
mixed fields in their own terms (`width`, `color`, `cap`, ...; for fill
`kind`, `color`, `pattern`, `gradient`), and the panels show them as
"Mixed". Route panel requests through `applyStrokeStyle(descriptor)` and
`applyFillStyle(descriptor)`. The panels add `changed`, the fields the user
set, and exactly those become style keys (D-285): setting a mixed width to
the value shown makes every width that value, and changing the alignment of
a mixed selection leaves each object's own width alone. A descriptor without
`changed` applies the fields that differ from the published one. Choosing the
`custom` dash mode before typing values is kept as presentation state until a
dash is entered.

Fill kinds are recoverable (CAN-18). While bound, the model keeps each
selected object's last solid colour (typed or plain), pattern (with the
colour behind its ink) and gradient. A kind change restores each object's
own settings of that kind: `none` and back to `solid` returns every object's
colour, a custom pattern comes back exactly, and `gradient` returns the
object's gradient. Without earlier settings, `solid` keeps a colour already
under a pattern or takes the descriptor's (else a neutral grey), `pattern`
applies the descriptor's swatch inked with the stroke, and `gradient` applies
the `gradientEditor`'s definition (a `GradientEditorModel`, the nested
editor, which the model also loads with the key object's gradient); with
neither, the request is refused with a reason. `settings` publishes, per
kind, what choosing it brings back (`settings.pattern.custom` marks a custom
pattern). An explicit colour, swatch or gradient (`changed` names it) applies
to every selected object. Recall is session state; undo is unaffected.

<a id="stroke-units"></a>
**Stroke units.** `strokeUnits` is `document`, `px`, `pt`, `mm` and `in`.
One document unit is one CSS pixel (1/96 in): SVG export writes document
units as user units under a `viewBox`, which viewers size as pixels.
Choosing a unit (a request whose `changed` is `unit`) is presentation and
edits nothing; `strokeStyle.width` is then in that unit and a requested
width is converted to document units. The value last typed is shown exactly
(no conversion rounding) while the document still holds the width it
produced, so switching units and back retains exact intent; a mixed
selection switches units without touching any width. The
[style scene](../examples/vector-authoring/StyleWorkingScene.qml) wires the
stroke, fill, opacity and blend controls, an `AppearanceStack` and the path
operations this way; `tests/qml/tst_vector_appearance_example.qml` drives it.

The stroke panel's `color`, `width`, `dash`, `cap` and `join` map to the matching
canvas fields (`width` through the unit conversion above). The
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

<a id="path-editing"></a>
### Node editing (CAN-19)

`PathEditor.model` is a `CanvasPathEditModel` over the same object. It
publishes every node of every contour with a stable id (`n<k>`) and derived
`type` (`corner`, `smooth` when both handles are collinear on opposite sides,
`symmetric` when they are also equal), each segment with an id (`s` + its
start node's id) and each contour's closed state. Ids survive the model's own
edits, undo and redo of them (it remembers the ids of geometry it has seen)
and edits made elsewhere (nodes keep their ids by anchor matching); they are
editor state, not saved (D-284). The node selection is held by id, so it
survives segment insertion, undo and moves; ids that disappear leave it.

- `insertNode(segmentId, t)` splits the segment's cubic at `t` by de
  Casteljau, so the outline does not change; it returns the new node's id.
- `deleteNodes(ids)` removes nodes (the selection by default) in one edit.
- `setNodeType(type, ids)`: `corner` retracts both handles; `smooth` aligns
  them on the direction through the neighbouring anchors, keeping their
  lengths (a retracted handle takes a third of its neighbour distance);
  `symmetric` also equalizes the lengths.
- `splitAt(id)` opens a closed contour at the node (it becomes the first
  node and a copy the last) or cuts an open contour in two there; the copy
  gets a new id.
- `join(a, b)`: the two ends of one open contour close it; ends of two open
  contours become one contour (reversed as needed). Coincident ends merge
  into one node.
- `moveNodePart(id, part, x, y)` moves an anchor (with its handles) or a
  handle in local coordinates.

Each is one undoable `CanvasEdit`, demotes a parametric shape to freeform and
is validated like any object edit. A contour left with fewer than three
closed or two open nodes, an interior node given to `join`, an end given to
`splitAt`, `t` outside (0, 1), a hidden or locked path and unknown ids are
refused with `lastError`, and nothing changes. In `PathEditor`, a click on an
anchor selects it (Shift adds or removes), selected anchors are solid, Delete
removes the selection, and `insertAfterSelection()`, `deleteSelection()`,
`splitSelection()`, `joinSelection()` and `setSelectionType(type)` report
refusals through `editRejected(message)`. The
[path scene](../examples/vector-authoring/PathWorkingScene.qml) wires these
to buttons; `tests/qml/tst_vector_path_editing_example.qml` drives it.

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
- **Miter limit** uses SVG `stroke-miterlimit` semantics (1–100, default 4):
  a join whose miter length exceeds limit x width bevels, on the canvas
  (`Qt::SvgMiterJoin`) as in exported SVG. `documentBounds` includes each
  mitred join's tip (up to the limit), so a raised limit never leaves a spike
  outside the spatial index or damage rectangles (D-284).
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
  0–250) and `color`; `gaussian-blur` takes `radius` (standard deviation);
  the nondestructive adjustment `color-adjust` takes `brightness` (−1–1,
  default 0), `contrast` (0–4, default 1) and `saturation` (0–1, default 1)
  and changes each pixel's straight sRGB values: saturation as SVG
  `feColorMatrix type="saturate"`, then `c * contrast + (0.5 − 0.5 *
  contrast + brightness)` as `feComponentTransfer` linear functions, each
  clamped to 0–1, which is how SVG export writes it.
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
`setEffectParameters` each commit one undoable edit; a partial parameter map
keeps the values it leaves out. Wire `reorderRequested` to `moveEffect` and
`enabledChangeRequested` to `setEffectEnabled`. With `selectionModel` the
object is the selection's key (first) object. `paints` lists the object's
fixed paint entries — `fill`, `pattern`, `stroke`, in the order they are
painted, each with `present` and a `detail` — for `AppearanceStack.paints`,
which shows them above the effects.

Scope (CAN-24, WP-044 item 3): an object has one fill, one pattern and one
stroke painted in that fixed order, and the stack inspects them; plan item 3
asks for *reorderable appearance effects*, which the effect stack provides
(shadows, blurs and the colour adjustment). Several fills or strokes per
object, reorderable among themselves, would need a multi-paint object model
and native format change that WP-044's numbered requirements do not ask for;
they are not offered.

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
