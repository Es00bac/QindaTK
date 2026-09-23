<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — DockTabStrip interaction slice

This focused repair covers the custom dock tab composite. It does not claim
overall WP-007 acceptance or native screen-reader qualification.

## Contract

`DockTabStrip` and its entries expose PageTabList/PageTab roles, a dock-panel
list name, panel-title names, selected/focused state, and an accessible press
action. A delegate is template-backed and keyboard focusable. Left/Right wraps
through tabs; Home/End moves to the first/last tab. Space, Return/Enter, and
accessible press activate the focused tab. All accepted user paths call
`DockModel.activateInGroup`; `DockModel` remains the authority for active tab
and order. The strip emits `tabActivated(panelId)` only when a different panel
is accepted. Programmatic model selection only changes its selected state.
Disabling the strip disables tab focus and actions. Focus is drawn by the
theme-coloured `dockTabFocus_<panelId>` ring. `tabItem(index)` exposes a
delegate for host focus management.

## Verification

The focused QtQuickTest fixture exercises accessible roles/names/selection,
silent programmatic selection, accessible press, Left navigation, Space
activation, pointer activation, disabled focusability/action gating, and a
visible focus ring. On Qt 6.11.1, all eight existing dock tests plus the new
test passed (10 including init/cleanup).

`qtk-preview` dumped and grabbed a 480×160 direct tab-strip fixture after
focusing its selected Channels tab. The dump showed
`TabButton#dockTab_channels 42,0 53x22 "Channels"` and a visible
`Rectangle#dockTabFocus_channels -2,-2 57x26`. The opened capture showed the
focus outline around Channels.

## Limits

This slice does not verify platform screen-reader delivery, native assistive
technology traversal, or physical pointer/keyboard devices. Dock tab order
and active state continue to come from the DockModel.
