<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Colour swatch interaction slice

**Scope:** current `ColorSwatches` cells. This is one composite audit slice;
it does not close F04 or the touch-target review.

The cells already accepted tap, Space and Enter and had accessible names,
but exposed no accessible press action. All four activation paths now call
`selectIndex(index)`, which rejects out-of-range indices and disabled
parents. Each cell reports whether it is focusable and focused. Updating
`current` from the owner remains silent; an accepted user operation emits
`selected(color)` exactly once. The custom button continues to use its
template button behavior.

The QML regression drives keyboard and accessible press, verifies the
selected-state report after a programmatic `current` update, and verifies
disabled keyboard, assistive and pointer paths emit nothing. The integrated
Qt 6.11.1 `tst_qml` target passed with this test and the scrolling Popover
regression. Native screen-reader delivery has not been exercised.
