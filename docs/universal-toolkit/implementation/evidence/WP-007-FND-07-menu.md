<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-07 — Menu width at large text

A 480×320 Touch, 3× text, high-contrast-dark popup exposed a 160px width
despite a 323px natural “Open recent project” item. The menu's ListView
reported no natural content width because each item uses a growing Flex label
with zero basis. The opened baseline grab showed “Open r…” and “Prefer…” even
though the window had available width.

`MenuItem` now reports its natural label, icon, shortcut and submenu arrow
width. `Menu` measures its widest item, then caps the popup to the host window
and anchor screen's available width. Overlong text still elides at the window
limit; its full `Accessible.name` remains available. Template keyboard,
cascading and scroll behavior stay intact.

The opened dark and light replacement grabs show “Open recent project” and
“Preferences” without elision. The dump reports `PopupItem#Menu 3,8 474x246`,
`MenuItem 0,0 468x48`, and `Label#menuItemText 0,4 456x40`; all stay inside
the 480×320 host. The focused `tst_menu_large_text.qml` verifies both high
contrast presets, the Touch row height, expanded and capped width, visible
first label, full accessible name for an overlong label, and Escape closure.
The ignored captures are `/tmp/qindatk-menu-touch-large-fixed.png` and
`/tmp/qindatk-menu-touch-large-light.png` in this environment.

Native screen-reader delivery, physical touch and compositor window placement
remain WP-092 qualifications.
