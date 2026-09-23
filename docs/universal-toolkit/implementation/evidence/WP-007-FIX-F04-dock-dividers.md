<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — DockDivider interaction slice

This focused repair gives dock seams keyboard and accessible resize
operations. It does not claim overall WP-007 acceptance or native
screen-reader qualification.

## Contract

`DockDivider` exposes the `Accessible.Splitter` role, an accessible name from
`tooltip` with a “Resize dock panels” fallback, and a direction-specific
description. A seam can enter the Tab order while enabled. Focus draws a
theme-coloured ring. Left/Right move a vertical seam; Up/Down move a
horizontal seam. Accessible increase/decrease emit the positive/negative
`Theme.space.lg` delta on the seam axis. These operations use the existing
`dragged(delta)` signal, and `DockZone` continues to apply the delta through
`DockHost` to `DockModel`. Disabled dividers reject keyboard, accessible and
direct resize requests. Divider geometry remains unchanged.

## Verification

`tests/qml/tst_dock_divider_accessibility.qml` checks both orientations,
direction-specific arrow handling, accessible increase/decrease, focus
indication, disabled controls and disabled DockHost ancestry. A DockHost case
checks that keyboard lane resizing and accessible share resizing update the
model through the existing `DockZone` callbacks. The complete Qt 6.11.1 CTest
suite passed: 19/19, including the existing vertical and horizontal synthetic
touch-target tests. `all_qmllint` exited successfully with the existing
module-wide warnings.

The compact `qtk-preview` dump reports
`DockDivider#dockDivider_left_0 80,0 4x120` and
`Rectangle#dockDivider_left_0_focus -2,-2 8x124`. The opened
`build/wp007-divider-keys/dock-divider-focus.png` shows the focus outline and
the original 4px painted seam.

## Limits

Native screen-reader delivery of splitter increase/decrease actions and
physical keyboard testing remain platform work. Keyboard/accessibility
operations emit a local seam-axis delta; for right/bottom zones, DockZone's
existing edge sign can invert the resulting lane extent direction.
