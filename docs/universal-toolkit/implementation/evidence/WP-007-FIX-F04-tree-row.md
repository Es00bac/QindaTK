<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — TreeRow interaction slice

**Scope:** TreeRow's keyboard focus, expansion action, accessibility state, and
disabled interaction semantics. This covers one composite and does not close
the selection-control audit for Segmented, PillSwitcher, or TabStrip.

Before the change, TreeRow inherited `ListRow`'s `Qt.NoFocus` policy. Its
chevron handled pointer taps only, and its accessibility role/name remained
the generic list-row values; no accessible press action exposed expansion.
The added regression tests assert a TreeItem role and name, focus reporting,
Space/Enter and accessible expansion, disabled action blocking, silent
programmatic `expanded` assignment, and F2 rename behavior. Those role,
focus, and activation assertions fail against the prior TreeRow implementation.

Expandable or editable rows now join the tab order. Space and Enter toggle
expandable rows; F2 starts inline rename. The accessible press action follows
the same expansion/edit operation. `toggle()` and `startEditing()` guard
disabled state, while direct `expanded` assignment remains a silent owner
update. The focused visual ring is shown only for enabled visual focus.

**Verification:** `cmake --build build/wp007-selection --target tst_qml`
completed. `ctest --test-dir build/wp007-selection -R '^tst_qml$'
--output-on-failure` passed (the complete QML test target, 0 failures). The first full-suite
run caught an Enter bubbling regression in inline rename; the key contract was
adjusted to reserve rename for F2, and the suite passed on rerun.

Integration review caught a visual-focus gap: the added ring used
`visualFocus`, which stayed false when the row received active focus in an
offscreen preview. It now uses `activeFocus` and has stable objectName
`treeFocusRing`. The QML regression asserts ring visibility after focus and
invisibility when disabled. Integrated Qt 6.11.1 `tst_qml` passed. A
480×160 offscreen dump shows `TreeRow 0,55 480x22` with
`Rectangle#treeFocusRing -2,-2 484x26` visible, and the opened
`build/wp007-tree-review-fixed.png` capture shows both horizontal focus edges.
