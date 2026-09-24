<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Vector authoring

`StrokeStyleEditor`, `FillStyleEditor`, `ShapePalette` and `AppearanceStack`
expose host-owned vector appearance requests. Their full descriptors retain
unsupported settings so an application can reopen a document without dropping
effects it cannot currently render. The reference fixture is
`examples/vector-authoring/Main.qml`.

`Tk.CanvasStyleModel` connects the currently supported solid appearance fields
to a `CanvasDocumentAdapter`. `apply(selectedIds, style)` accepts decimal-string
object IDs and any combination of `fill`, `stroke` and `strokeWidth` keys.
Colours may be QML colours or colour strings; null clears a channel. A style
command changes every visible, unlocked selected object in one `CanvasEdit`,
so one undo restores the complete selection. Invalid IDs, locked/hidden
objects, malformed values and unsupported properties reject the whole edit.
The existing canvas native codec persists these three fields.

The stroke panel's `color` and `width` map to `stroke` and `strokeWidth` after
the host converts its chosen unit to document units. The solid fill panel's
`settings.solid.color` maps to `fill`; kind `none` clears it. The reference
canvas does not yet encode dash, cap, join, stroke alignment, gradients,
patterns, effect stacks or editable paths. Those controls preserve their
descriptors for hosts with richer vector documents.
