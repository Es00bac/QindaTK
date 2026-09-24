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
behavior, so the same samples and preset give the same pixels.

Preset fields are `diameter` (1–512 pixels), `hardness`, `spacing` (0.05–1 of
diameter), `opacity`, `pressureSize`, and `pressureOpacity` (each normalized
0–1). Mask strokes use the color's red component as coverage, so black removes
coverage while white restores it. One completed stroke becomes one undo step;
cancel or invalid input restores all changed tiles.

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
composite with `QSaveFile` atomic replacement.

The `examples/tiled-raster/Main.qml` fixture paints a real pressure/tilt stroke
over a 256 px tile boundary, masks a segment, exposes undo and exports a PNG.
This standalone raster model does not yet attach to `CanvasDocument` image
objects, persist its layer/mask model, support color-managed export, or provide
a brush preset store/UI.
