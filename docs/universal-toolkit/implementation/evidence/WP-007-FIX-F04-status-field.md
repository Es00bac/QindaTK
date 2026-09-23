<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — StatusField actions and touch sizing

`StatusField` keeps its compact status readout presentation while providing a
real `T.AbstractButton` when `actionable` is true (the backward-compatible
default). `actionable: false` omits the button entirely and exposes the field
as static text. The button's accessible name follows `text`, falling back to
`tooltip` for icon-only fields; the tooltip is also the accessible description
and hover hint. Pointer, Space, Return, Enter and `Accessible.pressAction`
share the template click path and emit one `clicked` signal. Disabled fields
reject each user input path. Active focus paints the themed `statusFocusRing`.

Actionable `StatusField` uses the existing `Theme.size.control` Touch floor.
The StatusBar includes its top rule outside the 48px content target, and its
Flex stretches each field through that content box. Compact remains 22px and
requires no new theme token.

`tests/qml/tst_status_field_accessibility.qml` checks owner text updates,
static versus actionable accessibility, focus and its visible ring, pointer,
Space/Return/Enter, accessible press, disabled behavior, and Compact/Touch
status-band geometry. `tst_theme` verifies that the existing control role
resolves to 48px in Touch and 24px in Compact. The full 19-target CTest suite
and `all_qmllint` pass on Qt 6.11.1; lint has existing nonfatal warnings,
with no StatusField or StatusBar warning.

Opened Compact and Touch `qtk-preview` captures show the focus ring around the
actionable field. The dumps show `StatusBar#reviewStatusBar 0,49 420x22` and
`StatusField#reviewStatusField 4,0 65x21` in Compact; Touch shows
`StatusBar#reviewStatusBar 0,46 420x49`, a 48px inner content box, and
`StatusField#reviewStatusField 6,0 76x48`. Static “Saved” remains unfocused.
Ignored captures are `build/status-field/compact.png` and
`build/status-field/touch.png` in the StatusField worktree.

This focused slice does not qualify native screen-reader delivery or physical
touch.
