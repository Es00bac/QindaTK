<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 F04 — ZoomControl readout

`ZoomControl`'s percentage readout is now a `Tk.Button` with the accessible
name “Reset zoom”. It supports normal button focus, Space and Return/Enter,
pointer activation and `Accessible.pressAction`. Those actions share
`reset()`. The zoom in/out methods and reset return without user notifications
when the composite is disabled. Assigning `value` remains an owner update and
does not emit `valueModified` or `resetRequested`.

An enabled reset activation always emits `resetRequested`, even at 100%,
because that signal reports a command. `valueModified` emits only when the
zoom value changes. At a non-default value the reset action therefore sends
one command request and one changed-value notification.

## Verification

The focused case `ControlsStructure::test_zoom_control_steps` passed in
`ctest --test-dir build/wp007-zoom -R qml --output-on-failure` on Qt 6.11.1.
It exercises zoom in/out, keyboard Return, Enter and Space, accessible press,
the accessible name/description and keyboard visual-focus state, unchanged
reset at 100%, owner synchronization, and disabled user operations with
signal counts and final values. The complete `tst_qml` target passed.

`qtk-preview` reported `ZoomControl 72,28 116x24`; its children were
`IconButton#zoomOut 0,0 24x24`, `Button#zoomReadout 26,0 64x24 "200%"`,
and `IconButton#zoomIn 92,0 24x24`. The compact 260×80 capture
`build/wp007-zoom/zoom-focus.png` was opened and inspected: all three actions
and the 200% readout are visible and centered. The offscreen preview did not
show active focus, so focus-ring visibility is established by the button's
existing themed visual-focus path and keyboard interaction test, not by this
capture.

Integration kept the readout's actual text in the toolkit's monospaced
`Tk.Mono` content item and routed accessible press through the button's
`click()` path. The focused QML test now confirms `clicked` fires once for
Return, Space, Enter and accessible press, and never fires while disabled.
The integrated 260×80 preview was dumped, grabbed and opened:
`ZoomControl#reviewZoom 72,28 97x24` contains
`Button#zoomReadout 26,0 45x24 "200%"` and `Mono 8,0 29x24 "200%"`;
its focus rectangle is `-1,-1 47x26` and visible. The PNG shows that ring
around the readable monospaced percentage. Ignored artifacts are
`build/wp007-zoom-review.{qml,png}`.

This is focused control evidence only; no overall WP-007 acceptance is
claimed.
