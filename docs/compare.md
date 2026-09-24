<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Image comparison

`Tk.CompareView` displays two equal-sized source images through one zoom and
pan transform. Split shows the before image to the left of the divider and
after to the right. Blink switches the full view between sources. Difference
shows absolute per-channel differences after both images are converted to
sRGB; it is a visual change locator, not a calibrated proof or perceptual
distance metric. The images and rendered viewport are bounded to 16 million
pixels each.

QML hosts set `beforeSource` and `afterSource` to local image URLs or bounded
PNG data URLs, including `TiledRasterDocument.imageDataUrl`. C++ hosts
can call `ImageCompareItem::setImages(QImage, QImage)` with decoded images.
The item reports load or dimension failures in `lastError`. No image is
modified by a comparison. The application owns source loading, edits,
revision history and export policy.

The [fixture](../examples/image-compare/Main.qml) provides two local SVG
images with one changed object. Its split, blink and difference controls
demonstrate the synchronized presentation.
