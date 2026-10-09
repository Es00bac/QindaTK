<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Raster editing surfaces

`import QindaTK.Raster as Raster` supplies `Raster.RasterView`, a native
`Tk.Viewport` with a tiled transparency background, display image, selection
overlay, and `Tk.StylusHandler`. It requires the existing `QindaTK` module;
the pure QML companion can be installed separately as `dev-libs/qindatk-raster`.

The host owns the editing engine. Set `source` to an image URL (including a
native `QQuickImageProvider`) and `documentSize` to the original pixel size.
A reduced display image stretches into that size; zoom and input remain in
original document coordinates. `strokeBegan`, `strokeMoved`, and `strokeEnded`
forward the complete toolkit sample, including pressure, tilt, rotation, and
pointer type. `strokeCanceled` means the host must discard its pending stroke.
Middle-button pan, Ctrl+wheel zoom, fit modes, touch pinch, and keyboard zoom
are inherited from `Tk.Viewport`. `paintingEnabled` gates editing independently
of navigation. `selectionRect` uses document coordinates.
`displayRect` positions a rendered tile in document coordinates; its default
is the whole document. Hosts can render only the visible rectangle at the
current display resolution without allocating a full-resolution texture.
Disabling painting enables left-button pan; middle-button pan stays available.

Stable names: `rasterSurface`, `rasterImage`, `rasterSelection`, `rasterStylus`,
`rasterCheckerboard`. Theme roles control all chrome and checker colours.
Checkerboard storage is one tiny repeated texture regardless of canvas size.

The module lives in `modules/QindaTK/Raster`. For source-tree verification add
`modules` to `QML_IMPORT_PATH`; normal installs put it in Qt's QML import root.

Run the focused input/geometry tests with `qmltestrunner -input tests/raster
-import modules -platform offscreen` and `QT_QUICK_BACKEND=software`.
