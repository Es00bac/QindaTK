<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 — Dock tab pointer drag qualification

The `T.TabButton` delegate retains functioning horizontal reorder and vertical
tear-off on Qt 6.11.1 and 6.8.3. No production pointer handling change was needed.

The initial regression tests dispatched input after delegates existed but before
Flex had rendered their final ancestor geometry. They also inspected the original
strip after a successful reorder, although that structural model change replaces
the frame and strip. The tear-off test additionally issued an unrelated press in
root coordinates before starting its actual gesture. These test defects obscured
the working pointer path.

The corrected tests wait for a rendered frame, use a coherent pointer sequence,
and reacquire the strip after reorder. Temporary diagnostic instrumentation
confirmed the existing handler receives a passive grab, takes the exclusive grab,
and transitions active on/off. That instrumentation was removed.

Coverage in `tests/qml/tst_dock.qml` now proves:

- Dragging inactive tab C before B changes the order to C/B while B remains
  selected and `tabActivated` is not emitted.
- Vertical tear-off starts one host drag for C, updates `dragPointer` to the
  canvas target, ends once on release, emits one `panelDropped`, and floats C.
- The existing keyboard, click, accessible press, selected state, disabled state,
  and focus ring checks continue to pass.

Validation:

- Qt 6.11.1: focused Dock suite 12/12; full CTest 19/19, including QML 259/259.
- Qt 6.8.3: focused Dock suite 12/12.
- Catalog regenerated with no changes (101 types).
- No visual layout or production QML changed; no new screenshot claim is made.
- Builds used the actual system MAKEOPTS job/load limits without overrides.
