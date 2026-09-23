<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-09 — Popup focus and host bounds

**Scope:** a focused popup interaction and sizing repair. This is partial
FND-09 evidence, not acceptance of WP-007.

`Tk.Popover` now takes focus when opened, so Escape closes it and Qt returns
focus to the opener. `Tk.CommandPalette` constrains its height to the space
below its 12%-height top position and keeps its result ListView scrollable.
The small-host test initially found that the palette extended past the
host's bottom; after the first clamp, a 2× text preview still exposed a
four-pixel overflow in a 220×300 host. The final clamp accounts for the
actual top offset.

`tests/qml/tst_popup_focus.qml` checks focus entry and return through Menu,
Dialog, Popover and CommandPalette; Escape closure; Dialog and palette
bounds in a 220×140 host; and keyboard reachability of the last command
when the list scrolls. A further 220×300, 2× text case checks that the
palette remains inside the host and its list still scrolls. The final
preview dump reports `PopupItem#CommandPalette 16,36 188x248`,
`TextField#paletteInput 0,0 180x36` and
`ListView#paletteList 0,45 188x203`. The popup bottom is y=284 inside
the 300px host. Software and AMD OpenGL RHI captures were opened; the
search input and visible result rows fit within the popup border.

## Nested Menu in a constrained host

The 220×140 regression opens a root Menu with a child submenu by keyboard,
checks that the root and submenu remain within the host, presses Escape and
verifies focus returns to the parent submenu row, then navigates to the last
root row and checks that it is scrolled into view before Escape returns focus
to the original opener. The first reachability assertion failed: `currentIndex`
reached the final row while the ListView's `contentY` remained at zero. The
Menu's ListView now calls `positionViewAtIndex(currentIndex, ListView.Contain)`
when its current row changes. The regression passes with that change.

On the rebased integration source, the QtQuick suite failed at the new
`contentY > 0` assertion after keyboard selection reached the final root row.
After the Menu change, `cmake --build build/wp007-popup-audit` and
`ctest --test-dir build/wp007-popup-audit -R '^tst_qml$' --output-on-failure`
passed. `all_qmllint` completed with existing unqualified-access warnings.

This covers the exercised nested-menu case and is not full FND-09 or WP-007
acceptance. In particular, monitor work-area changes and native popup behavior
remain outside this fixture.

The integrated full CTest suite passes 19/19 on Qt 6.11.1 and Qt 6.8.3;
both Qt versions' `all_qmllint` targets exit 0. Builds used the live
system `MAKEOPTS` value `-j24 -l24` without an override. The ignored
captures are `build/palette-text2-small-fixed.png` and
`build/palette-text2-small-gpu.png` in the integration worktree.

Popovers larger than their hosts, work-area changes across monitors, and
native popup placement remain FND-09 work.
