<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 FND-09/FND-10 — ComboBox popup and selection slice

The selected ComboBox delegate now reports `Accessible.selected` alongside
its existing checkmark. Popup rows grow enough for scaled text. Before
opening, the popup compares the room above and below the control, uses the
side with more room, caps its height to that space and scrolls long lists.

Focused QML tests cover Space to open, Down/Return selection, Escape and
focus return, pointer selection, a silent owner `currentIndex` assignment,
selected delegate state and a 12-row list at 3× text in a 220×140 host.
The worker's Qt 6.11.1 `tst_qml` and `all_qmllint` passed. Its preview dump
showed `ComboBox#comboAudit 8,100 180x24`, `PopupItem#Popup 8,16 180x82`
and `ListView 2,2 176x78`; the scrollbar was present. Both the normal and
3× text PNGs were opened, and the popup fit above the bottom-anchored
control. The integrated two-version gate is tracked in
[WP-007 progress](WP-007-progress.md).

This covers the small-host scene. Dynamic movement of an already open combo,
multiple monitors, work-area changes and native assistive announcements
remain outside this slice. FND-09 and WP-007 remain active.
