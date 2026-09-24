<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Vector authoring

`StrokeStyleEditor`, `FillStyleEditor`, `ShapePalette` and `AppearanceStack`
expose host-owned vector appearance requests. Their full descriptors retain
unsupported settings so an application can reopen a document without dropping
effects it cannot currently render. The reference fixture is
`examples/vector-authoring/Main.qml`.

`Tk.CanvasStyleModel` connects supported appearance fields
to a `CanvasDocumentAdapter`. `apply(selectedIds, style)` accepts decimal-string
object IDs and any combination of `fill`, `stroke`, `strokeWidth`, `dash`,
`cap` (`butt`/`round`/`square`), `join` (`miter`/`round`/`bevel`) and
`fillRule` (`evenodd`/`nonzero`) keys. Dash values are positive, even-count
stroke-width multiples, matching the `StrokeStyleEditor` descriptor.
Colours may be QML colours or colour strings; null clears a channel. A style
command changes every visible, unlocked selected object in one `CanvasEdit`,
so one undo restores the complete selection. Invalid IDs, locked/hidden
objects, malformed values and unsupported properties reject the whole edit.
The canvas native codec persists the supported fields. The painter honors
them, and the scene-graph path uploads a composited tile for paths or custom
strokes to keep output consistent.

The stroke panel's `color`, `width`, `dash`, `cap` and `join` map to the matching
canvas fields after the host converts its chosen unit to document units. The
solid fill panel's
`settings.solid.color` maps to `fill`; kind `none` clears it. The reference
canvas has editable cubic paths: each node stores a local-space anchor plus
absolute incoming and outgoing controls. `CanvasDocument.addPath` takes
`[{x,y,inX,inY,outX,outY}, ...]`, with omitted controls collapsing to the
anchor. `movePathPart(id,index,"anchor"|"in"|"out",x,y)` commits one undoable
edit; moving an anchor preserves both handle offsets. `insertPathNode`,
`removePathNode` and `setPathClosed` are also undoable. A closed path needs
three nodes and an open path two. `pathNodes(id)` returns the current values.
`Tk.PathEditor` overlays a `CanvasView`, maps the selected path through its
object transform and view pose, previews node/handle drags, then calls the
document edit once on release. Escape cancels the preview.

`CanvasDocument.exportSvg(filePath)` atomically writes the supported SVG 1.1
subset: page background, rect, ellipse, polyline, cubic path, object transform,
flat RGBA fill/stroke, fill rule, cap, join and dash pattern. It reports an
explicit unsupported error for image objects, typed ICC/gradient fills and
non-sRGB output ICC, so export cannot silently lose them. A zero-width canvas
hairline exports as one SVG document unit at nominal zoom. Native `qtkc`
version 3 saves path nodes and stroke styles; the reader accepts versions 1
and 2. The session schema ID is unchanged.

Still outside this reference backend: multi-contour paths and boolean
operations, stroke inside/outside alignment, parametric shapes beyond rect
and ellipse, pattern fills, appearance-stack effects, and vector clipboard.
The panels keep these descriptors for a richer host and the reference model
rejects unsupported style keys rather than dropping them.
