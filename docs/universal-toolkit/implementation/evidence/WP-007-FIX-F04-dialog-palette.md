<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Dialog and command palette interaction slice

This is focused control evidence, not acceptance of all FIX-F04, FND-09 or
WP-007 requirements. Existing [popup focus evidence](WP-007-FND-09-focus.md)
covers dialog/palette focus entry, Escape return, host bounds and palette
scroll reachability.

`CommandPalette` command rows now expose ListItem roles, labels and selected
state. The command list exposes a List role and name. Optional command
`enabled` defaults true; disabled commands remain visible, are skipped during
arrow navigation, and cannot activate by click, Return/Enter or accessible
press. The accepted command emits one `activated(id)` and closes the palette;
changing the owner supplied command list remains silent. The root id is named
`commandPalette` because the generic `palette` id collided with Qt's inherited
`palette` property inside the ListView delegate. Delegates set the current row
and call the popup's guarded activation operation.

`Dialog` content exposes a Dialog role with `title` as its accessible name,
falling back to “Dialog”. A focused regression confirms `primaryEnabled: false`
disables its primary button and blocks both Enter keys and accessible press,
while Escape still closes the popup without emitting `accepted`. The existing
focus test continues to confirm the primary action receives focus when
enabled and Escape restores the opener.

Verification on Qt 6.11.1: the focused dialog/palette accessibility tests
passed, then `ctest --test-dir build/wp007-dialog-palette -R '^tst_qml$'
--output-on-failure` passed (1/1 target). The live system `MAKEOPTS` read was
`-j24 -l24`; the direct Ninja build used `-j24 -l24`. This fixture does not
establish native screen-reader delivery or monitor work-area behavior.
