<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-043 color picker presentation slice

`Tk.ColorPicker` provides a compact saturation/brightness square, full hue
rail and optional alpha rail. It accepts the host's current sRGB preview and
emits continuous `previewRequested(color)` requests, one `editFinished(color)`
request on pointer release or keyboard adjustment, and `editCanceled(color)`
when Escape cancels a live pointer gesture. Keyboard arrows and the accessible
increase/decrease actions use the same preview and finish requests. The
control leaves canonical colour, profile conversion, transactions, undo and
persistence to the host.

The focused `tst_color_picker.qml` run passed **6/6 cases** on Qt 6.11.1 after
Sol integration review. It
covers preview/finish without mutating owner state, Escape cancellation,
hue/alpha keyboard actions, silent owner updates and hiding the alpha rail.
Its green/blue regression checks the normalized hue marker position and
verifies saturation/value, alpha and keyboard hue requests preserve or adjust
the expected normalized hue. Review also made hue nudges wrap across red and
made the implicit height shrink when the alpha rail is hidden; focused tests
cover both behaviors.
The example passes `qtk-preview --check`. At 900×600, the compact dump reports
`Item#colorPickerSceneFrame 12,54 200x248`,
`Item#colorPickerSaturationValue 0,0 200x200`,
`Item#colorPickerHue 0,204 200x20` and
`Item#colorPickerAlpha 0,228 200x20`. Comfortable density reports a
240×240 square and 24px rails inside a 240×298 frame. Both captures were
opened and inspected:
[compact](WP-043-colorpicker-compact.png) ·
[comfortable](WP-043-colorpicker-comfortable.png).

This UI presents sRGB preview values and does not claim managed display
output. The canvas currently implements sRGB-to-sRGB only; color-profile
transforms, proofing and color-managed GPU/software agreement remain open
backend work.
