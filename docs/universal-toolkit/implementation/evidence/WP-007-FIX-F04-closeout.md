<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Current-operation closeout audit

This is a source-and-test audit of existing visible operations at integration
tip `4b08338`. FIX-F04 requires repairing `SectionHeader`/`TimeRuler` and
carrying the keyboard/accessibility audit into custom composites
([supporting scope](../supporting-scope.md#fix-f04)). This record makes no
WP-007 acceptance claim and does not claim native screen-reader qualification.

## Coverage matrix

| Operation | Evidence in the integrated source/tests | Disposition |
| --- | --- | --- |
| Section disclosure; TimeRuler scrub | `SectionHeader` shares a guarded toggle across tap, Space, Return, Enter and accessible press; `TimeRuler` has bounded keyboard and accessible scrub actions. Integrated focused tests are summarized in [WP-007 progress](WP-007-progress.md). | Covered at the QML action-contract level. Native delivery belongs to WP-092. |
| Ruler marker move/remove/add | Draggable markers have focus, Slider role, Left/Right and accessible increase/decrease; focused draggable tabs support Delete. Tests cover these paths. Empty-space tab creation is still only `MouseArea.onDoubleClicked` → `tabAdded(position)` in [`Ruler.qml`](../../../../src/qml/Ruler.qml#L381), and the focused evidence explicitly says it remains pointer-only ([marker evidence](WP-007-FIX-F04-ruler-markers.md)). Owner assignment of `markers` is silent, not a user command. | **Confirmed gap:** provide a keyboard and accessible insertion command with a defined insertion position, routed through the same `tabAdded(position)` request; test one emission, disabled behavior and owner-update silence. Keep pointer double-click as an additional gesture. |
| DockFloating resize / move | Eight resize handles expose focus, Splitter semantics, keyboard arrows and accessible increase/decrease; [`tst_dock_floating_handles.qml`](../../../../tests/qml/tst_dock_floating_handles.qml) covers those operations. Moving the floating frame is implemented only by the header `DragHandler` calling `moveFloating` in [`DockFloating.qml`](../../../../src/qml/DockFloating.qml#L126). The frame's Escape binding hides it; it is not a move alternative. | Resize covered. **Confirmed gap:** add a named, focusable move operation (for example a move mode with arrow-step movement and explicit accept/cancel), routed through `DockModel` and bounded by the host. Test keyboard and accessible entry, movement, cancellation, and disabled/constraint behavior. |
| Docked panel move, dock/undock, tab reorder and tear-off | [`DockFrame.qml`](../../../../src/qml/DockFrame.qml#L144) starts the host drag lifecycle only from header drag. [`DockTabStrip.qml`](../../../../src/qml/DockTabStrip.qml#L191) reorders and tears off only from `DragHandler`; its keyboard activation changes active tab, not placement. `DockHost` exposes callable `showPanel`/`hidePanel`/`floatPanel`/preset functions and drag hit-testing, but no focusable placement command surface ([source](../../../../src/qml/DockHost.qml#L150), [drag lifecycle](../../../../src/qml/DockHost.qml#L277)). Panel collapse/float/close icon buttons are keyboard/accessibility controls; the group menu also offers Take out of group, Float, and Close. | **Confirmed gaps:** tab reorder and choosing a dock destination have pointer-only paths. Add keyboard-accessible commands for moving/reordering the focused panel with a clear destination/order choice, using `DockModel` operations and its constraints. Existing Float/Take out menu actions cover those particular outcomes only. Test model order, destination, active panel and single accepted operation. |
| DockDivider / DockFloating resize | Divider keyboard and accessible deltas are covered by [`WP-007-FIX-F04-dock-dividers.md`](WP-007-FIX-F04-dock-dividers.md); floating resize is covered above and in [FND-07 evidence](WP-007-FND-07-touch.md). | Covered in QML fixtures. Native action delivery remains WP-092. |
| `Splitter` pane resize | [`Splitter.qml`](../../../../src/qml/Splitter.qml) wraps `QtQuick.Templates.SplitView` and supplies a custom `MouseArea` for expanded pointer/touch hit bounds. Its QML declares no keyboard or accessible resize operation. The only located `Splitter` fixture is a structural instantiation in [`tst_controls_structure.qml`](../../../../tests/qml/tst_controls_structure.qml#L78); touch coverage is in `tst_touch_targets.qml`. This does not prove the Qt template lacks such behavior. | **Unresolved contract gap:** add a focused keyboard/accessibility fixture on Qt 6.8 and 6.11 for each orientation, including focus/name/role and direction. If the template does not provide a stable documented path across supported Qt versions, implement a toolkit-owned accessible splitter action. |
| DataTable row/header operations | Table row selection has arrow/Return and accessible press; [`DataTable.qml`](../../../../src/qml/DataTable.qml#L227) headers support Space/Return/Enter/accessibility press for sort and Left/Right/accessibility increase/decrease for fixed-width resize. [`tst_data_table_headers.qml`](../../../../tests/qml/tst_data_table_headers.qml) asserts these paths and disabled guards. Header seam pointer drag is an alternate resize gesture. | Covered at the QML action-contract level. Virtual-row/native accessibility traversal remains a separate WP-092 qualification. |

## Boundary between toolkit and owner

`DockModel` remains the owner of dock arrangement. The app may define which
destinations are legal and how commands appear in its menus, but the toolkit
must supply an operable command path for its own visible panel-placement
operations; direct model/property assignment by application code is not an
equivalent user path. For the ruler, the application owns marker storage and
applies `tabAdded`, while the control owns a user operation that requests a
tab at a defined position. These additions should preserve the owner-versus-
user signal distinction in [D-023](../../../decisions.md#d-023--separate-owner-state-from-user-intent-notifications)
and [the interaction contract](../../../control-interaction.md).

WP-092 covers real assistive-technology, platform bridge, physical keyboard,
touch, monitor and scale qualification. It can qualify delivery of the
keyboard/accessibility actions above; it does not replace an operation path
that is absent from the toolkit source. The remaining interaction work listed
here should be closed or given an explicit supported-scope disposition before
using FIX-F04 as complete evidence.
