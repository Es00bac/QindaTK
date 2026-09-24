<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Spatial canvas and rendering boundary

`Tk.CanvasView` is the document surface for spatial editors: pan, zoom,
rotation, synchronized views, tools and picking over a real object model,
drawn through an explicit rendering boundary. WP-040 builds it; the proposal
scope is CAN-01 CanvasView, CAN-02 ToolController, CAN-03 HitTestService
and PLT-10 RenderSurfaceAdapter. Evidence:
[WP-040](universal-toolkit/implementation/evidence/WP-040.md).

| Type | Kind | Role |
| --- | --- | --- |
| `Tk.CanvasView` | QML over C++ `CanvasSurface` | Input, navigation, rendering host, overlay slot |
| `Tk.CanvasDocument` | C++ | Reference in-memory spatial document (`CanvasDocumentAdapter`) |
| `Tk.CanvasViewState` | C++ | Shareable navigation pose; views that share one are synchronized |
| `Tk.SnapModel` | C++ | Pure spatial snapping query over guides, visible object geometry and grid |
| `Tk.ToolController` | C++ | Active/temporary tools and the one-gesture state machine |
| `Tk.HitTestService` | C++ | Point and region picking with lock/visibility policy |
| `RenderSurfaceAdapter` | C++ (`view.renderSurface`) | Tiles, damage, budgets, backends, resource generations |
| `Tk.CanvasOverlay` | C++ painted item | Hover, selection outlines and tool preview (inside CanvasView) |

<a id="coordinate-spaces"></a>
## Coordinate spaces

| Space | Unit | Changed by |
| --- | --- | --- |
| document | model units | edits only (`commit`, undo, redo) |
| view | logical item pixels, as Qt input reports them | navigation (the pose) |
| device | physical pixels = view × devicePixelRatio | the screen |

A pose is `{center, zoom, rotation}`: the document point `center` sits at the
middle of the view, one unit is `zoom` view pixels and the document turns
`rotation` degrees clockwise (normalized to (−180, 180]).

```
documentToView = T(viewSize / 2) · R(rotation) · S(zoom) · T(−center)
viewToDocument = T(center) · S(1/zoom) · R(−rotation) · T(−viewSize / 2)
viewToDevice   = S(devicePixelRatio)
```

`viewToDocument` is assembled from inverse factors, not by inverting a
matrix. Round trips are exact to 1e-9 relative (tested for zoom 0.03–64 and
five rotations). Zoom is clamped to the state's `[minZoom, maxZoom]` (strictly
positive), so a view transform is never singular.

Object transforms can be singular. `CanvasTransform::invert` returns nothing
for a non-finite matrix or one whose |det| ≤ 1e-12 × scale²; never use
`QTransform::inverted()`, which silently returns identity. An object with such
a transform is **degenerate**: it stays in the document, is not drawn, not
indexed and not picked, and `CanvasDocument::degenerateObjects()` /
`objectInfo(id).degenerate` report it. `setObjectTransform` refuses non-finite
matrices and accepts finite singular ones.

Navigation (`zoomAbout`, `rotateAbout`) keeps the document point under the
anchor fixed on screen. `panBy(delta)` moves content by `delta` view pixels.
`fit` fits a rectangle *as rotated by the current rotation*.

<a id="object-model"></a>
## Object model

`CanvasObject` has a stable 64-bit id that is never reused within a document,
plus a layer, a shape (`Rect`, `Ellipse`, `Polyline`, `Image`), local
geometry, a local→document `transform`, fill, stroke, and stroke width (in
document units, not scaled by the object transform). It also carries
`visible`, `locked` and `z`. Layers carry `order`, `visible` and `locked`.
An object is effectively hidden or locked when it or its layer is.

Paint order is `(layer order, z)`, ties broken by id. Picking uses the
reverse order.

`Image` objects reference a document-owned `CanvasTiledImage`: a grid of
immutable 256² `QImage` tiles. Renderers and GPU uploads share those tiles
without copying.

Subobject parts on a hit are `Vertex` (index = corner or polyline point),
`Segment` (index = edge), `Stroke` (ellipse outline), `Fill`, and
`ImageTile` (index = row × columns + column).

<a id="document-adapter"></a>
## Document adapter and edits

Views, tools, picking and renderers use only `CanvasDocumentAdapter`. It is
read-only except for `commit(CanvasEdit)`, which applies all of an edit or
none of it. Each change in an edit lists `before` and `after`; an insert has
no `before` and a remove has no `after`. `commit` rejects the whole edit when
any `before` differs from the current object. That makes a stale edit (built
against an older revision) harmless. Each commit increments `revision` and
emits `damaged(regions, revision)` with the old and new document bounds of
every touched object. It also emits `edited(revision)`. `reset(revision)`
invalidates everything (load, clear, layer order).

`CanvasDocument` is the reference implementation. It keeps a bounded undo
history (`historyLimit`, default 256 edits) and a uniform-grid spatial index
(512-unit cells; objects spanning more than 256 cells go in a flat list).
Cell-span arithmetic uses 64-bit endpoint differences, including for finite
bounds that cross both clamped index limits. `populateFixture(count, seed,
withImage)` loads a deterministic fixture without history.

<a id="open-seams"></a>
### Open seams (not claimed)

- **WP-033 session.** WP-033 (shared application framework) is active. The
  shared `DocumentSession` and `UndoService` are built around
  a `PropertyDocument`. A canvas document therefore owns its own history
  behind the adapter. A later session adapter will bind
  `commit`/`undo`/`redo` to the shared services and persistence without
  changing any caller.
- **GestureRouter.** `ToolController` arbitrates gestures for its canvases
  itself. The router's Edit intent needs a PropertyDocument
  `EditTransaction`, so bridging canvas edits into it is part of the same
  seam.
- **WP-041.** `SnapModel` supplies one-point guide/object/grid queries, but
  authoritative selection geometry, multi-object transform edits and guide
  persistence still belong to WP-041. Its `SelectionOverlay` presentation
  slice maps a host-supplied selection and reports handle gestures. The canvas
  exposes the contracts that the remaining work needs:
  `CanvasToolPreview`, `ToolController.selection`, `HitTestService`,
  `CanvasSurface::mapFromDocument` and the `canvasOverlaySlot` item. It
  does not implement the authoritative selection or snap model.

<a id="hit-testing"></a>
## Hit testing

`HitTestService::hitPoint(document, point, toleranceDocument, policy)`
returns hits ordered topmost first. `documentTolerance(pixels, zoom) =
pixels / zoom` is exact because the view transform is a similarity. Input
positions are logical pixels, so `devicePixelRatio` does not change hit
sizes. Tests prove that device → view → document picking matches direct
document picking at zoom 0.25/1/6 × rotation 0/37/−90 × DPR 1/1.5/2.

Within one object the most specific part wins. A vertex within tolerance
wins first. Next comes an outline within tolerance plus half the stroke
width. Otherwise an exact filled containment counts: a rectangle or image is
tested in local space and an ellipse by its implicit equation.

| Lock policy | Locked objects |
| --- | --- |
| `Skip` (default, editing) | transparent to picking, so you can click through a locked backdrop |
| `Report` | returned with `locked: true`; they do not occlude |
| `Block` | the topmost locked hit is returned and hides everything below it |

Hidden objects and hidden layers are skipped unless `includeHidden` is set.

`hitRegion(document, polygon, Intersect|Contain)` supports marquee and
lasso: the polygon may be rotated (a view rectangle mapped into the
document). `Block` degrades to `Skip` for regions. Work is bounded by the
index. In a 10,000-object document, 200 probes examined 275 index entries.

<a id="tool-state-machine"></a>
## Tool state machine

```
idle --begin(view)--> active --update*--> active --end--> idle   (commit ONE edit, or none)
                        |  Escape · lost grab · tool switch · document switch
                        |  document edited elsewhere · provider destroyed
                        |  view destroyed · second finger (navigation)
                        +--cancel(reason)--> idle          (document untouched)
```

- One gesture at a time per controller. Another view's `begin` returns
  `rejected:busy`, and its `update`/`end` are ignored.
- Previews stay inside the tool (`CanvasToolPreview`, document coordinates).
  The document sees nothing until `end`, which commits one `CanvasEdit`: one
  revision and one undo step.
- Before committing, `end` checks that the document revision is still the one
  the gesture began on. If it is not, the outcome is `rejected:stale`.
- `lastOutcome` is `committed`, `completed` (no edit), `canceled:<reason>`,
  `declined:<reason>` (for example `layer-locked` or `layer-hidden`) or
  `rejected:<reason>`.
- Temporary tools (`pushTemporaryTool`/`popTemporaryTool`; CanvasView maps a
  held Space to `pan`) cancel a running gesture before switching.

Built-in tools: `select` (click, shift-add, drag-move, marquee), `lasso`, `pan`,
`rectangle`, `ellipse` and `pen`. The pen keeps at most 4096 samples, and
pressure scales stroke width. With an eraser device the pen removes what it
crosses in one edit. `ToolController.tools` uses the `Tk.ToolShelf` model
keys (`id`, `label`, `iconName`, `tooltip`). It is a C++ list, so pass
`Array.from(controller.tools)`, because ToolShelf checks `Array.isArray`.
The lasso keeps at most 4096 document-space points. Shift adds to selection;
Alt requires full containment instead of intersection. Escape restores the
selection from before the gesture. Lasso selection changes do not create a
document edit or undo step.

<a id="transient-preview"></a>
### Transient preview

`CanvasToolPreview {kind: none|marquee|rect|ellipse|polyline|move|lasso, rect,
points, offset, ids}` is published as `ToolController.preview`. The overlay
draws it. WP-041 overlays read the same structure.

<a id="input"></a>
## Input

| Input | Result |
| --- | --- |
| Left button / pen / one finger | tool gesture (device and pressure forwarded) |
| Middle drag | pan (the view's own; never a tool) |
| Wheel | pan; Shift+wheel horizontal |
| Trackpad pixel scroll | pan by the pixel delta |
| Ctrl+wheel | zoom about the cursor (`wheelZoomStep` per notch) |
| Alt+wheel | rotate about the cursor (`rotationStep` per notch) |
| Two fingers | pan + pinch zoom + rotate about the centroid (`touchRotation`), cancelling any tool gesture |
| Native zoom/rotate/pan/smart-zoom | same as above; smart zoom fits |
| `+`/`=`, `-` | zoom about the centre |
| Ctrl+0 / Ctrl+1 | fit / 100 % |
| R / Shift+R | rotate ± `rotationStep` |
| Arrows | pan by `keyboardPanStep` |
| Space (held) | temporary pan tool |
| Escape | cancel the active gesture |

<a id="synchronized-views"></a>
## Synchronized views

Views that share one `Tk.CanvasViewState` stay synchronized. Each shows the
shared `center` at its own centre, with the same zoom and rotation, at its
own size and DPR. Navigating in either writes the shared state and never the
document. A view without a `viewState` uses a private one.

`Tk.NavigatorView` binds a separate fitted preview to `mainView.document`.
Its outline maps the main view's four corners through document space into the
preview. A press or drag in the preview sets the main view state's centre;
the navigator does not change zoom, rotation or document data. The preview
is noninteractive so it cannot capture canvas editing gestures. WP-042 has
an artboard overlay but no authoritative board model or shared layer-selection model.

<a id="rendering"></a>
## Rendering

The document is tiled **in document space**. The level of detail is
`lod = ceil(log2(zoom × dpr))`. A tile is `tilePixels` (256) device pixels
square and covers `tilePixels / 2^lod` document units, so tiles are never
magnified. A tile appears on screen at (½, 1] of its pixel size. Rotation and
fractional zoom are applied by one transform node, so panning and rotating
reuse tiles.

Tile keys use signed 32-bit x/y indices. Planning includes boundary tiles at
`INT_MIN` and `INT_MAX`, and clips the visible interval to that range before
iteration. A view wholly beyond the addressable grid produces no tiles and
shows the canvas colour; panning back restores rendering.

Visible tiles are those whose rectangle meets the rotated view quad. They are
ordered nearest-centre first. Their number is bounded by
`(ceil(diagonal × dpr / (tilePixels/2)) + 1)²` (169 tiles at 1280×800 and DPR 1).

Two providers sit behind one adapter contract:

| Provider | Tile content | Works on |
| --- | --- | --- |
| Software | `QPainter` into a 256² ARGB32 premultiplied `QImage`, shown as a texture | every Qt Quick backend, including the software adaptation |
| SceneGraph | Vertex-coloured triangles (fills as centroid fans, miter-limited strokes) plus image-tile textures, clipped to the tile | RHI backends (OpenGL, Vulkan, Metal, D3D); the software adaptation silently skips custom geometry, so the adapter falls back and reports `fallbackReason: "software-adaptation"` |

Both providers drop sub-pixel objects to a single device pixel, draw stroke
widths of at least one device pixel, and flatten ellipses with
`clamp(4·√r, 8, 64)` segments, where r is the on-screen radius.

<a id="render-surface-adapter"></a>
## RenderSurfaceAdapter

The GUI-thread half of the boundary is `view.renderSurface`:

- **Request.** Each frame's request carries `documentRevision`, the pose,
  the view size, `devicePixelRatio` and `CanvasColorContext` (working and
  display colour spaces plus a generation). Only sRGB → sRGB is implemented;
  real profile transforms belong to WP-043. Changing the colour context
  invalidates every tile.
- **Damage.** `damaged` marks intersecting cached tiles stale at every LOD,
  padded by two device pixels. A stale tile stays on screen until it is
  re-rendered. `reset` marks every tile stale.
- **Bounded work.** At most `maxTilesPerFrame` (24) tiles are rendered per
  frame; the rest are finished on later event-loop turns (`pending`). Cached
  content is capped at `cacheLimitMiB` (96) with LRU eviction; tiles used in
  the current frame are never evicted.
- **Backends.** `requestedBackend` is `auto`, `software` or `scenegraph`;
  `effectiveBackend` says which one is active. A switch clears the cache and
  bumps `resourceGeneration`.
- **Resource generation.** Any bump makes the render side delete its whole
  node tree and rebuild from the CPU cache. `simulateContextLoss(dropCaches)`
  is the test injection. Qt's `sceneGraphInvalidated` and `releaseResources`
  bump it for real.
- **Stats.** `stats` reports visible, rendered, pending and cached tiles,
  bytes, LOD, candidates, `renderedRevision`, `resourceGeneration` and
  `resetReason`.

**Ownership and threads.** The GUI thread builds an immutable
`CanvasRenderFrame` in `updatePolish`. `updatePaintNode` runs on the render
thread while the GUI thread is blocked. It reads only that frame, never the
document, tools or adapter. Every texture and geometry is owned by a node in
the item's paint-node tree (`setOwnsTexture`, `OwnsGeometry`), so Qt's
scene-graph teardown frees it on the right thread. No QML object ever holds a
render-thread handle. The nodes are identity `QSGTransformNode`s rather than
plain `QSGNode`s, because the software renderer only tracks inherited
transforms through typed nodes. Triangle runs are split at 65,535 vertices
per geometry node; larger batches were drawn with wrapped 16-bit indices.

<a id="overlays"></a>
## Overlays

`Tk.CanvasView` contains `canvasOverlay` (`Tk.CanvasOverlay`: hover outline,
selection outlines up to `outlineLimit` (512) and then their union bounds,
and the tool preview), then `canvasOverlaySlot` for application items (the
view's default property), then `canvasFocusRing`. The overlay draws in view
coordinates from the same transform the renderer uses.

`Tk.SelectionOverlay` is an application overlay for that slot. The host passes
four ordered document-space corners, or an axis-aligned `selectionBounds`,
and the owning `CanvasView` as `surface`. The component maps the corners on
each view change, paints the frame, and places eight resize handles plus a
rotation handle. Pointer events report a handle and document-space
start/current points; keyboard and accessible actions report a nudge or
rotation-step request. The host owns selection identity,
snapping, constraints, preview, cancellation and the one authoritative commit.
`Tk.SpatialTransform` can own that edit boundary for a `CanvasDocumentAdapter`:
connect the overlay's transform request/finish/cancel/nudge and rotation-step
signals to its matching methods, bind `selectionBounds` to `previewBounds`,
and bind `selectionCorners` to `previewCorners`. It reads the selected IDs
from the bound `ToolController`.

`Tk.GuideLayer` is another application overlay. The host supplies guide
records with stable `id`, `orientation` (`vertical` or `horizontal`) and
document-unit `position`. It clips their transformed lines to the view and
offers pointer, keyboard and accessible move/delete requests. Its
`requestAdd` helper maps a view point back into document coordinates. The
host owns guide persistence and the edit transaction. The
[guide scene](../examples/spatial-canvas/GuideScene.qml) shows both orientations
over a rotated page.

`Tk.ArtboardView` is an application overlay for host-owned artboard records.
Each record has a stable `id`, document-space `rect`, `name` and optional
`background`; `activeBoardId` marks the resize affordance. It maps and paints
the board frames through the CanvasView pose and exposes accessible board
labels. A frame press/release requests activation. Dragging a board frame reports
arrange preview/finish/cancel with start and current document points; dragging
the active board's bottom-right handle reports resize intent with the same
semantics. The active press snapshots its board ID and starting point, so an
owner refresh of `boardData` cannot retarget that gesture. Escape suppresses
the rest of that press. Interior and outside presses pass through to the canvas
by default, so object tools work over the board. Set `captureInterior: true`
while an artboard editing tool owns the board interior.
Board backgrounds are document data painted by the host renderer below objects;
the overlay only paints frames. The view does not own geometry, apply previews, clip
content, persist, export or provide undo; those remain domain responsibilities.

<a id="spatial-snapping"></a>
## Spatial snapping

`Tk.SnapModel` is a stateless spatial query over a `CanvasDocumentAdapter` and
`CanvasViewState`. `snapPoint(documentPoint, excludedObjectIds)` returns a
document-space `point`, `delta`, `snapped`, and separate `x`/`y` provenance
maps (`source`, `targetId`, `feature`, `position`, `distancePixels`). Pass stable
object IDs as decimal strings in `excludedObjectIds` so dragged objects do not
snap to themselves. Set `guides` to records with stable string `id`,
`orientation` (`vertical` or `horizontal`), numeric `position` and optional
`visible`. The host stores and edits guide records; the snap model does not.

Visible guides have priority 300, visible object outline vertices,
continuous nearest-edge projections and centers 200, and grid lines 100.
Projected edge axes with effectively zero movement are ignored so a
perpendicular edge cannot suppress a useful snap. Within one priority, the closest
candidate wins; source, target ID and feature break equal-distance ties. A
candidate must be within `tolerancePixels` after scaling by the current view
zoom. The model queries only indexed objects near the point and skips hidden,
degenerate and excluded objects. A disabled query returns the raw point;
callers must send the raw pointer coordinate on every drag update rather than
feeding back a previously snapped point. One query does not edit a document,
own selection identity, or commit an undo step. A group move/resize controller
must choose which anchor points to query and apply the resulting delta in one
authoritative transaction.

<a id="spatial-transforms"></a>
## Spatial transforms

`Tk.SpatialTransform` captures the selected objects and document revision on
the first handle update. It rejects hidden, locked, missing and singular
objects as a group. Raw document-space pointer updates may pass through the
bound `SnapModel`, excluding the selection from snap targets. A move uses the
pointer delta; the eight resize handles scale around the opposite side or
corner. Alt resizes about the center, and Shift keeps the corner scale uniform.
The rotation handle rotates the group around its selection center or a
host-supplied document-space `pivot`; Shift constrains pointer rotation to
15-degree increments. `rotateBy(degrees)` is the equivalent numeric and
keyboard operation. `previewCorners` preserves the rotated frame shape while
the axis-aligned `previewBounds` remains available to consumers.
The controller accepts negative scale when the resulting object transforms
remain invertible, and rejects a zero scale. `previewBounds` moves the selection
frame without mutating the document; `finish` commits all object transforms in
one `CanvasEdit`, while `cancel` drops the preview. External edits or selection
changes cancel a gesture. A keyboard nudge maps view pixels through the bound
`CanvasViewState` rotation and zoom before committing the same operation.

This slice uses the reference canvas edit history. A shared session/undo
adapter, a shared selection model, numeric inspector wiring, persisted pivot,
transformed group-bounds policy and richer constraint rules remain WP-041 work.

`Tk.LayerTree` reads the current `CanvasDocument.layers` facade and sends
active, visibility and lock requests back to it. The list is flat. Layer
hierarchy, reorder and artboard transactions require the WP-042 model work.

`Tk.AlignDistributePanel` accepts selected stable object IDs as strings and
offers selection, key-object and page reference choices. It reports semantic
operations (`align.left`, `align.horizontalCenter`, `align.right`, `align.top`,
`align.verticalCenter`, `align.bottom`, `distribute.horizontal`,
`distribute.vertical`) with those IDs and a document-unit spacing value. The
host computes canonical bounds and lock policy, then commits one scene edit.
The [panel fixture](../examples/spatial-canvas/AlignPanelScene.qml) is only a
visual proof of the reusable controls.

## Color presentation

`Tk.ColorPicker` presents host-provided sRGB preview values with a
saturation/brightness square, hue rail and optional alpha rail. Pointer
interaction emits `previewRequested(color)` repeatedly, then one
`editFinished(color)` on release; Escape requests `editCanceled(color)` with
the starting preview restored. Keyboard arrows and accessible slider actions
make single-step edits through the same preview/finish requests. The control
never owns canonical document colour, profiles, conversion, transactions,
undo or persistence; the host decides how requests affect its model.

This presentation does not imply colour-managed display output. The current
canvas renderer implements sRGB-to-sRGB only. Profile-aware conversion,
out-of-gamut handling, proofing and agreement between managed GPU and software
compositing remain WP-043 backend work. The [picker scene](../examples/spatial-canvas/ColorPickerScene.qml)
demonstrates a host-maintained sRGB preview and committed value.

<a id="curve-presentation"></a>
## Curve presentation

`Tk.CurveEditor` presents host-owned knots with stable string IDs and
normalized x/y values in 0–1, plus a sampled polyline evaluated by the host.
It maps between normalized and view coordinates and reports selection,
move-preview, finish and cancel requests. The pointer gesture belongs to the
root item, so refreshing host knots during a drag does not destroy the drag
owner. A click selects a knot without an edit. Keyboard and accessible
increments send one preview followed by finish.

Knots may also provide optional `inHandle` and `outHandle` records, each with
`{x, y}` as an absolute normalized plot position in the same 0–1 coordinate
space as the knot. They are not relative vectors. Each supplied handle has a
visible line to its knot and a focusable pointer target. The editor emits
`tangentPreviewRequested(knotId, side, x, y)`,
`tangentEditFinished(knotId, side, x, y)` and
`tangentEditCanceled(knotId, side, x, y)`; `side` is `"in"` or `"out"`, and
all positions are normalized absolute coordinates. Cancel reports the
position captured when that handle press began. Arrow keys and accessible
increase/decrease use the same request lifecycle. These are proposals only:
the host validates and replaces its records, while CurveEditor never writes
to them. Stable visual names are `curveTangent_<id>_<side>` for handles and
`curveTangentLine_<id>_<side>` for their connections.

This is a WP-043 presentation slice. A domain curve evaluator must supply
constraints, typed units, transaction history and persisted samples. A
stateless evaluator may consume the same normalized handle records; the editor
does not infer curve semantics from the displayed line.

<a id="gradient-editor"></a>
## GradientEditor

`Tk.GradientEditor` presents host-owned `{id, position, color}` stop records,
with stable IDs and normalized positions. Pointer actions emit selection,
add, move-preview, move-finish, move-cancel and delete requests. A click
selects without committing. The root keeps an active drag snapshot if the
host replaces the stop array during a preview, and Escape cancels that drag.
The host supplies canonical colors and owns interpolation, validation,
persistence and undo; the widget's gradient strip is only a visual preview.
Focusable stop sliders send the same preview and finish requests for arrow or
accessible increments. Returning a pointer drag to its original stop position
cancels the preview instead of requesting a no-op commit.

<a id="viewport"></a>
## Relationship to Viewport

`Tk.Viewport` is unchanged: a scrolled, zoomable page with no rotation,
tools, picking or render boundary. Keep it for page-like content (documents,
scores). Use `Tk.CanvasView` when you need rotation, synchronized views,
picking or large spatial documents.
