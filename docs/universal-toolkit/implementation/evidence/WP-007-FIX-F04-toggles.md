<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Template toggle accessibility

**Scope:** `CheckBox`, `Switch` and `RadioButton`; one focused portion of
the current-control audit.

The template controls already accepted pointer and Space input, but their
attached `Accessible.checked` state was false after a programmatic check.
The new QML regression failed on that state assertion before the repair.
Each control now binds accessible checked, focusable and focused state to
its template state. Accessible press calls the template's `click()` while
enabled, so it follows the same toggle and exclusivity path as pointer and
keyboard input. CheckBox describes a partially checked state explicitly.

The focused QML tests cover Space and accessible press, disabled input,
programmatic state changes and notification provenance. On Qt 6.11.1,
`clicked` and `toggled` stayed silent after programmatic assignment and
each fired for the user action. The integrated `tst_qml` target passed.
Native screen-reader announcement remains unverified.
