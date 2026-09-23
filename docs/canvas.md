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

- **WP-033 session.** WP-033 (shared application framework) is still
  *planned*. The shared `DocumentSession` and `UndoService` are built around
  a `PropertyDocument`. A canvas document therefore owns its own history
  behind the adapter. A later session adapter will bind
  `commit`/`undo`/`redo` to the shared services and persistence without
  changing any caller.
- **GestureRouter.** `ToolController` arbitrates gestures for its canvases
  itself. The router's Edit intent needs a PropertyDocument
  `EditTransaction`, so bridging canvas edits into it is part of the same
  seam.
- **WP-041.** Snapping, selection overlays, transform handles and guides
  belong to WP-041. This package exposes the contracts that work needs:
  `CanvasToolPreview`, `ToolController.selection`, `HitTestService`,
  `CanvasSurface::mapFromDocument` and the `canvasOverlaySlot` item. It
  implements none of them.

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

Built-in tools: `select` (click, shift-add, drag-move, marquee), `pan`,
`rectangle`, `ellipse` and `pen`. The pen keeps at most 4096 samples, and
pressure scales stroke width. With an eraser device the pen removes what it
crosses in one edit. `ToolController.tools` uses the `Tk.ToolShelf` model
keys (`id`, `label`, `iconName`, `tooltip`). It is a C++ list, so pass
`Array.from(controller.tools)`, because ToolShelf checks `Array.isArray`.

<a id="transient-preview"></a>
### Transient preview

`CanvasToolPreview {kind: none|marquee|rect|ellipse|polyline|move, rect,
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

<a id="viewport"></a>
## Relationship to Viewport

`Tk.Viewport` is unchanged: a scrolled, zoomable page with no rotation,
tools, picking or render boundary. Keep it for page-like content (documents,
scores). Use `Tk.CanvasView` when you need rotation, synchronized views,
picking or large spatial documents.
