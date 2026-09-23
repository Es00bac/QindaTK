<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Keyboard and accessible dock placement

The dock placement gaps from the [closeout audit](WP-007-FIX-F04-closeout.md)
have a toolkit-owned operation surface. Docked and floating frame headers
have named Place buttons, keyboard activation and explicit accessible press.
Shift+F10 on a panel or focused tab opens the same host-owned dialog, including
for the focused non-active tab. The tab context menu also includes Place.

`DockPlacementDialog` lists permitted zone destinations, before/after and
adjacent-lane destinations, grouping with another panel, and explicit tab
positions. Floating panels additionally expose named X/Y NumberFields with
keyboard and accessible step operations. Coordinates are proposals; applying
calls `moveFloating` once and clamps to the current host minus panel dimensions
(or zero for an oversized panel). Cancel makes no model call. This supplies an
explicit accept/cancel movement path without copying live placement into QML.
All actual arrangement remains in `DockModel`.

Applying validates host enabled state, source visibility, the original
model/workspace, legal destination and group identity. A replaced/hidden target
cannot be applied. `panelPlacementAccepted` reports one accepted request;
programmatic model updates remain silent. Reordering preserves the existing
active tab. Closing resolves the successor frame and restores focus to its
Place button after structural replacement.

## Verification

Own worktree/builds: `QindaTK-wp007-dock-placement/build/placement` (Qt 6.11.1)
and `build/qt68` (Qt 6.8.3). Builds read `portageq envvar MAKEOPTS` and passed
its actual `-j24 -l24` equivalents to Ninja without overriding MAKEOPTS.

- Full CTest: **20/20** on each Qt version. These runs preceded the final
  direct-call primary-enabled guard and extra relative-destination test rows.
- Final focused `tst_dock_placement.qml`: **9/9** on each Qt version, including
  keyboard entry/move/apply, accessible entry/cancel/apply, unchanged canceled
  model coordinates, bounded coordinates, disabled entry and hidden source,
  tab order and active selection, tab grouping, before/after and lane placement,
  and one acceptance emission per operation.
- Final `all_qmllint`: exit **0** on both. Qt 6.8 continues to report the
  project's ThemeMetrics property and Flex enum inference warning classes.
- Compact and Comfortable `qtk-preview --dump --grab --wait 600` inspected
  visually. A 900×700 fixture contains a floating History panel and right-zone
  Layers panel, with the History placement dialog open. Generated images are
  `build/placement/placement.png` and `placement-comfortable.png`.

Geometry evidence:

```text
Compact:
PopupItem#DockPlacementDialog  240,255 420x191
ComboBox#dockPlacementDestination  0,0 404x24
NumberField#dockPlacementX  0,28 404x24
NumberField#dockPlacementY  0,56 404x24
Button#dialogPrimary  354,0 50x24  "Apply"
Comfortable:
PopupItem#DockPlacementDialog  198,238 504x224
ComboBox#dockPlacementDestination  0,0 484x29
NumberField#dockPlacementX  0,34 484x29
NumberField#dockPlacementY  0,68 484x29
Button#dialogPrimary  430,0 54x29  "Apply"
```

The existing docking example automatically exposes the new header actions.
`docs/docking.md`, `docs/controls.md` and the regenerated catalog describe the
operation and stable object names. Native screen-reader action delivery,
physical input and native platform qualification remain WP-092; this evidence
covers QML keyboard/accessibility contracts and model effects only.

## Integrated small-window review

A 480×320 Touch, 3× text, high-contrast-dark preview exposed a dialog defect:
the initial long subtitle occupied 262px of the 272px capped popup, leaving
its scrolling body hidden under the footer. The subtitle now reads “Choose
placement.” The opened replacement grab shows the destination picker and
footer within the popup; the body scrolls to the X/Y fields. Its dump reports
`PopupItem#DockPlacementDialog 24,24 432x272`, `Scroll#dialogBody 0,97
432x101`, `ComboBox#dockPlacementDestination 0,0 408x48`, and
`NumberField#dockPlacementY 0,108 408x48`. A new focused QML test resizes the
window to 480×320 at Touch 3×, focuses the Y field, checks that the body
scrolls to it, changes the draft by keyboard, and confirms the model remains
unchanged before Apply. The ignored capture is
`/tmp/qindatk-dock-placement-small-final.png` in this environment.
