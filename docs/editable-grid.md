<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Editable grid model

`EditableGridModel` is the authoritative two-dimensional C++ model for an
editable grid. It derives from `QAbstractTableModel`, so `TableView` and other
Qt item consumers can use it directly. This package supplies model, range,
header geometry and transaction behavior; the QML cell and header controls
are separate.

## Identity and storage

`setDimensions(rows, columns)` grows with generated `r:N` and `c:N` IDs and
preserves existing IDs when shrinking. `setRowIds` and `setColumnIds` accept
provider IDs. IDs must be unique, nonempty and exclude U+001F, which separates
row and column IDs in the sparse storage key. Reordering the same IDs preserves
values, selection endpoints and undo. Inserting IDs preserves history for
survivors. Removing an ID prunes its sparse cells and invalidates all history;
this conservative boundary prevents stale replay. A row/column index is a
visible coordinate, never persistent identity. `data()` exposes `value`,
`rowId` and `columnId` roles; `headerData()` returns stable section IDs as the
fallback label. An application can present a display label from its own ID
metadata.

`insertRowsAt(index, ids)` and `insertColumnsAt(index, ids)` emit Qt section
insert notifications and preserve survivor IDs, sparse values, ranges,
explicit section sizes and local/external history receipts. New IDs must be
unique and absent. `removeRowsAt(index, count)` and
`removeColumnsAt(index, count)` emit remove notifications, prune removed
cells/ranges and retire **all** local and external history receipts, even a
receipt whose edited cell survived. The host must discard those receipts;
`releaseHistoryTransaction` and `applyHistoryChanges` reject them after
removal. All four operations cancel a pending edit before changing structure.
Invalid requests leave the model unchanged. `setRowIds`/`setColumnIds` remain
the complete replacement/reorder API and may use model reset notifications.

`GridCellSource` provides lazy baseline `value(rowId, columnId)` and optional
`validate(rowId, columnId, candidate)`. The grid reads the source only for
requested cells. Committed edits live in its sparse override map. Replacing
the source resets overrides and history, so applications should treat it as
loading a new document. A host can persist changes from `committed` and
reconstruct the sparse state through a fresh source. The source and grid are
GUI-thread objects; a source callback must not mutate the grid.

## Visible cells and geometry

`visibleCells(firstRow, rowCount, firstColumn, columnCount)` returns at most
4096 cells and never materializes the whole grid. An invalid or larger request
returns an empty list. Each record has row/column indices and IDs plus the
value. `visibleRegions(viewportWidth, viewportHeight, scrollX, scrollY)`
returns up to four index rectangles: frozen corner, frozen row strip, frozen
column strip and scrolling body. `cellRect` and `hitTest` use the same section
size prefix arrays. A section resize updates those arrays; a move carries its
size with its ID. The initial logical sizes are 24 for rows and 100 for
columns. `defaultRowSize` and `defaultColumnSize` let a QML view bind the
defaults to density-aware `Tk.Theme.size` roles. Changing a default updates
all sections without explicit per-ID resizes in one O(axis length) pass, so a
100,000-row density change needs one model property assignment. Explicitly
resized sections retain their sizes. The view chooses viewport and scroll offsets in these same logical
units. The view clips scrolling cells under frozen strips and uses the exact
visible regions to create delegates; `visibleCells` is deliberately capped to
make an accidentally unbounded delegate request visible.

`requestRowResize`, `requestColumnResize`, `requestRowMove` and
`requestColumnMove` validate IDs and dimensions before mutation. A move places
an ID immediately before `beforeId`, or last for an empty `beforeId`. Frozen
counts are clamped to the available sections. Header visuals should use these
model requests and `cellRect`/`hitTest`; successful requests emit `rowResized`,
`columnResized`, `rowMoved` or `columnMoved` with stable ID and accepted result.
Headers should not keep another order or
width map. Section labels, hidden state and sort intent are domain metadata;
this foundation supplies stable IDs and geometry but does not claim a sort
operation. A domain sorter calls `setRowIds` with its sorted order.

## Ranges and editing

`setRange(anchorRowId, anchorColumnId, activeRowId, activeColumnId, append)`
stores up to 1024 discontiguous ranges by endpoint IDs. `ranges` resolves their
current indices after reorder. Removing an endpoint removes its range.
Selections remain compact even for a 100,000 by 100 rectangle. An overlay can
draw focus and fill handles from the resolved rectangle and `cellRect`.

`beginEdit(label)`, `stageCell`, `commitEdit` and `cancelEdit` form one edit.
The `QAbstractTableModel::setData` path uses the same transaction; if an edit
is already active it stages the cell and leaves the caller to commit or cancel.
`paste` and `fill` use that same path and commit automatically when no edit is
active. Both reject out-of-bounds targets; a paste or fill failure restores the
pending changes it added. The source validator and optional C++ validator run
for every candidate before any committed cell changes. One transaction is
limited to 65,536 changed cells. `commitEdit` emits one complete `committed`
change set with a transaction UUID. `cancelEdit` leaves committed state and
history unchanged. The default `undo`/`redo` applies all cells in that step
atomically and verifies current values before replay.

`externalUndo = true` transfers history navigation to the host. In that mode,
local `undo`/`redo` are disabled. The host records the one `committed` signal
in its document command history and calls
`applyHistoryChanges(transactionId, changes, reverse, expectedRevision)` on
undo or redo. It passes the exact change payload and the grid's current
`revision`; the grid checks transaction provenance, unchanged identity
generation, payload, revision, current values and validation before applying
anything. The host should call `releaseHistoryTransaction` when it discards a
step. This is an adapter bridge, not an `UndoService` registration:
`UndoService` currently records `PropertyDocument` property changes only.
There must be one history owner per active grid, otherwise a shared undo
command can replay the same edit twice.

`readOnly = true` cancels an active edit, clears local/external history and
rejects `setData`, edit transactions, paste/fill, undo/redo, history replay and
row/column identity changes. Item flags omit `ItemIsEditable`. A query/pivot
result adapter enables this mode on its presentation snapshot; the source
grid remains separately editable. Section geometry may still be resized for
display.

The model does not implement spreadsheet formulas, persistence, accessible
virtual cells or keyboard focus. Those belong to the domain model and the
EditableGrid/HeaderView/RangeSelectionOverlay view layer.

## QML view layer

`Tk.EditableGrid` is a `T.Control` over this model. It applies the current
`Theme.size.row` and `Theme.size.tableColumn` as model defaults, instantiates
the cells returned by the four visible regions, and keeps only viewport
offsets plus active/editing endpoint IDs. The `gridCellViewport` clip keeps
scrolling content below frozen rows and columns; cell delegates in frozen
regions draw above the body. `Tk.HeaderView` resolves visible section IDs and
geometry from the model on structural or geometry changes. Dragging a header
seam resizes its model section; dragging the section body submits a stable-ID
move. `Tk.RangeSelectionOverlay` splits a selection at frozen row/column
boundaries so the visible borders follow the four distinct cell regions.

Keyboard navigation keeps focus on the table and exposes its current cell,
value and dimensions through the accessible name/description path. Visible
cells expose row, column, value, selected/focused state and an assistive press
action to select the cell. The table description includes the selected range's
row/column extent. Shift+arrows, Shift+Page Up/Down and Shift+Home/End extend
selection; Escape restores the cell selected before extension. Ctrl+Home/End
jump to the first/last cell and Page Up/Down move by a visible page. After a
section removal the view retains surviving active IDs; a removed active ID
falls back to the nearest surviving index and updates its accessible state.
Enter or F2 moves focus to the
cell editor; edits remember the cell's row and column IDs until commit even if
the selection changes. Escape cancels. The view exposes `pasteMatrix` and
`fillSelection` as host integration points for clipboard and fill-handle
adapters; the model validates and commits either operation atomically.
