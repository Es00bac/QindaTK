<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-028 visual lane evidence

**Status:** visual lane implemented; full WP-028 acceptance remains active.

## Delivered

- `Tk.EditableGrid` renders the model's visible region windows, binds row and
  column defaults to the active `Theme` density, and keeps selection/editor
  identity by stable row and column IDs.
- `Tk.HeaderView` presents model-backed row/column headers, seam resize,
  stable-ID reorder, and synchronized geometry after model updates.
- `Tk.RangeSelectionOverlay` derives selection geometry from model endpoints
  and draws separate outlines across frozen and scrolling bands.
- Arrow keys move the active cell; Shift+arrows extends a range; Enter/F2
  begins a cell edit, Enter commits, Escape cancels, Ctrl+Z/Y uses model
  history, and wheel/Shift+wheel pans the viewport.
- The table accessible name and description report dimensions and the active
  cell/value. Visible cells expose row, column and value names.
- `examples/editable-grid.qml` shows a 100,000 × 36 lazy asset inventory;
  `tests/qml/tst_editable_grid.qml` exercises a 100,000 × 100 model.

## Verification

- `cmake --build build/wp028-visual`: exit 0, first full isolated build.
- `tst_qml -input tests/qml/tst_editable_grid.qml`: 5 passed, 0 failed.
  Covers keyboard range selection, atomic paste rejection/undo, accessible
  metadata, bounded delegates, header resize/reorder propagation, and editing
  the original cell after clicking a different cell.
- `tst_editable_grid_model`: 6 passed, 0 failed. Its 100,000 × 100 fixture
  reports 640 returned visible cells and 187 ms to set up dimensions on this
  run; it also covers stable identity, geometry, atomic edit/undo and host
  history.
- `all_qmllint`: exit 0. It reports repository-wide warnings; the three new
  controls produce no lint warnings or QML parse/build errors.
- `qtk-preview examples/editable-grid.qml --dump`: `EditableGrid#editableGridExample
  0,28 1096x588`; `HeaderView#gridColumnHeaders 80,0 1016x24`;
  `HeaderView#gridRowHeaders 0,24 80x564`; `Item#gridCellViewport 80,24
  1016x564`. The 100,000 × 36 demo creates 338 cell delegates, and the
  frozen/body selection is split into `160x22` and `160x22` outlines.
- Compact and comfortable-density grabs were rendered and inspected:
  `build/wp028-visual/grid-compact.png` and
  `build/wp028-visual/grid-comfortable.png` (1120 × 640).

## Current adapter limits

Clipboard ownership remains with the host: it can parse clipboard data and
call `pasteMatrix(row, column, rows)`. `fillSelection` is the model-backed
host hook; an external drag handle still needs to map its pointer target to a
row and column. The built-in cell editor is a single-line text editor; typed
column editor delegates remain an application concern.
