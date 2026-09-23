<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Current-operation closeout audit

This began as a source-and-test audit of existing visible operations at
integration tip `4b08338`. FIX-F04 requires repairing `SectionHeader`/`TimeRuler`
and carrying the keyboard/accessibility audit into custom composites
([supporting scope](../supporting-scope.md#fix-f04)). Subsequent focused
repairs close the operation gaps below at the QML contract level. This record
does not claim native screen-reader qualification.

## Coverage matrix

| Operation | Evidence in the integrated source/tests | Disposition |
| --- | --- | --- |
| Section disclosure; TimeRuler scrub | `SectionHeader` shares a guarded toggle across tap, Space, Return, Enter and accessible press; `TimeRuler` has bounded keyboard and accessible scrub actions. Integrated focused tests are summarized in [WP-007 progress](WP-007-progress.md). | Covered at the QML action-contract level. Native delivery belongs to WP-092. |
| Ruler marker move/remove/add | Draggable markers have focus, Slider role, Left/Right and accessible increase/decrease; focused draggable tabs support Delete. A separate named insertion-position Slider starts at the ruler midpoint, adjusts by `space.unit`, and sends its precise current position through `tabAdded(position)` on Enter or accessible press. Tests cover Tab focus, adjustment, one request per action, disabled behavior, focus indication and silent owner assignment. Pointer double-click remains available. | Covered at the QML action-contract level. Native action delivery remains WP-092 qualification. |
| DockFloating resize / move | Eight resize handles expose focus, Splitter semantics, keyboard arrows and accessible increase/decrease; [`tst_dock_floating_handles.qml`](../../../../tests/qml/tst_dock_floating_handles.qml) covers resize. A named header Place button and Shift+F10 now open a host-owned [placement dialog](WP-007-FIX-F04-dock-placement.md) with bounded X/Y NumberFields, explicit Apply/Cancel, and a single `DockModel.moveFloating` commit. | Covered at the QML action-contract level. Cancel leaves the model unchanged; disabled, hidden and stale source guards are tested. Native delivery remains WP-092. |
| Docked panel move, dock/undock, tab reorder and tear-off | Header Place buttons, focused-tab Shift+F10 and the tab context menu open the same [placement dialog](WP-007-FIX-F04-dock-placement.md). It offers permitted zones, before/after/adjacent-lane destinations, grouping, and explicit tab positions; accepted commands call `DockModel` once and restore focus after frame replacement. [`tst_dock_placement.qml`](../../../../tests/qml/tst_dock_placement.qml) checks destination, order, active tab and one acceptance emission. Existing pointer drag and float/take-out controls remain alternate gestures. | Covered at the QML action-contract level. Native delivery remains WP-092. |
| DockDivider / DockFloating resize | Divider keyboard and accessible deltas are covered by [`WP-007-FIX-F04-dock-dividers.md`](WP-007-FIX-F04-dock-dividers.md); floating resize is covered above and in [FND-07 evidence](WP-007-FND-07-touch.md). | Covered in QML fixtures. Native action delivery remains WP-092. |
| `Splitter` pane resize | [`Splitter.qml`](../../../../src/qml/Splitter.qml) gives every template seam an accessible Splitter role, stable per-pane name, keyboard arrow and accessible increase/decrease paths, and a visible focus ring. The same resize writes the documented `SplitView.preferredWidth/Height` attachment and respects both adjacent panes' minimum/maximum. [`tst_splitter_targets.qml`](../../../../tests/qml/tst_splitter_targets.qml) exercises both orientations, a later seam in a three-pane layout, disabled behavior, bounds and pointer/touch regression. [Focused evidence](WP-007-FIX-F04-splitter.md) records geometry and both Qt-version QML suites. | Covered at the QML action-contract level. Native delivery remains WP-092. |
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
that is absent from the toolkit source. The source-level operation paths listed here are closed by the focused
repairs. The integrated dual-version gate and WP-007 foundation acceptance
are tracked separately in [WP-007 progress](WP-007-progress.md).
