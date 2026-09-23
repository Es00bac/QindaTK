<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-006 nested scrolling and focus reveal

This is FND-05 integration evidence, not acceptance of all WP-006 work.
The policy is D-021; the executable event fixture is
`tests/qml/tst_scroll_nested.qml`.

## Baseline failures and repair

The original two-Scroll wheel fixture passed: the inner viewport moved
under the pointer, then Qt delivered a wheel event to the outer Scroll at
the inner hard edge. Keyboard Down while the inner Scroll had focus failed
because Scroll had no key handler. Focusing a button near the bottom of
the inner content also failed to reveal it through both viewports.

`Tk.Scroll` now handles row-step arrow keys, Page Up/Down and Home/End,
accepting a key only when its own viewport moves. An unaccepted edge key
reaches its ancestor. It uses `Flickable.StopAtBounds`, keeps horizontal
overflow hidden by default, provides an accessible Pane name path and
reveals focused descendants. An outer Scroll reveals the immediately
nested Scroll as a unit; the inner Scroll reveals the actual focused item.

The canvas fixture reproduced another failure: `Tk.Viewport` accepted all
ordinary wheel events, even at its document edge, so an outer Scroll did
not move. Its WheelHandler now uses `blocking: false` and accepts an
ordinary wheel event only when canvas content moves. Ctrl+wheel remains
owned by Viewport for zoom. A Grid-cell nested Scroll fixture verifies the
same inner/outer handoff in Grid placement.

## Verification

The focused QtQuickTest file has ten passes (including init/cleanup) on
Qt 6.8.3 and 6.11.1: nested wheel ownership, edge handoff, keyboard
ownership/handoff, paging, disabled inner movement, horizontal overflow,
nested focus reveal, Grid-cell wheel routing, canvas edge handoff and
Ctrl+wheel zoom ownership. The full integration CTest suite passes 17/17
on each Qt version; `all_qmllint` passes on Qt 6.11.1.

The post-change diagnostic dump includes
`Scroll#outerScroll 12,214 216x184`,
`Flex#outerScrollContent 0,0 216x228`,
`Scroll#innerScroll 0,152 216x76`, and
`Flex#innerScrollContent 0,0 216x120`.
The compact and comfortable 960×760 software captures were opened: rows
past both viewport bottoms are clipped, with the two vertical scrollbars
visible. Ignored captures are
`build/layout-diagnostics-scroll-{compact,comfortable}.png` in the
integration worktree. The dump proves viewport/content geometry; the
QtQuickTests prove routing and reveal behavior.

## Support boundary

An in-progress touch drag remains owned by the viewport where it began;
there is no mid-gesture handoff at an edge. Overscroll uses hard bounds.
Physical touch hardware, high-resolution trackpad phases and GPU pixels
have not been qualified by these headless tests. Viewport's wheel handler
uses `angleDelta`; trackpad pixel-delta behavior remains a later input
qualification gate. This slice does not add virtualization to Scroll.
