<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — ColorButton action path

`Tk.ColorButton` uses the `T.AbstractButton` click operation to open its
swatch popover for pointer, Space and `Accessible.pressAction`. The accessible
name comes from `tooltip` or the fallback “Colour”; focusability follows
`enabled`. This keeps a disabled button from opening the popover through a
custom assistive action.

`tests/qml/tst_controls_office.qml` checks that owner `color` assignment emits
no `colorSelected`, keyboard and accessible press open the popover, an
accessible swatch press emits one selection and updates the colour, and
disabled pointer, keyboard and accessible operations do nothing. The
integrated Qt 6.11.1 `tst_qml` target passed after the repair. This fixture
does not establish native screen-reader delivery or certify all F04 controls.
