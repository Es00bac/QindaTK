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
| `Tk.CanvasDocument` | C++ | Reference spatial document (`CanvasDocumentAdapter`, `DocumentModel`); opens, saves and recovers as a `DocumentSession` |
| `Tk.CanvasViewState` | C++ | Shareable navigation pose; views that share one are synchronized |
| `Tk.SnapModel` | C++ | Pure spatial snapping query over guides, visible object geometry and grid |
| `Tk.SpatialTransform` | C++ | Group move/resize/rotate: handles, keyboard and numeric entry commit the same one edit |
| `Tk.SpatialTransformEditor` | QML | Numeric inspector (position, size, rotation, pivot) over a `SpatialTransform` |
| `Tk.ToolController` | C++ | Active/temporary tools and the one-gesture state machine |
| `Tk.CanvasSelectionModel` | C++ | Shared live-object selection across tool controllers/views |
| `Tk.CanvasLayerTreeModel` | C++ (`QAbstractItemModel`) | Layer/group/object rows derived from the document; requests become document commands ([layer tree model](#layer-tree-model)) |
| `Tk.CanvasArtboardModel` | C++ | Active board and gesture preview for `Tk.ArtboardView`; commits board edits ([artboard model](#artboard-model)) |
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
plus a layer, a shape (`Rect`, `Ellipse`, `Polyline`, `Image`, `Path`), local
geometry, a local→document `transform`, fill, stroke, and stroke width (in
document units, not scaled by the object transform). It also carries
`visible`, `locked` and `z`. Layers carry `order`, `visible`, `locked`, a
`parent` group and a `group` flag ([layer hierarchy](#layer-hierarchy)).
An object is effectively hidden or locked when it, its layer or any
ancestor group is.

Paint order is `(layer rank, z)`, ties broken by id, where the rank is the
layer's place in the depth-first flattening of the layer tree. Picking uses
the reverse order.

`Path` stores ordered contours of cubic nodes in local coordinates. Each
node has an anchor and absolute incoming/outgoing controls; coincident
controls create straight edges. Closed paths support odd-even or nonzero
winding fill. Polygon, star and rounded-rectangle definitions generate
path nodes and retain editable parameters; direct node edits make them
freeform. Strokes also carry cap, join and bounded dash settings. Node,
parameter and handle edits are reversible `CanvasEdit` changes; see
[vector authoring](vector-authoring.md).

`Image` objects reference a document-owned `CanvasTiledImage`: a grid of
immutable 256² `QImage` tiles. Renderers and GPU uploads share those tiles
without copying.

Raster painting has a focused reference backend in
[raster-document.md](raster-document.md). It owns editable premultiplied pixel
tiles and optional grayscale masks; `placeOnCanvas` and `publishToCanvas` put
its flattened composite into these immutable image assets as ordinary
recorded canvas edits ([canvas placement](raster-document.md#canvas-placement)).

Subobject parts on a hit are `Vertex` (index = corner, polyline point or path anchor),
`Segment` (index = edge), `Stroke` (ellipse outline), `Fill`, and
`ImageTile` (index = row × columns + column).

<a id="document-adapter"></a>
## Document adapter and edits

Views, tools, picking and renderers use only `CanvasDocumentAdapter`. It is
read-only except for `commit(CanvasEdit)`, which applies all of an edit or
none of it. Each object change in an edit lists `before` and `after`; an
insert has no `before` and a remove has no `after`. An edit may also carry
layer, image (shared `CanvasTiledImagePtr`, null = absent), page, guide-list
and artboard-list deltas under the same rule. `commit` rejects the whole edit when any target appears
twice, or any `before` differs from the current state. That makes a stale
edit (built against an older revision) harmless. It also rejects the edit
when the resulting document would be invalid: non-finite geometry, a missing
layer or image reference, a removed layer that still holds objects, a removed
image still referenced, the last layer removed, an invalid layer tree (a
missing or non-group parent, a cycle, nesting deeper than
`maximumLayerDepth`, objects on a group, no content layer left), invalid
artboards, or a limit (`CanvasDocumentLimits`) exceeded. An edit that names
`baseResetGeneration` is stale after a reset. `commitEdit` returns the
reason. A rejected commit changes nothing and emits nothing: no revision,
damage, history step or notification. Each accepted commit increments
`revision` and emits `damaged(regions, revision)` with the old and new
document bounds of every touched object. It also emits `edited(revision)`.
`reset(revision)` invalidates everything (load, clear, layer order).

`CanvasDocument` is the reference implementation and a
[`DocumentModel`](document-models.md). The applied edit is its recorded undo
step, so history holds touched-object deltas and shared image pointers and
never a copy of the scene. Standalone, the document keeps a private
`UndoService` (`historyLimit`, default 256 steps). A `DocumentSession` moves
that single history into the application's shared `UndoService`, where
`manager.undoService` and `document.undo()` drive the same steps under the
shared byte budget. Page size and colour, layer add/remove/visibility/lock
and image add/remove are recorded steps too; every content change
increments the revision. `insert(object, false)` still validates, but
unrecorded content makes a saved clean state unreachable. `clear()` and
`populateFixture(count, seed, withImage)` are loads: they increment
`resetGeneration` and discard the history. The active layer is editor state:
it changes no revision, is not saved, is never a group, and a loaded
document starts on its topmost content layer. Values declared on a `Tk.CanvasDocument` in QML set up the
document without creating undo steps. The uniform-grid spatial index uses
512-unit cells; objects spanning more than 256 cells go in a flat list.
Cell-span arithmetic uses 64-bit endpoint differences, including for finite
bounds that cross both clamped index limits.

<a id="open-seams"></a>
### Open seams (not claimed)

- **GestureRouter.** `ToolController` arbitrates gestures for its canvases
  itself. The router's Edit intent needs a PropertyDocument
  `EditTransaction`, so bridging canvas edits into it is still open. Canvas
  sessions, persistence, recovery and shared undo are provided through the
  [domain model boundary](document-models.md).
- **WP-041.** Selection, snapping, group transforms, numeric entry and
  document guides are described under [spatial transforms](#spatial-transforms)
  and [document guides](#document-guides). Not claimed: skew handles, a
  draggable pivot handle, object-angle snapping, and grid or measurement
  overlays; none is in WP-041's requirements. The canvas itself still does not
  store selection or calculate snaps; `CanvasToolPreview`,
  `ToolController.selection`, `HitTestService`, `CanvasSurface::mapFromDocument`
  and the `canvasOverlaySlot` item remain the contracts overlays build on.

<a id="persistence"></a>
## Persistence and native format

`canvasDocumentCodec()` (schema `qindatk.canvas-document.v1`, suffix
`qtkc`) lets `DocumentManager` create, open, save, recover and restore canvas
sessions like any other document. A save captures `CanvasDocument::state()`
on the GUI thread. That is an O(1) share of the object, layer and image
hashes; the next edit detaches the document's copy. A worker encodes the
state.

Guides, artboards and the layer tree travel inside the extensions JSON
(D-041, D-092, D-093) under `qindatk.canvas.guides`,
`qindatk.canvas.artboards` and `qindatk.canvas.layerTree`, so the binary
layout and version stay unchanged and an older reader keeps them as opaque
data. The decoder moves each record out of the host extensions and validates
it against the decoded layers and objects (unique ids, finite geometry, group
parents, no cycles, depth, no objects on a group). A flat document without
guides or boards writes none of them, so its bytes are unchanged. Qt's JSON
drops U+FEFF and cannot carry an unpaired surrogate, so an artboard name that
JSON would change is also written as base64 UTF-16LE code units in
`nameUtf16`, which the decoder prefers; artboard names round-trip exactly,
like binary layer and object names. Host
extensions and these records share `maximumExtensionBytes`; a save that would
exceed it fails explicitly.

<a id="native-format"></a>
The file is big-endian binary: the signature `QTKCANVS`, version 6 (the reader
also accepts versions 1 through 5), the
document UUID, revision and reset generation, then the exact 64-bit
`nextObject`/`nextLayer`/`nextImage` counters and `nextZ`, the page rectangle
and colour, an owned output RGB ICC profile (or empty for sRGB), the session
extensions (JSON, which also carries the [document guides](#document-guides),
[artboard](#artboards) and [layer tree](#layer-hierarchy) records), layers,
images and objects in
ascending id order, and finally a SHA-256 of everything before it. Equal
content therefore gives equal bytes. Names are stored as UTF-16 code units,
so every `QString` round-trips exactly; UTF-8 would drop a leading U+FEFF or
an unpaired surrogate. Image tiles are raw ARGB32
premultiplied rows at their stored size, so pixels round-trip exactly.
`addImage` normalizes tiles to that format. Tiles are not compressed:
`qUncompress` does not bound its output to the declared size, so compressed
tiles would first need a bounded inflater.
Each version 2 or newer object has a bounded managed-fill payload after its name.
Solid fills own their tagged channels and ICC bytes; gradient fills own their
typed stop definition and stop ICC bytes. Version 3 adds path nodes, closure,
fill rule, cap, join and dash pattern to each object. Version 4 adds
bounded extra path contours, each with its own closure. Version 5 adds
polygon, star and rounded-rectangle definitions beside generated path nodes.
Version 6 ends every object record with a length-prefixed appearance block
(stroke alignment, miter limit, opacity, blend, pattern and effect stack;
empty for the default appearance, D-106), so the commit-time size accounting
covers it like the rest of the record.
The `qindatk.canvas-document.v1` session schema ID remains stable while the
native file reader handles all six binary versions.
`setOutputProfileIcc(bytes)` changes the document's output
reference and is persisted; it is currently a document-setting change outside
object undo history.

Decoding validates the checksum, the version (a newer one is refused),
identity and counters (every id below its counter, no duplicates), layer and
image references, finite geometry, parametric definitions against their
generated path nodes, tile grids and every total before it
allocates decoded pixels. Only then does `adopt` build the live document.
Each case below fails with a field path and changes nothing.

| Bound (`CanvasDocumentLimits`) | Default |
| --- | --- |
| Encoded file | 320 MiB |
| Objects / layers / images | 2,000,000 / 4096 / 4096 |
| Points per object / in total | 1,000,000 / 16,000,000 |
| Image side / tile size | 65,536 px / 16–4096 px |
| Decoded image pixels (all images) | 256 MiB |
| Names / extensions JSON | 4096 characters / 16 MiB |
| Output ICC / managed fill per object | 16 MiB / 24 MiB |
| Path nodes / dash entries per object | 1,000,000 combined points / 4096 |

The commit validator uses the same bounds. It also tracks the exact encoded
size and refuses an edit that would grow the file past the limit while
leaving room for the largest allowed extensions JSON, so every committed
canvas can be saved and reopened. Edits that shrink a file always remain
possible. `makeCanvasDocumentCodec(limits)` registers a codec with
other bounds. `CanvasNativeFormat` offers the same file to `FormatRegistry`
for import and export when an application registers it.

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

A pose change that did not come through a view (another view on the shared
state, a `NavigatorView`, the host writing the state) is navigation for that
view: a view with a `fitMode` stops refitting until `fitDocument()`, so a
later edit or resize cannot refit the shared pose over it. A `changed`
signal that leaves the pose as it was (a zoom-limit change) is not
navigation. `CanvasViewState.setCenter(x, y)` moves both coordinates in one
change, so synchronized views never see a half-moved centre.

`Tk.NavigatorView` binds a separate fitted preview to `mainView.document`.
The preview is a second `CanvasView` on the same render providers with its
own private view state, fitted to the content (`fitMode: "content"`), so it
renders at its own coarse level of detail and never requests the main
view's resolution; it refits after edits and after a model reset. Its
outline maps the main view's four corners through document space into the
preview. A press or drag in the preview centres the main view there
(`setCenter`, one pose change per step); arrow keys pan the main view by
`keyboardStep` of its visible extent, and `+`/`-`, the wheel and the
accessible increase/decrease actions zoom it about its centre. The navigator
never changes document data, selection or tool state, and the main pose
never feeds back into the preview. The preview is noninteractive so it
cannot capture canvas editing gestures.

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

For an explicit non-sRGB RGB display space, both backends use a fully
composited `QImage` tile converted by Qt after painting. The scene graph then
uploads that same encoded raster as a texture. A document's output ICC is the
default display/output reference; an explicit `setColorContext` call overrides
it. Managed solid and gradient fills also take this composited tile path on
the scene graph, so they blend before output conversion, and so does any
object with a non-default [appearance](vector-authoring.md#appearance)
(aligned strokes, patterns, effects, opacity, blend), which the painter draws
through a per-object offscreen layer (D-107). The document indexes objects by
`CanvasGeometry::paintBounds`, which includes effect reach, so a shadow
reaches every tile it touches; an adapter with its own index must do the
same. Their typed ICC
values are converted to sRGB for composition; gradients are sampled at 65
positions across each object's document-space bounding box. The raw sRGB
scene-graph path keeps geometry runs for other objects. The supported output
path is RGB and relative colorimetric, with black-point compensation off.
Other rendering intents and soft proof requests report `colorError` explicitly;
CMYK and arbitrary working-space painting are not supported.
Paths and nondefault cap, join or dash strokes use the composited raster-tile
path on the scene graph so cubic geometry and winding match software output.

Both providers drop sub-pixel objects to a single device pixel, draw stroke
widths of at least one device pixel, and flatten ellipses with
`clamp(4·√r, 8, 64)` segments, where r is the on-screen radius.

<a id="render-surface-adapter"></a>
## RenderSurfaceAdapter

The GUI-thread half of the boundary is `view.renderSurface`:

- **Request.** Each frame's request carries `documentRevision`, the pose,
  the view size, `devicePixelRatio` and `CanvasColorContext` (working and
  display colour spaces plus a generation). sRGB-authored tiles can be
  transformed after compositing to a valid RGB display space. Changing the
  colour context clears every cached tile so old-profile pixels cannot remain
  visible. Unsupported spaces are reported in `colorError` and leave the
  frame empty; `stats.colorError` carries the same text.
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
  bump it for real. A canvas moving to another live window also bumps its own
  generation; the second canvas keeps its generation and resources. Scene
  graph initialization schedules a fresh frame so the new window can upload
  tiles from its own CPU cache after a reset.
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
rotation-step request; the focusable `selectionMoveTarget` (no pointer
handler, so canvas tools still get presses) reports arrow keys as a `move`
nudge. A handle drag held within `autoScrollMargin` (`size.dropEdge`) of a
view edge, or past it, pans the surface every frame (`autoScrolling`,
`autoScrollStep(seconds)`) and re-reports the unchanged view point as a new
document point, so the gesture continues over newly revealed content. The
host owns selection identity,
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
`requestAdd` helper maps a view point back into document coordinates. A
pointer drag is previewed by the layer itself: it reports every move and one
`guideMoveFinished` (or `guideMoveCanceled`), so a host that commits on finish
records one step per drag. A keyboard or accessible step reports a move and
its finish. `previewAdd`/`finishAdd`/`cancelAdd` let a host drag a new guide
out of a ruler (`pendingGuide` is the previewed record). The host owns the
records and the edit; `Tk.CanvasDocument.guides` is a ready document-owned
source ([document guides](#document-guides)). The
[guide scene](../examples/spatial-canvas/GuideScene.qml) shows both orientations
over a rotated page.

<a id="artboard-view"></a>
`Tk.ArtboardView` is an application overlay for artboard records. Each record
has a stable `id`, document-space `rect`, `name` and optional `background`;
`activeBoardId` marks the resize affordance. With `artboardModel` (a
`Tk.CanvasArtboardModel`, [below](#artboard-model)) the records and the active
board come from the document and every gesture is sent to the model; without
it a host supplies `boardData` and handles the signals itself. The view maps
and paints the board frames through the CanvasView pose and exposes
accessible board labels. A frame press/release requests activation. Dragging a board frame reports
arrange preview/finish/cancel with start and current document points; dragging
the active board's bottom-right handle reports resize intent with the same
semantics. The active press snapshots its board ID and starting point, so an
owner refresh of `boardData` cannot retarget that gesture. Escape suppresses
the rest of that press. Interior and outside presses pass through to the canvas
by default, so object tools work over the board. Set `captureInterior: true`
while an artboard editing tool owns the board interior, and `createEnabled:
true` while a board-creation tool owns canvas presses: a drag that does not
start on a frame or handle previews `pendingBoard` and reports
`boardCreateRequested(documentRect)` on release (Escape cancels it).
Board backgrounds are document data painted by the renderers below objects;
the overlay only paints frames. The view does not own geometry, apply
previews, clip content, persist, export or provide undo; the model and the
document do.

<a id="layer-hierarchy"></a>
## Layer hierarchy

Layers form a tree (decision D-092). A layer's `parent` is 0 (top level) or
a layer **group**; `order` ranks it among its siblings, larger in front.
Groups hold child layers and never objects; content layers hold objects and
never child layers. Groups carry no transform, opacity or blend, so a
hierarchy edit never moves geometry: only an object's layer and paint order
(`z`) can change. Visibility and lock cascade: a layer is effectively
hidden/locked when it or any ancestor group is
(`layerEffectivelyVisible/Locked`), and painting, picking, snapping,
alignment, the selection model and tool declines all use those effective
flags. `CanvasDocument::layerRank` is the depth-first flattening of the tree
(bottom first), and the paint key is `(rank, z)`.

Every commit that touches layers validates the resulting tree: parents exist
and are groups, no cycles, depth below `maximumLayerDepth` (64 levels), no
objects on a group and at least one content layer. The active layer is
always a content layer.

| Command | Undo label | Notes |
| --- | --- | --- |
| `addLayerAt(name, group, parent, index)`, `addLayerGroup(name)` | *Add layer* / *Add layer group* | `index` counts siblings in paint order (0 = bottom), clamped |
| `moveLayer(id, parent, index)` | *Move layer* | refuses a non-group parent, itself or a descendant (cycle) |
| `renameLayer(id, name)` | *Rename layer* | |
| `groupLayers(ids, name)` | *Group layers* | siblings only; the group takes the topmost member's place |
| `ungroupLayer(id)` | *Ungroup layers* | children move to the group's parent at its place |
| `removeLayerTree(id)` | *Delete layer* / *Delete layer group* | the layer, its descendants and their objects; refuses locked content |
| `moveObjectToLayer(id, layer, index)` | *Move object to layer* / *Reorder object* | fresh paint orders for the object and the objects above it |

Each accepted command is one recorded `CanvasEdit`, so one undo restores the
whole structural change; a refusal sets `lastError` and changes nothing, and
a request that would change nothing returns false with no step. The QML
facade exposes the same commands by id (`addGroupNamed`, `moveLayerTo`,
`renameLayerById`, `groupLayersById`, `ungroupLayerById`,
`removeLayerTreeById`, `moveObjectTo`) plus `childLayerIds(parent)` and
`layerObjectIds(layer)` in paint order. `layers` lists every layer in paint
order with `parent`, `group`, `depth`, `rank`, `effectiveVisible` and
`effectiveLocked`.

<a id="layer-tree-model"></a>
## Layer tree model

`Tk.CanvasLayerTreeModel` is the `QAbstractItemModel` a layer panel reads
(decision D-094). It keeps no hierarchy of its own: after every document
change it derives rows from the layers and objects. Keys are `layer:<id>` and
`object:<id>`; children list the front-most first (the reverse of paint
order). Roles: `key`, `kind` (`group`, `layer`, `object`), `itemId`, `label`,
`detail`, `iconName`, `shown`, `locked`, `effectiveVisible`,
`effectiveLocked`, `selected`, `active`, `childCount`, `parentKey`. A change
that keeps the same rows emits `dataChanged` only, so expansion and scrolling
survive visibility, lock, selection and rename changes.

Requests: `select(key, toggle)` writes the shared `CanvasSelectionModel` (an
object row; hidden or locked objects are refused) or activates a layer;
`setShown`, `setLocked`, `rename`, `move(key, targetKey, placement)`
(`above`, `below` or `inside`; `canMove` answers without editing), `step`,
`indent`, `outdent`, `addLayer`, `groupLayers`, `ungroup` and `remove` are the
document commands above. `revealRequested(key)` fires only for selections
made elsewhere (a canvas tool, another view); the model's own selection
writes never reveal, so tree and canvas cannot drive each other in a loop.

`Tk.LayerTree` projects the model through `Tk.VirtualCollectionAdapter`
(`idRole: "key"`): only expanded rows are projected and the list builds
delegates for visible rows only. Groups start expanded and stay collapsed
once the user collapses them; content layers start collapsed. Pointer:
click selects or activates, Ctrl+click toggles, double-click renames, a drag
drops above, below or inside a row with an accent marker (invalid drops show
no marker). Keyboard: arrows move the current row, Left/Right collapse and
expand, Ctrl+Up/Down reorder, Ctrl+Right/Left indent and outdent, Ctrl+G and
Ctrl+Shift+G group and ungroup, F2 renames, Delete removes, V and L toggle
visibility and lock, Space or Return selects. A reveal expands the ancestors
and scrolls to the row without writing selection.

<a id="artboards"></a>
## Artboards

Artboards are document content (decision D-093): an ordered list of
`{id, name, rect, background, bleed}` records with stable `artboard-<n>` ids
from a saved counter that is never reused. `rect` is a finite document-space
rectangle of positive size, `background` an 8-bit sRGB colour and `bleed` a
finite non-negative margin; `exportRect` is the rectangle grown by the
bleed. List order is board order: a later board's background paints above an
earlier one's, and `artboardAt(x, y)` returns the topmost board containing a
point. Boards own no objects: arranging or resizing a board never changes
object coordinates, and objects outside a board stay in the document.

| Command | Undo label |
| --- | --- |
| `addArtboard(name, rect, background)` | *Add artboard* |
| `setArtboardRect(id, rect)` | *Move artboard* (same size) or *Resize artboard* |
| `renameArtboard`, `setArtboardBackground`, `setArtboardBleed` | *Rename artboard*, *Artboard background*, *Artboard bleed* |
| `moveArtboard(id, index)` | *Reorder artboard* |
| `removeArtboard(id)` | *Delete artboard* |

Each is one `CanvasEdit` carrying the whole before/after list, so undo, stale
edits and the shared session history behave as for guides. The renderers
paint board backgrounds after the page colour and below every object
(`CanvasDocumentAdapter::backdrops`), in both the software and scene-graph
providers, and a board change damages its old and new rectangles.
`exportArtboardSvg(id, path)` writes the SVG vector subset with the board's
export rectangle as the view box over its background. `artboards` lists the
records (`{id, name, rect, background, bleed, exportRect}`) that
`ArtboardView.boardData` reads; `contentBounds` includes every board.

<a id="artboard-model"></a>
## Artboard model

`Tk.CanvasArtboardModel` connects `Tk.ArtboardView` to a `CanvasDocument`. It
owns editor state only: `activeBoardId` (not saved, like the active layer)
and one gesture preview. `previewArrange`/`previewResize` show the moved or
resized board in `boards` (the document records with the preview applied)
without editing; `finish` commits one *Move artboard* or *Resize artboard*
step; `cancel` drops the preview. A resize moves the bottom-right corner and
keeps each side at least `minimumSize`. A board list changed elsewhere (undo,
another view) or a document reset drops a running preview instead of
committing stale geometry. `createBoard(rect)` adds a normalized board and
activates it; `removeActive()` deletes the active board.

<a id="document-guides"></a>
## Document guides

`Tk.CanvasDocument.guides` lists `{id, orientation, position}` records in
document order: the shape `GuideLayer.guideData` and `SnapModel.guides` read,
so both can bind to it directly. A vertical guide is the line x = position.
`addGuide(orientation, position)` returns a new stable id (`guide-<n>`, from a
saved counter that is never reused) or `""`; `moveGuide(id, position)` and
`removeGuide(id)` return whether an edit was made. Each accepted call is one
`CanvasEdit` (*Add guide*, *Move guide*, *Delete guide*) carrying the whole
before/after guide list, so undo, redo, stale-edit rejection and the shared
session history work as for objects. Guides do not paint document pixels.
Limits: `maximumGuides` (1024) and `maximumGuideIdLength` (64); positions must
be finite.

Guides are document content (decision D-041): saved inside the extensions
JSON under `qindatk.canvas.guides` as `{next, items}`, so the binary layout and
version are unchanged and an older reader keeps the record as opaque data.
The decoder moves the record out of the session extensions into the
document and validates it (orientation, numeric finite position, unique ids,
limits, an integral counter). Host extensions and the guide record share
`maximumExtensionBytes`; a save that would exceed it fails explicitly. A
document that never had a guide writes no record, so its bytes are unchanged.
Workspace-only guides remain possible: any host array works as `guideData`.

A ruler creates a guide through host composition: a pointer handler over the
`Tk.Ruler` maps each drag point into the `GuideLayer` and calls `previewAdd`,
then `finishAdd` when released over the layer or `cancelAdd` otherwise; the
layer's `guideAddRequested` handler calls `addGuide`. Commit guide drags from
`guideMoveFinished` and deletes from `guideDeleteRequested`.

<a id="spatial-snapping"></a>
<a id="alignment-commands"></a>
## Alignment commands

`Tk.CanvasAlignmentModel` takes an `AlignDistributePanel.operationRequested`
request through `apply(operation, selectedIds, referenceMode, keyObjectId,
spacing)`. IDs are decimal strings so QML does not round 64-bit object IDs.
The model reads transformed document bounds and commits one `CanvasEdit`;
undo restores every moved object together. Hidden, locked, missing or invalid
objects reject the whole command. A selected key object stays fixed.

Alignment uses the selection union, page, key-object or artboard bounds.
With a zero gap, distribution places centers at equal intervals; selection
mode preserves the two outer centers. With a positive gap, objects receive
that exact edge gap, starting at the first selected object (or the
page/board/key reference edge). The working fixture is
`examples/spatial-canvas/AlignWorkingScene.qml`.

The reference choices are domain data (decision D-095): `referenceOptions`
lists `{value, text, available}` for selection, key object and page, plus
`artboard` when the document has boards; it is available once `artboardId`
names a board (bind it to `CanvasArtboardModel.activeBoardId`). Bind
`AlignDistributePanel.referenceOptions` to it. With a `selectionModel`, the
model mirrors the shared selection as decimal strings in `selectedIds` for
the panel. One selected object may align to the page or a board.

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

**Constraints and frames.** Snapping and constraints are measured in
document space; only keyboard steps are given in view pixels. Shift while
moving keeps the delta on the nearest 45-degree document direction (an axis
direction zeroes the other component exactly). The selection frame is the
axis-aligned document union of the selected objects' transformed bounds; a
running gesture shows the transformed frame through `previewCorners`, and a
committed rotation leaves an axis-aligned union again (no group frame is
stored). The default pivot is the frame center. A custom `pivot` is editor
state like the active layer: it is not document content, is not saved or
undone, and resets when the selection changes.

**Numeric entry.** `setBounds(x, y, width, height)` maps the frame onto a
document rectangle: a pure translation is labelled *Move selection*, anything
else *Resize selection* (scaled from the top-left). `rotation` is the angle of
the primary (first selected, visible) object's local x axis, and
`rotateTo(degrees)` rotates the group by the difference about `pivot`. These
calls build the same world transform as the handles and commit the same one
`CanvasEdit`; they never snap. A zero extent or a non-finite value is refused
without an edit, as is any call while a pointer gesture is active (a refused
keyboard step also cancels its captured gesture). Keyboard steps and numeric
entries are atomic: bindings never observe `active` or a preview for them. `Tk.SpatialTransformEditor`
exposes X/Y, W/H, rotation and pivot fields over these calls. It reads
unedited components from the exact `selectionBounds`, follows
`previewBounds` read-only during a gesture, and snaps a refused value back.
Its fields are `spatialX`, `spatialY`, `spatialWidth`, `spatialHeight`,
`spatialRotation`, `spatialPivotX` and `spatialPivotY`.

**History.** Every finish, keyboard step and numeric entry is one
`CanvasDocument` commit. In a `DocumentSession` that history is the
application's shared `UndoService`, so the manager's undo reverts it, in order
with guide edits. The
[inspector scene](../examples/spatial-canvas/InspectorScene.qml) wires handles,
keyboard, inspector, snapping and ruler-created guides to one document.

`CanvasSelectionModel` is a QML-facing selection source. Set its `document`
to the shared `CanvasDocumentAdapter`, then set each `ToolController`'s
`selectionModel` to it. Controller gestures publish selected IDs into the
model; all bound controllers receive the resulting selection. The model
deduplicates IDs and removes deleted, hidden or locked objects on document
edits/reset. Controllers without a selection model keep their local selection.

`Tk.LayerTree` shows the document's layer tree and shares this selection
model; see [layer tree model](#layer-tree-model).

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

The canvas now has a bounded RGB display transform after sRGB compositing,
using Qt's `QColorSpace`/`QImage` conversion. It does not connect the picker to
the document color engine, infer gamut, perform proofing or transform an
arbitrary working profile. The [picker scene](../examples/spatial-canvas/ColorPickerScene.qml)
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
