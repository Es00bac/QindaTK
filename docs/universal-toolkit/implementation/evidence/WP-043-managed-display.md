<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-043 — Bounded managed canvas display

**State: display slice; WP-043 remains active.** Canvas QColor objects are
authored in sRGB. For a valid RGB `QColorSpace` display target, the software
painter composites a tile in sRGB, then Qt converts its complete premultiplied
image to the target profile. Both software and scene-graph backends present
that encoded raster; the identity sRGB scene-graph geometry path remains.
Changing the color context clears all old-profile tiles. Unsupported working
or display spaces report `RenderSurfaceAdapter.colorError` and present an
empty tile frame. The policy is [D-051](../../../decisions-application.md#d-051--convert-composed-srgb-canvas-tiles-for-explicit-rgb-displays).

The focused Qt 6.11.1 `tst_canvas_display` and existing `tst_canvas_render`
CTest targets passed **2/2**. The separate offscreen RHI registration passed
**1/1** with an actual scene-graph readback. A page authored at sRGB 128/255
converted to linear-sRGB byte value **55** at the center; switching from
scene graph to software kept the same value. The two 300×300 inspected grabs
were pixel-identical (maximum absolute RGBA difference 0):

- [scene graph capture](WP-043-managed-display-scenegraph.png)
- [software capture](WP-043-managed-display-software.png)

The focused test also covers the unsupported linear working-space error and
proves the old cache and frame items are cleared. `tst_canvas_render.cpp` was
split to keep both test sources under 500 nonblank lines; its prior software
pixel fixture moved unchanged into `tst_canvas_display.cpp`.

**Limits:** This uses Qt's `QColorSpace`/`QImage` transform, separate from the
ICC `ColorEngine` API. It supports the current sRGB-authored QColor document
and Qt-representable RGB display targets. There is no proofing, gamut result,
CMYK, arbitrary working-space compositing, display-device discovery, typed
picker binding or document transaction integration. The managed scene-graph
path uploads converted CPU rasters; a profile-aware geometry shader has not
been implemented. Qt 6.8 integration verification is pending.
