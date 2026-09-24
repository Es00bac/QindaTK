<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Image comparison

`Tk.CompareView` displays two source images of the same region through one
zoom and pan transform. The images may have different resolutions (for
example a full render against a half-size preview) as long as they show the
same region: the smaller one must be the larger one scaled by a common factor,
each side rounded to the nearest pixel (`|w·H − h·W| ≤ (W + H)/2` for the
larger `W×H`). Both are drawn into one target rectangle, so linked split and
blink views show the same document region. Split shows the before image to the left of the divider and
after to the right. Blink switches the full view between sources. Difference
shows absolute per-channel differences after both images are converted to
sRGB, at the finer resolution after smoothly resampling the other image; it is
a visual change locator, not a calibrated proof or perceptual distance
metric. The images and rendered viewport are bounded to 16 million
pixels each.

Drag in the viewport to move the split divider, click the blink view to swap
sources, or drag the difference view to pan. Middle-button drag pans in any
mode, and the mouse wheel changes the shared zoom.

QML hosts set `beforeSource` and `afterSource` to local image URLs or bounded
PNG data URLs, including `TiledRasterDocument.imageDataUrl`. C++ hosts
can call `ImageCompareItem::setImages(QImage, QImage)` with decoded images.
The item reports load or region failures in `lastError`. No image is
modified by a comparison.

## Revision-tracked pairs

A host that renders previews for document revisions sets `revisionKey` to the
pair it expects (for example `"12:15"`) and delivers both images at once with
`setSourcePair(before, after, key)` (C++: `setImagePair`). A delivery whose key
differs from `revisionKey` is refused and changes nothing, so a late preview of
an older revision can never be shown beside a newer one. Changing
`revisionKey` keeps the displayed pair: `displayedRevisionKey` names it and
`stale` is true until the matching pair arrives, and the status line reports
it. Setting `beforeSource` or `afterSource` individually is untracked
(`displayedRevisionKey` is empty). The application owns source loading, edits,
revision history and export policy.

The [fixture](../examples/image-compare/Main.qml) provides two local SVG
images with one changed object. Its split, blink and difference controls
demonstrate the synchronized presentation.
