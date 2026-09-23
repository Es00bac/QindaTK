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
were initially outside this slice. Integration then reproduced a direct-move
failure: opening at y=8 and moving the control to y=100 left a 136px popup
at y=0, overlapping the control. QML `mapToItem()` did not make the placement
bindings depend on the control's x/y. An internal geometry revision now
invalidates both bindings on movement. The repaired 220×140 dump shows
`ComboBox#movingCombo 8,100 180x24` and `PopupItem#Popup 8,2 180x96`,
ending above the control. The grab was opened and shows a scrolling popup
with the selected row visible and no overlap. The focused motion QML test
passes on Qt 6.11.1. Ignored preview artifacts are
`build/wp007-combo-motion-review.{qml,png}`.

Ancestor scrolling was then audited with a nested Flickable. Scrolling
its content by 30px moved the combo from scene y=100 to y=70 but left the
82px popup at y=0, overlapping the control by 12px. The control now observes
ancestor geometry, including the Flickable content Item, and remeasures the
popup through the same internal revision. The repaired dump shows
`ComboBox#scrollCombo 8,100 180x24` inside content Item y=−30 and
`PopupItem#Popup 8,2 180x66`, ending before the control's scene y=70.
The PNG was opened and the focused scroll regression passed on Qt 6.11.1.
Ignored artifacts are `build/wp007-combo-scroll-review.{qml,png}`.

A further focused test changes `Tk.Density.textScale` from 1 to 3 while a
six-row popup is open near the bottom of the same 220×140 host. The popup
remeasures, stays above the control and scrolls its taller rows. The QML
target passes on both Qt 6.11.1 and 6.8.3. This exercises live content
scaling, not a native monitor-DPI or work-area transition.

`tests/qml/tst_combo_editing.qml` verifies the inherited editable contract:
owner `editText` assignment emits neither `accepted` nor `activated`; typed
valid text followed by Return emits one `accepted` and no selection activation;
validator-rejected and disabled submissions emit no `accepted`. Qt's
[ComboBox documentation](https://doc.qt.io/qt-6.8/qml-qtquick-controls-combobox.html)
defines `accepted` as the user text-submission signal and `activated` as a
user selection signal. The app decides whether to add novel text to the model.

Multiple monitors, work-area changes and native assistive announcements were
outside this isolated slice. The integrated [monitor work-area fixture](WP-007-FND-09-monitor.md)
subsequently covers synthetic per-screen changes; real compositor and native
announcement behavior remain WP-092 qualification. The package's final
disposition is in [WP-007 acceptance](WP-007.md).
