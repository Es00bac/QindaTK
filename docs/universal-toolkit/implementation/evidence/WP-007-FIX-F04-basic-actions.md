<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Basic action controls

`Button`, `IconButton` and `Chip` now explicitly expose enabled focusability
and route `Accessible.pressAction` through the same guarded template `click()`
operation as keyboard input. Their accessible checked/checkable state follows
the owner properties. Text controls use their text as the accessible name;
icon-only controls use `tooltip`; tooltip descriptions are retained.

[`tst_button_accessibility.qml`](../../../../tests/qml/tst_button_accessibility.qml)
asserts roles, names and descriptions; actual Tab traversal; Space activation;
keyboard and accessible activation each producing one `clicked` signal and,
for checkable controls, one state/toggled change; silent owner checked
assignment; and disabled focus/action/pointer guards.

On Qt 6.11.1, the three focused QML cases passed (5 QtTest cases including
init/cleanup). `cmake --build build/wp007-buttons --target tst_qml all_qmllint
-- -j24 -l24` exited 0. Lint reported existing nonfatal warnings elsewhere in
the module. Qt 6.8 was not installed in this worktree environment and was not
run. These QML tests establish wrapper action paths, not native accessibility
backend delivery; that remains WP-092 qualification. A full QML-suite attempt
also exposed six duplicate-activation assertions in composite consumers that
provide their own `Accessible.onPressAction` alongside the shared wrapper
action (ZoomControl and DockPlacementDialog); those redundant overrides must
be removed during integration.
