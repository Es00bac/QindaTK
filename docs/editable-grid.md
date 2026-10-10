<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Editable grid model

`EditableGridModel` is the authoritative two-dimensional C++ model for an
editable grid. It derives from `QAbstractTableModel`, so `TableView` and other
Qt item consumers can use it directly. This package supplies model, range,
header geometry and transaction behavior; the QML cell and header controls
are separate.

## Identity and storage

`setDimensions(rows, columns)` grows with generated `r:N` and `c:N` IDs and
preserves existing IDs when shrinking. `insertGeneratedRowsAt(index, count)`
and `insertGeneratedColumnsAt` insert fresh generated IDs, skipping numbers
still in use. When an axis becomes empty its numbering restarts at 1, so a
document reopened into the same grid gets the same IDs back. Each axis stores
consecutive generated IDs as one run (`GridAxisIds`), not a string and a hash
entry per section: index ↔ ID lookups are O(log runs), and a 1,000,000-row
axis of generated IDs costs a few runs. `setRowIds` and `setColumnIds` accept
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
GUI-thread objects; a source callback must not mutate the grid. A source that
owns its data, such as a spreadsheet, uses the
[domain source](#domain-sources) extension instead of the override map.

## Visible cells and geometry

`visibleCells(firstRow, rowCount, firstColumn, columnCount)` returns at most
4096 cells and never materializes the whole grid. An invalid or larger request
returns an empty list. Each record has row/column indices and IDs plus the
value. `visibleRegions(viewportWidth, viewportHeight, scrollX, scrollY)`
returns up to four index rectangles: frozen corner, frozen row strip, frozen
column strip and scrolling body. `cellRect` and `hitTest` use the same section
size prefix arrays; `frozenExtent()` is the width of the frozen columns and the
height of the frozen rows, where the scrolling panes start. A section resize updates those arrays; a move carries its
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
An interactive resize ends with `finishResize(id, rowAxis)`, which emits
`resizeFinished(id, rowAxis, size)` once per gesture; a host that persists
sizes listens to that instead of every intermediate `rowResized`.
`resetRowSize(id)`/`resetColumnSize(id)` return a section to the default size
so it follows density changes again.

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
step. This is an adapter bridge, not an `UndoService` registration. A
domain model that owns its data uses [`sourceCommits`](#domain-sources)
instead and records its own steps as a
[document model](document-models.md#commands). There must be one history
owner per active grid, otherwise a shared undo command can replay the same
edit twice.

`readOnly = true` cancels an active edit, clears local/external history and
rejects `setData`, edit transactions, paste/fill, undo/redo, history replay and
row/column identity changes. Item flags omit `ItemIsEditable`. A query/pivot
result adapter enables this mode on its presentation snapshot; the source
grid remains separately editable. Section geometry may still be resized for
display.

The model does not implement spreadsheet formulas, persistence, accessible
virtual cells or keyboard focus. Those belong to the domain model and the
EditableGrid/HeaderView/RangeSelectionOverlay view layer.

<a id="domain-sources"></a>
## Domain sources

A `GridCellSource` that also derives from `GridCellSourceExtension` adds
optional capabilities. The model finds the extension with `dynamic_cast`, so
existing sources keep their behavior and ABI. `Tk.WorkbookSheetAdapter` is
the reference implementation ([workbook.md](workbook.md#views-and-adapter)).

| Member | Contract |
| --- | --- |
| `editValue(rowId, columnId)` | The text an editor starts from, when the displayed `value` is computed (a formula shows `4900`, edits `=SUM(B2:E2)`). `EditableGridModel.editValue` returns an override, else this, else `cellValue`. |
| `cellStyle(rowId, columnId)` | Optional presentation: `horizontalAlignment` (`left`, `center`, `right`), `bold`, `italic`, `underline`, `strikeout`, document `textColor`/`fillColor` (`#rrggbb`), and extra keys a delegate understands (`dataBar`, `dataBarColor`, `icon`, `iconTone` — `positive` success, `negative` danger, `muted` textMuted, `accent` accent, anything else warning — `error`, `numeric`: a number too wide for its cell shows `###` instead of being cut, and `indent`: outline levels, a non-negative integer that moves the icon and text right by `indent × space.lg` — a task tree's name column, D-303). Empty means theme defaults. |
| `mergedRanges(window)` | Merged areas intersecting an index window (x = first column, y = first row). Areas never overlap; the top-left cell is the anchor. |
| `commitChanges(label, changes, error)` | Applies one validated change set (the `committed` payload shape) atomically. Used when `sourceCommits` is true; returning false with an error keeps the edit open. |
| `fillCells(source, target, error)` | A domain fill of `target` from the repeating `source` block. Returning false with an empty error means unsupported, and the grid copies values instead. |

With `sourceCommits = true`, `commitEdit`, `paste`, `fill` and `fillRange`
validate as usual and hand the change set to `commitChanges` (or
`fillCells`). The grid keeps no overrides and no history: `canUndo`/`canRedo`
are false and the source's own history owns undo. After its data changes
elsewhere, the source calls `notifySourceChanged(firstRow, rowCount,
firstColumn, columnCount)`. That increments `revision` and notifies views,
which re-read only the cells they show. `revisionValue()` is the same
revision as an integer, for cheap per-revision memos.

Merged areas change geometry and navigation, not storage:

- `mergedRegions(firstRow, rowCount, firstColumn, columnCount)` and
  `mergeAt(row, column)` report them as `{firstRow, firstColumn, lastRow,
  lastColumn}`.
- `mergeBounds(firstRow, firstColumn, lastRow, lastColumn)` returns the
  smallest rectangle that holds the given one and every merged area it
  touches; selections and the editor grow to it.
- `layoutCells(viewportWidth, viewportHeight, scrollX, scrollY)` returns
  the visible cells positioned for a view: the `visibleCells` fields plus
  `x`, `y`, `width`, `height`, `frozen` and the cell's pane (`frozenRow`,
  `frozenColumn`), with `style` from `cellStyle`.
  A merged area is one record with `rowSpan`/`columnSpan`, positioned over
  the whole area, so it shows the anchor's value even when the anchor is
  scrolled out. An area that crosses a frozen edge is drawn once per pane,
  clipped to it; the part without the anchor has `mergeContinuation: true`,
  no value and the anchor's style. Covered cells are omitted. At most
  4,096 records.

<a id="fill-handle"></a>
### Fill handle

`fillRange(firstRow, firstColumn, rowCount, columnCount, targetRow,
targetColumn, targetRowCount, targetColumnCount)` repeats a source block over
a target rectangle as one transaction. With `sourceCommits` the source fills
(a spreadsheet moves relative references and copies formats); otherwise the
values are copied. `Tk.GridFillHandle` (`gridFillHandle`) is the drawn
handle on the bottom-right corner of the model's last range. Dragging
previews the target in `gridFillPreview`: it extends the range along the
axis the pointer is furthest outside, down, up, left or right. Releasing
calls `fillRange` and selects source plus target. A drag cut short (the
handle hides because an edit starts) fills nothing. The handle is hidden
for read-only models and when the corner is scrolled out or under a frozen
pane. Keyboard users
fill through host commands (SheetView: Ctrl+D / Ctrl+R).

## QML view layer

`Tk.EditableGrid` is a `T.Control` over this model. It applies the current
`Theme.size.row` and `Theme.size.tableColumn` as model defaults, instantiates
one `Tk.GridCellDelegate` per record of `layoutCells`, and keeps only
viewport offsets, active/editing endpoint IDs and the fill-handle preview.
The delegate draws the source's `cellStyle`: alignment, weight, slant,
underline, strike-out, document text and fill colors (text over a document
fill switches to a contrasting theme color), a data bar
(`gridCellDataBar`), a trend icon (`gridCellIcon`) and the danger color for
error values, and indents icon and text together by `indent` levels (icon first, then text); a merged record spans its area. A `numeric` value that does not
fit shows whole `#` glyphs (`gridCellHashes`) while its accessible name keeps
the value; other text elides. The `gridCellViewport` clip
keeps scrolling content below frozen rows and columns. Cells stack by pane:
the body, then the frozen row and column strips, then the frozen corner, so
a strip scrolled under a pane that stays put is covered by it. The selection
(`gridRangeOverlay`) draws above every pane and the editor
(`gridCellEditor`, in `gridEditorPane`) above the selection, each clipped to
its own pane. `Tk.HeaderView` resolves visible section
IDs and geometry from the model on structural or geometry changes. Dragging a
header seam resizes its model section and calls `finishResize` on release;
dragging the section body submits a stable-ID move. `Tk.RangeSelectionOverlay`
splits a selection at frozen row/column boundaries so the visible borders
follow the four distinct cell regions; each band's outline
(`gridRangeBorder_<index>`) is drawn in a clip of its pane
(`gridRangePane_<index>`), so a band scrolled under a frozen pane is cut
there, border included. `gridRangeSelection_<index>` keeps the whole band. `Tk.GridFillHandle` is shown while
`fillHandleEnabled` (default true) and the model is editable.

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
cell editor, which starts from `editValue` and covers the whole merged area
(`editorRect`); typing a printable character on the table replaces the
cell's content with it (type-to-edit). Edits remember the cell's row and
column IDs until commit even if the selection changes. A commit the model
refuses cancels the pending model edit and keeps the editor open with the
typed text. Escape cancels. Arrow keys treat a merged area as one cell and
leave it from its far edge, and range selection grows to whole merged areas.
The view exposes `pasteMatrix` and `fillSelection` as host integration points
for clipboard and fill-handle adapters; the model validates and commits either
operation atomically.

<a id="group-header-rows"></a>
## Group header rows (D-312)

`EditableGrid.groupHeader` is a function `(rowId, index)` returning
`{label, summary, collapsed, level, values: {columnId: text}}` for a row that
is a group header of the host's projection (MS Project's and Access's group
bands), and null for every other row. The grid draws such a row as a band
across the viewport (`GridGroupHeaders`): a chevron indented by `level`, the
bold name and the summary at the viewport's left edge, and each `values`
entry right-aligned in its column where it does not run under the title.
The band lies above the cells and under the selection and the editor; a
body row's band is clipped under the frozen rows like the cells it covers.
The grid never edits a group row (`beginCellEdit` refuses it); a tap selects
its cell under the pointer, and the chevron, Return/Enter/F2 or Space emit
`groupToggled(rowId, collapsed)` — the state the user asked for. The host
re-projects its rows (`setRowIds` without a collapsed group's members) and
its `groupHeader` answer, which keeps a grid beside a `GanttView` on the
same rows ([gantt.md](gantt.md#planning-rows-d-312-d-313)). The model is
unchanged: group rows are ordinary row IDs whose cells the host leaves
empty or read-only.
