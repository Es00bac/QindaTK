<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 F04 — ListRow keyboard and accessible action

`ListRow` can receive Tab focus and shows a theme focus ring. Its accessible
ListItem reports the row name, optional secondary description and selected
state. A tooltip supplies a name for an icon-only row. Space and the
accessible press action use the template click operation; assigning
`selected` from the owner does not emit `clicked`. Disabled rows reject
keyboard, assistive and right-click actions.

The focused `tst_list_row_accessibility.qml` fixture checks identity,
selection, focus ring visibility, action signals and disabled behavior.
The integration Qt 6.11.1 `tst_qml` target passed. A 320×120 preview was
dumped, grabbed and opened: `ListRow#reviewListRow 24,24 260x28` contains
`Rectangle#rowFocusRing -2,-2 264x32`. The PNG shows the selected row text
and a visible cyan focus outline. Ignored artifacts are
`build/wp007-list-row-review.{qml,png}`.
The WP-007 progress record holds the cross-version gate. This does not
assert native screen-reader delivery or whole-list selection policy.
