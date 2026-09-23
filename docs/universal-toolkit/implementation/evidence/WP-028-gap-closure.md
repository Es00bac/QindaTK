<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-028 gap-closure lane evidence

Isolated worktree from integration `224a681`, 2026-09-23. Linux offscreen
software renderer, Qt runtime 6.11.1, GCC 15.3.0, Debug/Ninja.
`portageq envvar MAKEOPTS` returned `-j24 -l24`. Builds used plain
`cmake --build build/wp028-gap-closure` and named targets, with no job/load
override.

## Focused checks

- `tst_editable_grid_model`: pass. Inserted row and column IDs emit Qt
  insert notifications; survivor IDs, sparse values, ranges, sizes and local
  undo/redo remain attached to their original cells. External history replay
  works after an insertion with the current revision. Removing an unrelated
  row emits a remove notification and retires that receipt; replay and release
  reject it. Removing an edited column prunes its sparse cell and range.
  Invalid insert/remove requests emit no structure change.
- Focused `tests/qml/tst_editable_grid.qml`: 7 pass, 0 fail. A real
  100,000 × 100 QML model and view exercise bounded visible work, true
  insert/remove transitions, stable active IDs, accessible selected/focused
  state and press action, overlay movement, Shift range extent, Escape
  restore, Ctrl+Home/End and Page Down navigation.
- `qmllint` on `EditableGrid`, `HeaderView` and `RangeSelectionOverlay` with
  CMake module import/resource paths: exit 0, no warnings.
- `qtk-preview examples/editable-grid.qml`: compact and comfortable render
  and dump succeeded using the 100,000 × 100 visual fixture.

## Visual evidence

The [compact](WP-028-100k-compact.png) and
[comfortable](WP-028-100k-comfortable.png) PNGs were opened and inspected.
Headers, frozen columns, source values and the selected row align with the
cell viewport at both densities. The dump counted **338** visible cell
delegates compact and **242** comfortable, versus 10,000,000 logical cells.

Representative `qtk-preview --dump` lines at 1120×640:

```text
compact:     EditableGrid#editableGridExample  0,28 1096x588
compact:       HeaderView#gridColumnHeaders  80,0 1016x24
compact:       HeaderView#gridRowHeaders  0,24 80x564
compact:       Item#gridCellViewport  80,24 1016x564
comfortable: EditableGrid#editableGridExample  0,34 1092x578
comfortable:   HeaderView#gridColumnHeaders  96,0 996x29
comfortable:   HeaderView#gridRowHeaders  0,29 96x549
comfortable:   Item#gridCellViewport  96,29 996x549
```

The previous [visual-lane evidence](WP-028-visual.md) records the original
100,000 × 36 example and its then-current checks. This lane updates that
example to 100,000 × 100. Full WP-028 acceptance, including integrated
dual-Qt and assistive-technology verification, remains with Sol.
