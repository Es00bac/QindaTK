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

The integrated full CTest suite passes 19/19 on Qt 6.11.1 and Qt 6.8.3;
both Qt versions' `all_qmllint` targets exit 0. Builds used the live
system `MAKEOPTS` value `-j24 -l24` without an override. The ignored
captures are `build/palette-text2-small-fixed.png` and
`build/palette-text2-small-gpu.png` in the integration worktree.

Nested menu focus and overflow, popovers larger than their hosts, work-area
changes across monitors, and native popup placement remain FND-09 work.
