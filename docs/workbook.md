<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Workbook model and calculation engine

`Tk.Workbook` (C++ `QindaTK::Workbook`, `<qindatk/workbook.h>`) is the
authoritative spreadsheet document: ordered sheets with stable IDs, sparse
cells, typed literals and formulas, computed values, cell formats,
conditional formats, validation rules, merged areas, sheet properties,
workbook names and one revision counter. Views never keep cell state.
`Tk.SheetView`, `Tk.FormulaBar`, `Tk.WorkbookTabs`,
`Tk.ConditionalFormatEditor` and `Tk.DataValidationEditor` present and edit
it. `Tk.WorkbookSheetAdapter` feeds `Tk.EditableGrid` as a domain source
([editable-grid.md](editable-grid.md#domain-sources)). The Workbook is a
[document model](document-models.md): its steps go to the shared
`UndoService`, and `DocumentManager`/`DocumentSession` open, save and
recover it through its built-in codec ([sessions](#sessions)). This page is
the contract for WP-055; [controls.md](controls.md#workbook-and-spreadsheet)
lists the QML members.

## Document model

| Concept | Rule |
| --- | --- |
| Sheet identity | `sheet-N` IDs come from a counter that is persisted and never rolls back, so an ID is never reused in a document, even after undo or reopen. Order and names are separate, editable state. |
| Sheet names | 1–31 UTF-16 units, none of `[ ] : * ? / \`, no leading/trailing `'`, no control characters, unique ignoring case. |
| Dimensions | Each sheet declares `rowCount × columnCount` (default 1000 × 26), up to the engine grid of 1,048,576 × 16,384 (`A1:XFD1048576`). Row/column insertion grows the declaration; a sheet keeps at least one row and column. Cells are stored only where they have content, so memory follows content, not dimensions. |
| Cell content | Empty, a literal (`number`, `text`, `boolean`, `error`) or a formula. A formula cell also caches its computed value. |
| Address | A row/column position. Views give rows/columns stable grid IDs, but formulas refer to positions and are rewritten by structural edits (below). |
| Sheet features | Cell formats, conditional formats, validation rules, merged areas and sheet properties (tab color, hidden, protected, frozen panes, row/column sizes) belong to one sheet. They are stored as rectangles, not per cell, and follow structural edits ([formats and rules](#formats-rules-and-names)). |
| Names | Workbook-wide names for one rectangle on one sheet ([names](#names)). |

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
primary     = number | string | TRUE | FALSE | error | reference | name
            | function "(" [ comparison { "," comparison } ] ")" | "(" comparison ")"
reference   = [ sheet "!" ] ( cell [ ":" cell ] | columns | rows )
cell        = ["$"] column ["$"] row                 ; A..XFD, 1..1048576, any case
columns     = ["$"] column ":" ["$"] column          ; whole columns: A:A, $B:$D
rows        = ["$"] row ":" ["$"] row                ; whole rows: 1:1, $3:$5
name        = (letter | "_") { letter | digit | "_" | "." }   ; see Names
sheet       = plain-name | "'" name-with-'' "'"      ; quoted when not [A-Za-z_][A-Za-z0-9_.]*
string      = '"' { any | '""' } '"'
```

Whitespace between tokens is ignored (there is no intersection operator).
Arguments are separated by `,` and numbers use `.` regardless of locale.
Unknown functions and wrong argument counts are parse errors. A word that is
a valid name parses as a name even when no such name is defined yet; it
evaluates to `#NAME?` until one is. A word that cannot be a name, such as a
function name without `(` or an address outside the grid (`XFE1`), is a
parse error. Limits: 8,192 UTF-16 units typed (stored formulas may render up
to 8× longer after reference rewrites), 64 nested parentheses/calls/unary
prefixes, AST height 1,024, 4,096 nodes, 255 arguments. Every error names a
span `[start, end)` into the input, counting the leading `=`;
`Workbook.checkInput` returns it without changing anything, along with every
reference's span, target sheet and rectangle.

Canonical rendering is uppercase, without whitespace, keeps explicit
parentheses, writes numbers in shortest round-trip form with a two-digit
upper-case exponent (`1.5E-07`), normalizes ranges per axis (`$B2:A$1` →
`A$1:$B2`, `C:A` → `A:C`), writes names in their defined case and quotes
sheet names when needed (`'Q3 plan'!A1`, `'2024'!A1`). Parsing a rendering
reproduces the same formula.

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

Whole columns, whole rows and names work wherever a range does. A range
function visits only stored cells, so `SUM(A:A)` costs the column's content,
not 1,048,576 positions.

### Values, coercion and errors

- Blank is 0 in arithmetic, `""` in `&`, and takes the other operand's type
  in comparisons. A formula whose result is a blank reference yields 0.
- Arithmetic converts booleans to 1/0 and numeric text exactly as typed
  input does; other text is `#VALUE!`. Results that are not finite are
  `#NUM!`, except division by zero and `0^negative` (`#DIV/0!`); `0^0` is
  `#NUM!`.
- `&` joins General display text: numbers use up to 15 significant digits
  (`1/3&""` is `0.333333333333333`), booleans `TRUE`/`FALSE`. Text longer
  than 32,767 units is `#VALUE!`.
- Comparisons never coerce across types: Number < Text < Boolean; text
  compares case-insensitively (Unicode simple case folding).
- A multi-cell range used as a single value is `#VALUE!`; a 1×1 range is its cell.
- Errors propagate left to right: `=1/0+#N/A` is `#DIV/0!`.
- Display: the cell's [number format](#number-formats) when it has one,
  otherwise General: up to 15 significant digits, exponent form from 1E+15
  upward and below 1E-4 (`0.00001` shows `1E-05`), `TRUE`/`FALSE`, error
  literals, text unchanged.

## References

| Operation | Rule |
| --- | --- |
| Copy, paste, fill | Relative row/column parts move by the distance from the source cell; `$` parts stay. Whole columns move only across columns and whole rows only down rows (`A:A` filled right is `B:B`). A reference moved outside `A1:XFD1048576` becomes `#REF!` (`=G1*2` filled up from G2 to G1 becomes `=#REF!*2`). Implicit-sheet references follow the destination sheet; explicit `Sheet!` references keep their sheet. |
| Fill | The source block repeats over the target in both directions, aligned to the source origin. Literals are copied, not extrapolated into series. `WorkbookSheetAdapter.fillDown/fillRight` follow Excel: a multi-row selection copies its top row down; a single row copies the row above. |
| Insert rows/columns | Every reference into the sheet keeps its target cell, regardless of `$`. A range spanning the insertion point grows; whole columns ignore row edits and whole rows ignore column edits. A reference pushed past the grid becomes `#REF!` and is recalculated; otherwise values are unchanged and nothing is recalculated (measured: 0 evaluations). |
| Remove rows/columns | A single-cell reference into the removed band becomes `#REF!`. A range loses the removed band and becomes `#REF!` only when all of it is removed. Only formulas whose value can change are recalculated. |
| Rename/reorder sheets | References hold sheet IDs, so identity is unchanged; formula text re-renders with the new name. Nothing recalculates. A name typed after a rename must be the new one. |
| Remove sheet | Every reference to it becomes `#REF!` (`=Data!A1*2` → `=#REF!*2`). Undo restores the sheet (same ID) and every rewritten formula exactly. |
| Rule formulas | Conditional-format and validation formulas are relative to their rule's first cell and follow the same rules, including `#REF!`. |

## Names

`defineName(name, sheetId, firstRow, firstColumn, lastRow, lastColumn)` adds
or redefines a workbook name, and `removeName(name)` removes one; each is one
undo step. `names` lists `{name, sheetId, valid, reference, firstRow,
firstColumn, lastRow, lastColumn, range}` sorted by name. A name has 1–255
ASCII letters, digits, `_` and `.`, starts with a letter or `_` and compares
ignoring case. It cannot look like a cell (`A1`, `XFD9`), an R1C1 reference
(`R`, `C`, `R1C2`), `TRUE`/`FALSE` or a function name.
`nameProblem(name)` names the first rule a candidate breaks. A workbook
holds at most 4,096 names.

A formula stores the name, not its target: `=SUM(Totals)` follows a
redefinition and recalculates. After `removeName` it shows `#NAME?`, and a
new definition makes it work again. Every change to the name table rebuilds
the dependency index, so formulas that use a name recalculate when the name
or its cells change. A name holds one absolute rectangle. Row/column edits
move, grow and shrink it like a range. A name whose cells were all removed,
or whose sheet was removed, stays in `names` as `#REF!` (`valid: false`) and
evaluates to `#REF!`; undo restores it exactly.

## Transactions, history and revision

Every operation validates everything first and is one atomic history step.
That covers `setCellInput`, `setCellInputs`, `pasteInputs`, `pasteTransfer`,
`copyCells`, `fill` and `clearCells`; row, column and sheet operations;
formatting, merges, rules, names, sheet properties and sizes.
`beginTransaction(label)` groups several operations into one step;
`commitTransaction` recalculates and records it; `cancelTransaction`
restores the exact prior state, values included. A failing operation inside
an open transaction rolls back only itself. While a transaction is open,
`undo`/`redo`/`save` are refused and values are not yet recalculated.

Each step is a `WorkbookCommand` recorded by an `UndoService`: the
mutations the operation applied and their exact inverses, bound to the
document ID and reset generation. Under a linear history the two lists stay
valid for every later undo and redo, so undo reproduces the previous
serialization byte for byte (except the sheet-ID counter). A feature edit
builds a new immutable copy of the sheet's features (or of the name table)
and swaps it in, so its inverse is the previous copy. The operation applies
the transaction first; the service then records the step and publishes it,
which recalculates and announces. If the service refuses the step, the
whole transaction rolls back and the operation fails with the service's
message. A standalone workbook records into a private service (200 steps,
`StandaloneHistoryBytes` = 1 GiB); a `DocumentSession` moves that history
into the shared service, whose `memoryLimit` applies (8 MiB unless the host
raises it). A step whose estimated undo data exceeds the budget is refused
(“Edit exceeds undo memory limit”) rather than recorded as not undoable,
and older steps are evicted first. Adjacent cell edits of one step share a
single mutation. Even so, a 45,002-cell transaction keeps about 13 MiB, and
at the feature limits (4,096 format areas and 16,384 merged areas) one
format step keeps 768 KiB, so hosts that edit large workbooks raise
`memoryLimit`. `UndoService` macros group
workbook steps (`composeCommands`), and `undoService` names the service that
holds the history. `revision` increases on every commit, undo, redo, restore
and reset. `modified` is the service's clean state: `save`, `open` and
`markSaved` mark the current position clean, and `markUnsaved` marks the
current state as not saved anywhere (an adopted import). An input equal to
the current content (after canonicalization) records nothing.

## Recalculation

- The dependency index maps each referenced cell to its formulas and each
  distinct range to its formulas, bucketed by column (ranges wider than 32
  columns, whole rows included, are indexed per sheet).
- Commit seeds recalculation with the changed positions. Every formula
  reachable through the index is dirty, ordered with Kahn's algorithm and
  evaluated once. No work is done for formulas outside that closure.
- Cycles are found on the static graph with iterative Tarjan SCC over the
  unresolved remainder. Members of a cycle (including self and range
  self-references such as `F10: =SUM(F1:F10)`) are `#CYCLE!`; formulas that
  depend on a cycle evaluate normally and usually propagate it (`IFERROR`
  can absorb it). Chains and cycles of 50,000 cells need no recursion.
- `statistics` reports `lastDirty`, `lastEvaluated`, `lastCycleCells`,
  `lastRecalcMicroseconds`, `totalEvaluations`, index sizes and
  `historySteps`, `historyCursor` and `historyBytes`.
- Range functions scan stored cells: a range of at most 4,096 positions
  probes them; a larger one walks the stored cells of its row band. A
  running total such as `=SUM(A$1:A1)` filled down is quadratic.

<a id="formats-rules-and-names"></a>
## Formats and rules

Rules and styles are maps. The QML API, the editors, the clipboard payload
and the native file use the same keys, and every error names the offending
field. Rule ranges are plain `A1`/`A1:B5` texts inside the rule's sheet (a
list or a space-separated string), at most 64 per rule. Rule values are
typed like cell input, or `=formula`.

A protected sheet refuses cell edits, pastes, fills, clears, row/column
edits, formats, merges, rules and sizes. Sheet properties, renaming,
reordering, removal and names stay available, so a sheet can be unprotected.

### Cell formats

`applyStyle(sheetId, firstRow, firstColumn, lastRow, lastColumn, patch)`
sets only the keys it names:

| Key | Values |
| --- | --- |
| `numberFormat` | A [number format](#number-formats); `General` resets it. |
| `horizontalAlignment` | `general` (by value type), `left`, `center`, `right`. |
| `bold`, `italic`, `underline`, `strikeout` | Booleans. |
| `textColor`, `fillColor` | Document colors `#rgb`/`#rrggbb` (stored as `#rrggbb`); `""` restores the theme default. |

Formats are layers `{area, style}` per sheet; a later layer wins per key.
A patch drops older layers inside its area whose keys it replaces entirely,
so reformatting the same cells does not grow the list. `clearFormats` cuts a
rectangle out of every layer (at most four pieces each). A sheet holds at
most 4,096 layers, and an edit beyond that is refused. `copyCells`, `fill`
and `pasteTransfer` carry formats with the cells: the target takes the
source block's layers, tiled like the values, and loses its own (at most
2,048 new layers per operation).

`cellFormat(sheetId, row, column)` returns a cell's explicit keys.
`cellPresentation` returns what a view draws, resolved in this order:

1. The explicit formats.
2. Conditional formats, which win per key ([below](#conditional-formats)).
3. `horizontalAlignment` by value type while it is `general`: numbers
   right, booleans and errors centered, text left.
4. Extra keys: `dataBar` (0–1) and `dataBarColor`, `icon` and `iconTone`
   (`negative`, `neutral`, `positive`), `error: true` for error values and
   `numeric: true` for numbers. A view shows a number that does not fit its
   column as `###`, never cut short.

### Number formats

A number format changes only how a number is displayed; `cellValue`,
formulas and `&` keep the number. Text, booleans and errors ignore it.
`numberFormatProblem(format)` returns `""` for a supported code.

| Code | Meaning | Example (1234.5) |
| --- | --- | --- |
| `General` or `""` | General display (above). | `1234.5` |
| `@` | Text: General display. | `1234.5` |
| `0`, `0.00`, `#,##0`, `#,##0.00`, `.00` | Integer digits (`0` pads, `#` does not), optional `,` grouping, up to 15 decimals. Rounded half away from zero like `ROUND`. | `#,##0.00` → `1,234.50` |
| `0%`, `0.0%` | Percent: the value × 100. | `0%` → `123450%` |
| `0.00E+00` | Scientific: 1–3 exponent digits. | `1.23E+03` |
| `yyyy-mm-dd`, `d/m/yy`, `dddd`, `mmm yyyy`, `h:mm`, `hh:mm:ss AM/PM` | Dates and times over serial days since 1899-12-30 (1 = 1899-12-31, 0.5 = noon; serials from 61, 1900-03-01, match other spreadsheets). `m` next to `h` or `s` is minutes; `mmm`/`mmmm` and `ddd`/`dddd` are C-locale names. | `yyyy-mm-dd` → `1903-05-18` |

Either kind may have a literal prefix or suffix: quoted text, `\x`, or any
of `$ € £ ¥ - + ( ) / : , .` and space (`"USD "#,##0`, `0.0 "kg"`). A
negative number shows `-` before the prefix. Dates outside
0 ≤ serial < 2,958,466 (year 10000) show `######`. There are no sections
(`;`), colors, conditions or locale-specific separators. At most 64
characters.

### Conditional formats

A sheet keeps up to 256 conditional formats in priority order (index 0
first). `setConditionalFormat(sheetId, rule, priority = -1)` adds a rule
(empty or unknown `id`: a new `cfN` ID) or replaces the rule with that ID,
validates it completely and returns the ID, or `""` on failure. Priority −1
keeps a replaced rule's place and puts a new rule first.
`moveConditionalFormat`, `removeConditionalFormat`, `conditionalFormats`
and `checkConditionalFormat` (`{ok, message}`, nothing changes) complete the
set.

| `type` | Matches or draws | Keys |
| --- | --- | --- |
| `cellValue` | The value compared with `value1` (and `value2` for `between`/`notBetween`); blank counts as 0, errors never match. | `operator`: `between`, `notBetween`, `equal`, `notEqual`, `greater`, `less`, `greaterOrEqual`, `lessOrEqual`; `style` |
| `expression` | `value1` is a formula that is `TRUE` for the cell (relative to the rule's first cell, like a filled formula). | `value1` (`=…`), `style` |
| `containsText` | General display text contains `text`, ignoring case. | `text`, `style` |
| `blank`, `notBlank`, `errors`, `noErrors` | The value's kind (empty text counts as blank). | `style` |
| `colorScale` | Fill interpolated from `minColor` (default `#f8696b`) through optional `midColor` to `maxColor` (default `#63be7b`) over the numeric minimum and maximum of the rule's ranges. | colors |
| `dataBar` | `dataBar` = (value − min)/(max − min) in `maxColor` (default `#638ec6`). | `maxColor` |
| `iconSet` | Three icons by thirds of the same span: `arrow-down`, `arrow-right`, `arrow-up`. | — |

Rules apply in priority order. The first rule that sets a key keeps it, and
`stopIfTrue` ends evaluation after its rule matches. Every conditional key
wins over the cell's own format. Scales, bars and icons ignore cells that do
not hold numbers; when all values are equal, a scale uses `maxColor`, a bar
is full and the icon is neutral. Their minimum and maximum are computed once
per workbook revision. `previewConditionalFormat(sheetId, rule, firstRow,
firstColumn, rowCount, columnCount)` returns `cellPresentation` records (with
`row`, `column`, `display`) for at most 4,096 cells as if `rule` were set. It
runs the same code on a candidate copy of the rules, so the preview equals
what commits, saves and reopens.

### Data validation

Each cell has at most one validation rule; a sheet keeps up to 256.
`setValidation(sheetId, rule)` adds (`dvN` IDs, appended) or replaces a
rule, and the new rule takes its cells from older rules, which are split
around it (at most 64 ranges each; a rule left with no cells is removed).
`removeValidation`, `validations`, `checkValidation` and
`validationAt(sheetId, row, column)` complete the set.

| `type` | Accepts | Keys |
| --- | --- | --- |
| `any` | Everything (input messages only). | — |
| `wholeNumber`, `decimal` | A number (integral for `wholeNumber`) within the bounds. | `operator`, `value1`, `value2` (numbers or formulas) |
| `textLength` | Display text whose length is within the bounds. | as above |
| `list` | Display text equal to an item, ignoring case. `items` holds up to 1,024 inline items of 1–1,024 characters; otherwise `value1` names a source range or name (`=$D$1:$D$9`, `=Categories`) whose non-blank values are the items. | `items` or `value1` |
| `custom` | `value1` is `TRUE` with the candidate in place, so `=B2<=C2` sees the new B2. | `value1` (`=…`) |

Shared keys: `allowBlank` (default true; blank or empty text passes),
`inputTitle`/`inputMessage` (shown while the cell is active), `errorStyle`
(`stop`, `warning`, `information`) and `errorTitle`/`errorMessage`. Without
an error message the engine writes one (“Enter a whole number between 1 and
10”, “Choose one of: Rent, Travel, Food”). `validationItems(sheetId, row, column)`
returns a list rule's current items.

`validateInput(sheetId, row, column, input)` returns `{ok, style, title,
message, ruleId}` (or the parse error with its span) without changing
anything. `setCellInput` refuses only `stop` rules; warning and information
rules are the view's question to ask (SheetView asks). Pastes validate every
cell and follow a policy. `allOrNothing` (the default) applies nothing when
any cell fails to parse or breaks a stop rule. `skipInvalid` applies every
other cell. Cells that break a warning or information rule are applied
under both policies and reported. `lastPasteReport` (and the `pasteReported`
signal) gives `{policy, ok, applied, invalid: [{row, column, address,
message, style, ruleId}]}`; a parse failure has `syntax: true` with its
span. A partial paste is still one undo step.

### Merged cells

`mergeCells(sheetId, firstRow, firstColumn, lastRow, lastColumn)` merges a
rectangle of at least two cells into one area. It replaces merged areas it
intersects, keeps the top-left input and clears the other cells, all in one
step. `unmergeCells` removes every merged area that intersects the
rectangle. `mergedAreas(sheetId, …window)` and `mergeAt(sheetId, row,
column)` return `{firstRow, firstColumn, lastRow, lastColumn, range}`.
Areas never overlap; a sheet holds at most 16,384. Row/column edits grow and
shrink them, and an area reduced to one cell disappears. `copyCells` and
`pasteTransfer` carry merged areas that lie entirely inside the copied
block; `fill` does not. Views draw the top-left value across the area and
edit only that cell.

### Sheet properties and sizes

`sheetProperties(sheetId)` returns `{tabColor, hidden, protected,
frozenRows, frozenColumns}`. `setSheetProperties(sheetId, changes)`
validates every key before changing any: `tabColor` is `#rrggbb` or `""`,
`hidden` and `protected` are booleans, and `frozenRows`/`frozenColumns` are
0 to the sheet's rows/columns − 1. Unknown keys are refused, and a workbook
keeps at least one visible sheet. `setColumnWidth(sheetId, column, width)`
and `setRowHeight(sheetId, row, height)` store sizes in device-independent
pixels (1–100,000; ≤ 0 restores the default); `columnWidths`/`rowHeights`
return `{"<index>": size}`. Sizes move with row/column edits and disappear
with their row or column.

## Native file format

`serialize()`/`restore()`, `save(url)`/`open(url)` and the FormatRegistry
provider `qindatk.workbook.v1` (`.qtkwb`, `WorkbookFormat` in
`<qindatk/workbook_format.h>`) share one codec. Files are compact UTF-8 JSON
with sorted keys (shown wrapped):

```json
{"documentId":"<uuid>","format":"qindatk.workbook",
 "names":[{"name":"Totals","ref":"Budget!F2:F5"}],"nextSheetNumber":3,
 "sheets":[{"cells":[["A1","s","Item"],["B2","n",1200.5],["C2","b",true],
                     ["D2","e","#N/A"],["F2","f","=SUM(B2:E2)"]],
            "columnWidths":{"A":140},"columns":26,
            "conditionalFormats":[{"id":"cf1","operator":"greater","ranges":["F2:F5"],
                                   "style":{"bold":true},"type":"cellValue","value1":"4000"}],
            "formats":[{"range":"B2:F5","style":{"numberFormat":"#,##0.00"}}],
            "frozenRows":1,"id":"sheet-1","merges":["A7:C7"],"name":"Budget",
            "rows":1000,"tabColor":"#3b82f6",
            "validations":[{"id":"dv1","operator":"between","ranges":["B2:E5"],
                            "type":"decimal","value1":"0","value2":"100000"}]}],
 "version":2}
```

Cells are row-major `[address, type, value]` with `n` number (shortest
round trip), `s` non-empty text, `b` boolean, `e` error literal, `f` canonical
formula text naming sheets. Computed values are not stored; opening
recalculates every formula deterministically, so `serialize(restore(bytes))`
equals `bytes`. Version 2 adds the per-sheet `formats`,
`conditionalFormats`, `validations` (rule maps with defaults omitted),
`merges`, `tabColor`, `hidden`, `protected`, `frozenRows`, `frozenColumns`,
`columnWidths` (by column letter) and `rowHeights` (by 1-based row), and the
workbook `names` (`ref` is `#REF!` for an invalid name). A workbook with no
names and no sheet features is written as version 1, so older files
re-save byte-identically. Restore reads versions 1 and 2 and validates
everything before replacing anything: format, version, unknown fields, IDs,
names, dimensions, addresses, duplicates, literal types, every formula with
its span, every rule, overlapping merges or validation areas, rule IDs and
sizes. Files written through the [codec](#sessions) also carry the session
header: `revision` and `resetGeneration` as decimal strings and the
session's `extensions` object (at most 16 MiB). `serialize()` never writes
it, and a file without it opens as revision 1. A restore invalidates the
history. `save` writes atomically with
`FormatIO::writeAtomic`; an existing file is replaced only if it still has
the SHA-256 this workbook last read or wrote there (or with
`replaceExisting` for a file it has not seen).

<a id="clipboard"></a>
## Clipboard

`rangeTransfer(sheetId, firstRow, firstColumn, lastRow, lastColumn)`
captures a rectangle of at most 65,536 positions and 30,000 filled cells as
`{ok, range, text}`. `range` is a versioned `qindatk.workbook.range`
payload. It records the source document ID, sheet ID and origin, the size,
the workbook's sheet name → ID map, the stored inputs as `[row, column,
input]` relative to the origin, and the formats and merged areas inside the
rectangle. `text` holds the displayed values, tab-separated, one line per
row with a trailing newline; tabs and line breaks inside a value become
spaces. `SheetView.copySelection()` puts both on the system clipboard
through `ClipboardService.copyRange` ([transfers.md](transfers.md#ranges)).

`pasteTransfer(sheetId, row, column, range, policy)` pastes such a payload
as one “Paste cells” step. Every target position takes the range's input
(blank positions clear it); then the range's formats and merged areas
replace the target's. Formulas are re-read first. In the document they came
from, sheet names resolve through the copied name → ID map, so a rename after
the copy does not matter. In another workbook they resolve by name, and a
sheet that does not exist there becomes `#REF!`. Relative references then
move by the offset from the source origin, as in `copyCells`. The cells go
through validation and the paste policy. A damaged or foreign payload
changes nothing. `SheetView.pasteCopied()` uses a range payload first, then
plain text (tab-separated values under `pastePolicy`), then the adapter's
in-view copy.

<a id="interchange"></a>
## Interchange

`registerFormat(registry)` adds three providers to a `Tk.FormatRegistry`:
the native `qindatk.workbook.v1` (import, export, roundtrip) and `text.csv`
and `text.tsv` (`WorkbookDelimitedFormat`, `<qindatk/workbook_delimited.h>`;
import and export). Each lists `qindatk.workbook.v1` as its only schema, so
export dialogs refuse other documents. A workbook in a session exports with
`FormatRegistry.startExportSession(session, url, formatId, options)`, and
a standalone Workbook, a `SnapshotSource`, with
`preflightExportSource(workbook, …)`/`startExportSource(…)`
([format-registry.md](format-registry.md#snapshot-sources)). Imports produce
native workbook bytes: `FormatRegistry.adoptImport(manager, jobId)` opens
them as a new, unsaved session, and `Workbook.adoptImport(registry, jobId)`
replaces a standalone workbook's content, all-or-nothing like `restore`
(unsaved, no file URL).

| Delimited text | Import | Export |
| --- | --- | --- |
| Encoding | Strict UTF-8, optional byte-order mark; invalid bytes fail the job. | UTF-8; `byteOrderMark: true` adds a mark. |
| Structure | RFC 4180 quoting (`""`, delimiters and line breaks inside quotes); at most 1,000,000 cells, 1,048,576 rows and 16,384 columns. | Fields are quoted when they contain the delimiter, a quote or a line break, or start or end with a space. Lines end `\r\n` (CSV) or `\n` (TSV) unless `lineEnding` is `lf` or `crlf`. |
| Values | Numbers, `TRUE`/`FALSE` and error literals as typed input. A field starting with `=` stays text unless `formulas: true`; a warning counts them, and unparseable formulas stay text. Other fields are text as written. | The displayed values (number formats applied) of one sheet's used range. |
| Sheets | One sheet named `sheetName` (default `Sheet1`), at least 1000 × 26. | `sheet` names the sheet (any case); by default the first visible sheet. |
| Declared losses | — | Only what the workbook has: `formulas`, `otherSheets`, `names`, `formats`, `mergedCells`, `conditionalFormats`, `validation`, `sheetProperties`. |

XLSX and ODS are not provided. Both are ZIP packages of XML parts, and the
toolkit has no package reader or writer. Their fidelity (styles, shared
formulas, function coverage) is the subject of WP-089.

<a id="sessions"></a>
## Sessions and shared history

`workbookDocumentCodec()` (`<qindatk/document_codec.h>`) makes
`qindatk.workbook.v1` a built-in schema of `DocumentCodecRegistry`, next to
the reference and canvas schemas ([document-models.md](document-models.md#codecs)).
Everything in [persistence.md](persistence.md) then applies to workbooks:

- `DocumentManager.createDocument("qindatk.workbook.v1")` creates a session
  whose `model` is a new Workbook. `openDocument(path)` recognizes a
  workbook file by content, decodes it with full validation and opens a clean
  session. `adoptBytes` and `FormatRegistry.adoptImport(manager, jobId)`
  open imports as unsaved sessions. The session owns the Workbook and adopts
  its history into the manager's `UndoService`.
- A save captures a plain copy of the content on the GUI thread (cells,
  formulas, features and names are shared immutable data). The worker
  encodes it with the session header and `extensions`, appends a recovery
  frame and replaces the file through the store. The codec's `inspect`
  checks the payload against the captured header.
- Edits made while a save runs stay dirty: only the captured history
  position becomes clean, and undoing back to it is clean again. A failed
  save keeps the session dirty and blocks `closeDocument(…, Save)`. Save
  All, close negotiation, autosave, workspace restore and
  `restoreRecovered` (the newest valid frame, reopened dirty at its original
  path) are the shared framework's own.
- `undoService.activeModel = workbook` routes the shared undo/redo commands
  and labels to the workbook, whose own `undo()`, `canUndo`, `undoText` and
  `modified` read the same history.
- A workbook in a session keeps its document identity: `restore()` of a
  file with another document ID and `reset()` are refused. Open or create
  another document through the manager instead. `restore()` of the same
  document replaces the content and leaves the session dirty.

## Views and adapter

`WorkbookSheetAdapter` gives `EditableGridModel` the sheet's dimensions as
compact generated row/column ID runs, and runs the grid in `sourceCommits`
mode through a `GridCellSourceExtension`. The grid reads display text,
editor text (the source input), `cellPresentation` and merged areas only for
the cells it shows. A committed edit, paste or fill-handle drag reaches the
Workbook as one transaction, and the grid keeps no values or history. A
single-cell edit that breaks a warning or information rule is not applied;
the adapter emits `validationPrompt` and SheetView asks. Workbook changes
call `notifySourceChanged` for the changed rectangle. Structural changes
insert or remove IDs at the changed position, so selections follow content;
a reset or reopen of the same sheet keeps its IDs. Protected sheets make the
grid read-only. Frozen panes and row/column sizes come from the sheet
properties, and a finished header resize is recorded with `setColumnWidth`
or `setRowHeight` (a refused resize snaps back). The adapter falls back to
the first sheet when its sheet is removed or the document is replaced.
`sourceReadCount()` measures the bounded visible work.

<a id="gaps-in-consumed-contracts"></a>
## Consumed contract changes

The first engine slice consumed the WP-019/WP-028 contracts unchanged and
recorded eight gaps; the final visual review found a ninth. All are now
closed by additive contract changes:

| ID | Gap | Resolution |
| --- | --- | --- |
| G-1 | The grid pre-filled its editor from the displayed value and dropped a commit equal to it. | `GridCellSourceExtension::editValue` and `EditableGridModel.sourceCommits` ([editable-grid.md](editable-grid.md#domain-sources)). SheetView's editor workaround is removed. |
| G-2 | No public "source values changed" request. | `EditableGridModel.notifySourceChanged(firstRow, rowCount, firstColumn, columnCount)`. |
| G-3 | `activeRow`/`activeColumn` went stale after moves. | Fixed on the integration branch (`47086db`); SheetView binds the grid's values and its derived copy is removed. |
| G-4 | No per-cell presentation, type-to-edit or fill handle. | `cellStyle`/`mergedRanges` hooks, `layoutCells`, `Tk.GridCellDelegate`, `Tk.GridFillHandle` and type-to-edit in `Tk.EditableGrid`. |
| G-5 | Grid fill copied display values. | `GridCellSourceExtension::fillCells`: the Workbook fills formulas and formats. |
| G-6 | Registry export took only `PropertyDocument`/`DocumentSession`; imports were adopted as sessions. | `SnapshotSource` with `FormatRegistry.preflightExportSource`/`startExportSource`, and `Workbook.adoptImport`. |
| G-7 | `UndoService`/`DocumentSession` were `PropertyDocument`-specific. | The integrated domain-document boundary (D-053): the Workbook is a `DocumentModel` recording `WorkbookCommand`s, with a built-in `DocumentCodec`. The lane's interim `UndoParticipant` routing, recovery validators and `WorkbookSession` were retired when D-053 was merged. |
| G-8 | O(rows + columns) grid ID strings: a 100,000-row sheet took about 190 ms to present. | `GridAxisIds` keeps generated IDs as runs; the adapter measures in `tst_workbook_adapter`. |
| G-9 | The grid drew its selection outline and cell editor under its opaque cells: the editor was hidden, the outline showed only through cell borders (not at all in high contrast), and a frozen strip scrolled under the frozen corner drew over it. | Cells stack by pane; the selection and then the editor draw above them, each clipped to its pane (`EditableGridModel.frozenExtent()`, `frozenRow`/`frozenColumn` in `layoutCells`). Tested by pixel in `tst_sheet_features`. |

## Not supported

3-D references (`Sheet1:Sheet3!A1`), array formulas and spilling, volatile
and lookup functions (`NOW`, `ROW`, `VLOOKUP`…), text, date and statistical
function families, sheet-scoped names, number-format sections and
locale-specific separators, rich text inside a cell, vertical alignment and
wrapping, borders, cell comments, charts, filters and sorting in the
Workbook, and XLSX/ODS interchange.
