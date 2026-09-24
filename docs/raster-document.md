<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Tiled raster document

`Tk.TiledRasterDocument` is a small host-owned raster editing backend. It
stores transparent premultiplied RGBA pixels in row-major 256×256 tiles and
composites visible layers in their stable creation order. A layer's optional
mask uses sparse 8-bit grayscale tiles: missing mask tiles mean full coverage.
When enabled, mask coverage multiplies layer alpha during compositing.

## Stroke contract

Create layers with unique stable string IDs, add masks where required, then
bracket input with `beginStroke(layerId, paintMask, color, preset)`, zero or
more `sampleStroke(x, y, pressure, tiltX, tiltY)` calls and `endStroke()`. A
sample uses canvas pixel coordinates, pressure in `[0,1]`, and tilt degrees in
`[-90,90]`. The tool interpolates samples at no more than half the spacing
distance, evaluates a deterministic tilted soft elliptical stamp, and applies
pressure to size/opacity using only the preset values. It has no random
behavior, so the same samples and preset give the same pixels. The pressure
response is linear: size and opacity scale by `1 − k + k·pressure` with
`k = pressureSize` or `pressureOpacity`.

Preset fields are `diameter` (1–512 pixels), `hardness`, `spacing` (0.05–1 of
diameter), `opacity`, `pressureSize`, and `pressureOpacity` (each normalized
0–1); omitted fields take the document's saved `brushPreset`. `beginStroke`
captures its preset for the whole stroke and does not change the saved preset,
so `updateBrushPreset` during a stroke only affects later strokes. Mask
strokes use the color's red component as coverage, so black removes coverage
while white restores it. A stroke into a hidden layer or a disabled mask is
refused: hidden data is never edited, and the paint target is always the
explicit `paintMask` argument.

The undo history holds three kinds of steps: one per completed stroke (its
changed tiles), one per `setMaskEnabled` change and one per
`updateBrushPreset` change that alters the saved preset. Disabling a mask only
hides its coverage; enabling it again, or undoing the toggle, restores the
same composite. Cancel or invalid input restores all changed tiles.

## Bounds and limits

Canvas dimensions are limited to 8192 per side and 16 megapixels total, and
must be set before adding layers. At most 256 stable layers are allowed. Each
document stores at most 128 MiB of allocated layer and mask tile data. Each
stroke can touch at most 64 tiles, evaluate at most 8192 interpolated stamps,
and spend at most 16 million estimated stamp-pixel operations; exceeding a
bound cancels the stroke atomically. Input points are clamped to a bounded
512-pixel margin around the canvas. Undo retains at most 64 MiB of before/after
tile snapshots, dropping oldest edits when necessary. `composite()` returns
the actual image, `imageDataUrl` is a preview, and `exportPng(path)` writes the
composite with `QSaveFile` atomic replacement. The preview and exported PNG
are tagged sRGB. Color-managed output to other RGB profiles goes through
[canvas placement](#canvas-placement): the canvas converts its composed tiles
to the document's output ICC. Linear-light compositing, precision above 8 bits
per channel, and raster sources tagged with other color spaces are not
implemented.

## Local document file

`saveToFile(path)` atomically writes compact JSON. Its envelope identifies
`format: "qindatk.tiled-raster"`, `version: 1`, `tileSize: 256`, canvas width
and height, the active `brushPreset`, and an ordered `layers` array. Every
layer stores its stable `id`, name, visibility, mask-presence/enabled flags,
and sparse `pixels`/`mask` arrays. Each tile entry stores its row-major tile
`index` and base64 data. Pixel bytes are portable row-major premultiplied RGBA
channel bytes (R, G, B, A); mask bytes are one grayscale coverage byte per
pixel. Missing pixel tiles are transparent; missing mask tiles are full white
coverage. The QML preview and PNG export are derived and are not embedded.

`loadFromFile(path)` validates into temporary state, then replaces the live
document only after every field and tile passes. It refuses unknown versions,
non-256 tile sizes, duplicate/empty layer IDs or tile indices, unsupported
image dimensions, non-premultiplied pixels, malformed base64, incompatible
tile lengths, mask data on a layer without a mask, or bound overflows. A failed
load leaves the current pixels and layers intact. Reopening establishes a clean
undo baseline; undo history and in-progress strokes are not saved.

Exact limits: input/output JSON ≤192 MiB; canvas ≤8192 per side and ≤16
megapixels; ≤256 layers; live pixel and mask tiles ≤128 MiB; each tile ≤256²
pixels; IDs/names ≤256 characters; brush values use the ranges above. The file
is local raster document state, not a project/session or color-managed asset
container.

The `examples/tiled-raster/Main.qml` fixture paints a real pressure/tilt stroke
over a 256 px tile boundary, masks a segment, exposes undo and exports a PNG.
It can also save and reopen the versioned raster file. A preset store is the
host's: `BrushPresetView` and `BrushDynamicsEditor` present host records and
previews.

<a id="brush-previews"></a>
## Brush previews

`brushPreview(preset, color, width, height)` (C++) and
`brushPreviewDataUrl(...)` (QML) render a preset through the real brush engine:
one stroke through the vertical centre from left to right with pressure rising
linearly from 0.1 to 1 and no tilt, inset by half the diameter. Previews are
16–512 × 8–256 pixels, deterministic for the same preset, color and size, and
never touch the document. Invalid input returns a null image or an empty
string. A `pressureSize` edit is therefore visible at a known place: the
preview's column at pressure `p` is `diameter × (1 − k + k·p)` pixels tall.
Hosts feed these images to `BrushDynamicsEditor.previewSource` and
`BrushPresetView.providePreview`.

<a id="canvas-placement"></a>
## Canvas placement

A raster joins the spatial canvas as a `CanvasDocument` image object
([D-085](decisions.md#d-085--place-raster-composites-on-the-canvas-as-shared-image-assets)):

- `placeOnCanvas(canvas, x, y)` adds the flattened composite as a new tiled
  image plus an `Image` object on the canvas's active layer, with local
  rectangle `0,0 width×height` and a translation to `x, y`. Both go in one
  canvas edit: one undo step (“Place raster”). It returns the object ID, or 0
  with `error` set when the canvas refuses the edit (for example its image
  limits); nothing is added then.
- `publishToCanvas(canvas, objectId)` replaces that object's image with the
  current composite in one canvas edit (“Update raster pixels”). Tiles whose
  pixels did not change keep sharing the previous `QImage`, so a stroke in one
  tile retains one new tile. Identical pixels make no edit. It refuses while a
  stroke is active, for a non-image object, or for an image of another size.
  Objects sharing the image all update.
- `canvasToRaster(canvas, objectId, x, y)` maps a document point through the
  object's inverse transform to raster pixels (NaN when the transform is
  singular), so `RasterStrokeInput.pointMapper` can paint on a moved, scaled or
  rotated placement.

The canvas owns the placed pixels from then on: object transforms, paint order
with vector objects, hit testing, undo, persistence in the canvas file and
sessions, and ICC output conversion all use the canvas contracts. The raster
document still owns the layered pixels, masks, stroke undo and its own state
file; the canvas image is its published flattened snapshot. The two histories
stay separate: undoing a canvas publish restores the previous snapshot, and the
next publish again sends the raster's current composite. The canvas shows a
stroke when it is published, normally when the stroke commits. Canvas tiles
are untagged sRGB-encoded premultiplied ARGB32, the same encoding as the
raster composite. The canvas undo budget charges each publish the full
replaced image size, even though unchanged tiles are shared.

The `examples/raster-canvas/Main.qml` fixture places a masked, seam-crossing
raster above a vector star, rotates it, paints through the canvas view and
compares the placement-time raster with the current one by revision.

## QML pointer input

`Tk.RasterStrokeInput` is a thin input surface over a host-owned raster model.
Place it over the same displayed rectangle as the image and assign the model,
stable layer ID, brush, color and optional mask target. It scales target-local
coordinates to document pixels and forwards each sample's normalized pressure
and tilt in degrees. Each pointer gesture is one document transaction; Escape,
pointer cancellation or loss of the exclusive pointer grab cancels that edit.
Changing its model, target, layer or stroke parameters, or disabling the surface,
also cancels an active edit. Changing `brushPreset` does not: the active stroke
keeps the preset captured when it began, and the next stroke uses the new one.
By default the input maps its local rectangle proportionally onto the
document; an optional `pointMapper` function maps each local point to raster
pixels instead, for example `canvasToRaster(canvas, id, ...)` of
`CanvasView.mapToDocument(point)`. The fixture includes mouse paint in addition
to its deterministic demo stroke.

The underlying `Tk.StylusHandler` accepts Qt tablet pen/eraser samples and reads
`QTabletEvent` pressure plus x/y tilt, including devices whose `QEventPoint`
pressure is reported as zero. Mouse is the fallback at `mousePressure` (default
0.6); synthesized mouse events generated after tablet samples are filtered to
avoid double painting. Touch is intentionally ignored for palm rejection. Actual
pressure and tilt range/quality depend on the Qt platform plugin and tablet
driver. Eraser pointer events currently paint with the configured color; there
is no separate eraser mapping. This control does not add touch painting,
barrel-button tool mapping, multi-point input, or tablet calibration.
