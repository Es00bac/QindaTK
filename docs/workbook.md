<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Workbook model and calculation engine

`Tk.Workbook` (C++ `QindaTK::Workbook`, `<qindatk/workbook.h>`) is the
authoritative spreadsheet document: ordered sheets with stable IDs, sparse
cells, typed literals and formulas, computed values, one revision counter and
one undo/redo history. Views never keep cell state. `Tk.SheetView`,
`Tk.FormulaBar` and `Tk.WorkbookTabs` present it; `Tk.WorkbookSheetAdapter`
feeds `Tk.EditableGrid` through the grid's existing public contract
([editable-grid.md](editable-grid.md)). This page is the contract for WP-055's
engine slice; [controls.md](controls.md#workbook-and-spreadsheet) lists the
QML members.

## Document model

| Concept | Rule |
| --- | --- |
| Sheet identity | `sheet-N` IDs from a counter that is persisted and never rolls back, so an ID is never reused in a document, even after undo or reopen. Order and names are separate, editable state. |
| Sheet names | 1–31 UTF-16 units, none of `[ ] : * ? / \`, no leading/trailing `'`, no control characters, unique ignoring case. |
| Dimensions | Each sheet declares `rowCount × columnCount` (default 1000 × 26) up to the engine grid 1,048,576 × 16,384 (`A1:XFD1048576`). Row/column insertion grows the declaration; a sheet keeps at least one row and column. Cells are only stored where they have content, so memory follows content, not dimensions. |
| Cell content | Empty, a literal (`number`, `text`, `boolean`, `error`) or a formula. A formula cell also caches its computed value. |
| Address | A row/column position. Views give rows/columns stable grid IDs, but formulas refer to positions and are rewritten by structural edits (below). |

### Typed input

`setCellInput(sheetId, row, column, input)` and every paste use the same
rules, applied to the whole text with no trimming:

| Input | Result |
| --- | --- |
| `""` | Clears the cell. |
| `=…` | Formula (grammar below). A malformed formula is rejected with a UTF-16 span and changes nothing. |
| `'…` | Text of the rest (`'123`, `'=x`, `'TRUE`). A lone `'` clears. |
| `TRUE`, `FALSE` (any case) | Boolean. |
| `#DIV/0!`, `#VALUE!`, `#REF!`, `#NAME?`, `#NUM!`, `#N/A`, `#CYCLE!` (any case) | Error literal. |
| `[+-]? (digits [. digits] \| . digits) ([eE] [+-]? digits)?`, finite | Number (C locale, no grouping, no percent/currency/date parsing). |
| anything else | Text, at most 32,767 units. |

`cellInput` renders content back so that re-entering it is a no-op: numbers
in shortest round-trip form (`1.50` → `1.5`), text with a leading `'` when it
would otherwise parse as something else, formulas in canonical form.
QML/C++ values in pastes: numbers and booleans are literals, `null`/`undefined`
clear, strings follow the table.

## Formula grammar

```
formula     = "=" comparison
comparison  = concat   { ("=" | "<>" | "<" | "<=" | ">" | ">=") concat }
concat      = additive { "&" additive }
additive    = term     { ("+" | "-") term }
term        = power    { ("*" | "/") power }
power       = percent  { "^" percent }              ; left associative: 2^3^2 = 64
percent     = unary    { "%" }
unary       = ("-" | "+") unary | primary            ; binds tighter than ^: -2^2 = 4
primary     = number | string | TRUE | FALSE | error | reference
            | function "(" [ comparison { "," comparison } ] ")" | "(" comparison ")"
reference   = [ sheet "!" ] cell [ ":" cell ]
cell        = ["$"] column ["$"] row                 ; A..XFD, 1..1048576, any case
sheet       = plain-name | "'" name-with-'' "'"      ; quoted when not [A-Za-z_][A-Za-z0-9_.]*
string      = '"' { any | '""' } '"'
```

Whitespace between tokens is ignored (there is no intersection operator).
Arguments are separated by `,` and numbers use `.` regardless of locale.
Unknown names, unknown functions and wrong argument counts are parse errors,
not `#NAME?` results. Limits: 8,192 UTF-16 units typed (stored formulas may
render up to 8× longer after reference rewrites), 64 nested
parentheses/calls/unary prefixes, AST height 1,024, 4,096 nodes, 255
arguments. Every error names a span `[start, end)` into the input, counting
the leading `=`; `Workbook.checkInput` returns it without changing anything,
along with every reference's span, target sheet and rectangle.

Canonical rendering is uppercase, without whitespace, keeps explicit
parentheses, writes numbers in shortest round-trip form with a two-digit
upper-case exponent (`1.5E-07`), normalizes ranges per axis (`$B2:A$1` →
`A$1:$B2`) and quotes sheet names when needed (`'Q3 plan'!A1`, `'2024'!A1`).
Parsing a rendering reproduces the same formula.

### Functions

| Function | Semantics |
| --- | --- |
| `SUM`, `AVERAGE`, `MIN`, `MAX` | References contribute only stored numbers (text, booleans and blanks are skipped); direct arguments are coerced (`"3"` → 3, `TRUE` → 1, other text → `#VALUE!`). The first error in argument order, then row-major, is returned. `AVERAGE` of nothing is `#DIV/0!`; `MIN`/`MAX` of nothing is 0. |
| `COUNT` | Numbers in references; numbers, booleans and numeric text given directly. Errors are not counted and do not propagate. |
| `COUNTA` | Every non-blank value, errors included. |
| `IF(condition, then, [else])` | Lazy: only the chosen branch is evaluated. A missing `else` yields `FALSE`. The chosen branch may be a reference (`SUM(IF(TRUE,A1:A5,0))`). |
| `IFERROR(value, fallback)` | `fallback` when `value` is an error, otherwise `value`. |
| `AND`, `OR` | Booleans and numbers; text and blanks in references are skipped; direct text must be `TRUE`/`FALSE`. No logical value at all is `#VALUE!`. |
| `NOT(x)`, `ABS(x)` | Scalar coercions as below. |
| `ROUND(x, digits)` | Half away from zero on the decimal value: the scaled value is first rounded to 15 significant digits, so `ROUND(2.675,2)` is 2.68. `digits` is truncated; negative digits round to tens, hundreds, … |

### Values, coercion and errors

- Blank is 0 in arithmetic, `""` in `&`, and takes the other operand's type
  in comparisons. A formula whose result is a blank reference yields 0.
- Arithmetic converts booleans to 1/0 and numeric text exactly as typed
  input does; other text is `#VALUE!`. Results that are not finite are
  `#NUM!`, except division by zero and `0^negative` (`#DIV/0!`); `0^0` is
  `#NUM!`.
- `&` joins display text: numbers use up to 15 significant digits
  (`1/3&""` is `0.333333333333333`), booleans `TRUE`/`FALSE`. Text longer
  than 32,767 units is `#VALUE!`.
- Comparisons never coerce across types: Number < Text < Boolean; text
  compares case-insensitively (Unicode simple case folding).
- A multi-cell range used as a single value is `#VALUE!`; a 1×1 range is its cell.
- Errors propagate left to right: `=1/0+#N/A` is `#DIV/0!`.
- Display ("General"): up to 15 significant digits, exponent form from
  1E+15 upward and below 1E-4 (`0.00001` shows `1E-05`), `TRUE`/`FALSE`,
  error literals, text unchanged. There are no number formats yet.

## References

| Operation | Rule |
| --- | --- |
| Copy, paste, fill | Relative row/column parts move by the distance from the source cell; `$` parts stay. A reference moved outside `A1:XFD1048576` becomes `#REF!` (`=G1*2` filled up from G2 to G1 becomes `=#REF!*2`). Implicit-sheet references follow the destination sheet; explicit `Sheet!` references keep their sheet. |
| Fill | The source block repeats over the target in both directions, aligned to the source origin. Literals are copied, not extrapolated into series. `WorkbookSheetAdapter.fillDown/fillRight` follow Excel: a multi-row selection copies its top row down; a single row copies the row above. |
| Insert rows/columns | Every reference into the sheet keeps its target cell, regardless of `$`. A range spanning the insertion point grows. A reference pushed past the grid becomes `#REF!` and is recalculated; otherwise values are unchanged and nothing is recalculated (measured: 0 evaluations). |
| Remove rows/columns | A single-cell reference into the removed band becomes `#REF!`. A range loses the removed band and becomes `#REF!` only when all of it is removed. Only formulas whose value can change are recalculated. |
| Rename/reorder sheets | References hold sheet IDs, so identity is unchanged; formula text re-renders with the new name. Nothing recalculates. A name typed after a rename must be the new one. |
| Remove sheet | Every reference to it becomes `#REF!` (`=Data!A1*2` → `=#REF!*2`). Undo restores the sheet (same ID) and every rewritten formula exactly. |

## Transactions, history and revision

Every operation (`setCellInput`, `pasteInputs`, `copyCells`, `fill`,
`clearCells`, row/column and sheet operations) validates everything first and
is one atomic history step. `beginTransaction(label)` groups several into
one step; `commitTransaction` recalculates and records it;
`cancelTransaction` restores the exact prior state, values included. A failing
operation inside an open transaction rolls back only itself. While a
transaction is open, `undo`/`redo`/`save` are refused and values are not yet
recalculated.

The history stores exact inverse mutations, recomputed at every undo/redo, so
undo reproduces the previous serialization byte for byte (except the
sheet-ID counter). History keeps 200 steps. `revision` increases on every
commit, undo, redo, restore and reset; `modified` compares the history
position with the last `save`/`open`/`markSaved`. An input equal to the
current content (after canonicalization) records nothing.

## Recalculation

- The dependency index maps each referenced cell to its formulas and each
  distinct range to its formulas, bucketed by column (ranges wider than 32
  columns are indexed per sheet).
- Commit seeds recalculation with the changed positions. Every formula
  reachable through the index is dirty, ordered with Kahn's algorithm and
  evaluated once. No work is done for formulas outside that closure.
- Cycles are found on the static graph with iterative Tarjan SCC over the
  unresolved remainder. Members of a cycle (including self and range
  self-references such as `F10: =SUM(F1:F10)`) are `#CYCLE!`; formulas that
  depend on a cycle evaluate normally and usually propagate it (`IFERROR`
  can absorb it). Chains and cycles of 50,000 cells need no recursion.
- `statistics` reports `lastDirty`, `lastEvaluated`, `lastCycleCells`,
  `lastRecalcMicroseconds`, `totalEvaluations` and index sizes.
- Range functions scan stored cells: a range of at most 4,096 positions
  probes them; a larger one walks the stored cells of its row band. A
  running total such as `=SUM(A$1:A1)` filled down is quadratic.

## Native file format

`serialize()`/`restore()`, `save(url)`/`open(url)` and the FormatRegistry
provider `qindatk.workbook.v1` (`.qtkwb`, `WorkbookFormat` in
`<qindatk/workbook_format.h>`) share one codec. Files are compact UTF-8 JSON
with sorted keys:

```json
{"documentId":"<uuid>","format":"qindatk.workbook","nextSheetNumber":3,
 "sheets":[{"cells":[["A1","s","Item"],["B2","n",1200.5],["C2","b",true],
                     ["D2","e","#N/A"],["F2","f","=SUM(B2:E2)"]],
            "columns":26,"id":"sheet-1","name":"Budget","rows":1000}],
 "version":1}
```

Cells are row-major `[address, type, value]` with `n` number (shortest
round trip), `s` non-empty text, `b` boolean, `e` error literal, `f` canonical
formula text naming sheets. Computed values are not stored; opening
recalculates every formula deterministically, so `serialize(restore(bytes))`
equals `bytes`. Restore validates everything (format, version 1 only,
unknown fields, IDs, names, dimensions, addresses, duplicates, literal types
and every formula with its span) before replacing anything, and clears
history. `save` writes atomically with `FormatIO::writeAtomic`; an existing
file is replaced only if it still has the SHA-256 this workbook last read or
wrote there (or with `replaceExisting` for a file it has not seen).

The provider claims import, export and roundtrip for this format only. Its
import job returns canonical bytes in `jobs.job(id).result.documentBytes`
for `Workbook.restore`. No CSV, XLSX, ODS or other interchange format is
recognized.

## Views and adapter

`WorkbookSheetAdapter` gives `EditableGridModel` the sheet's dimensions as
stable IDs (`r:N`, `c:N`), returns display text from `GridCellSource.value`
only for requested cells, validates candidate inputs with the formula parser
and sets `externalUndo` so the Workbook stays the only history owner. A
committed grid edit or paste becomes one Workbook transaction; the adapter
then reverts the grid's override layer through `applyHistoryChanges`, so the
grid never holds values. Workbook changes emit the grid's `dataChanged` and
`cellsChanged`; structural changes insert fresh IDs or remove the deleted
ones, so selections and section sizes follow content. The adapter falls back
to the first sheet when its sheet is removed or the document is replaced.
`sourceReadCount()` measures the bounded visible work.

## Gaps in consumed contracts

These are consumed as-is; each is recorded for the WP-019/WP-028 owners
rather than changed here.

| ID | Contract | Gap and current handling | Proposed change |
| --- | --- | --- | --- |
| G-1 | EditableGridModel commit | The grid pre-fills its editor from `cellValue` (display) and drops a commit whose text equals it, so replacing a formula shown as `6` with the literal `6` never reaches `committed`. SheetView sets `editText` to the source input and also applies every `editCommitted` value through `commitInput` (a no-op when unchanged). | `GridCellSource::editValue()` for the editor and a source-owned commit mode. |
| G-2 | EditableGridModel source changes | No public request for "source values changed"; the adapter emits the model's public `dataChanged`/`cellsChanged` signals itself. | `notifySourceChanged(firstRow, rows, firstColumn, columns)`. |
| G-3 | EditableGrid view | At `224a681`, `activeRow`/`activeColumn` bind to `rowIndex(activeRowId)` and do not re-evaluate after rows move. SheetView derives its own indices from the IDs. | Depend on `dataEpoch` — done on the integration branch by `47086db`; SheetView's copy then becomes redundant. |
| G-4 | EditableGrid view | No per-cell presentation hook (alignment, type or error styling), no type-to-edit, no fill handle. Numbers are left-aligned. | A delegate or style-role hook. |
| G-5 | EditableGridModel fill | `fill` copies display values; formula fill uses `Workbook.fill` instead. | — (domain operation) |
| G-6 | FormatRegistry export | `startExport` accepts only `PropertyDocument`/`DocumentSession`; a workbook snapshot cannot be exported through the registry. The provider's `exportDocument` works directly. `adoptImport` creates a `DocumentSession`, so workbook imports are adopted with `restore`. | A public `startExportSnapshot(SnapshotPtr, …)` or snapshot-source interface. |
| G-7 | UndoService / DocumentSession | Both are `PropertyDocument`-specific; the Workbook keeps its own history and saved state. | A document-agnostic history participant. |
| G-8 | Grid identity scale | `EditableGridModel` keeps O(rows + columns) ID strings; a 100,000-row sheet takes about 190 ms to present (Debug build), and a 1,048,576-row sheet is not practical in the view even though the engine handles it. | Range-compressed or lazy section IDs. |

## Not supported yet

Named ranges, whole-row/column references (`A:A`, `1:1`), 3-D references,
array formulas and spilling, volatile and lookup functions (`NOW`, `ROW`,
`VLOOKUP`…), dates and number formats, locale-specific separators, merged
cells, freeze-pane persistence, column widths in the file, conditional
formatting (OFF-17), data validation (OFF-18), system-clipboard formula
transfer (copy/paste is internal to one adapter) and interchange with other
spreadsheet formats.
