<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Rich text and paginated documents

WP-050 adds structured rich text with two kinds of view over one
authoritative document: a continuous editor and a paginated page view. The
QML controls are catalogued in
[controls.md](controls.md#rich-text-and-paginated-documents). The backend
choice and its measurements are decision [D-050](decisions-application.md#d-050--own-the-rich-text-model-and-paginate-per-view).

| Layer | Type | Files |
| --- | --- | --- |
| Model | `RichTextDocument` (QML `Tk.RichTextDocument`) | `src/core/rich_text_document.*`, `rich_text_edit.cpp`, `rich_text_history.cpp`, `rich_text_ops.cpp`, `rich_text_structure.cpp`, `rich_text_table_edit.cpp`, `rich_text_fragment.cpp` |
| Persistence | `RichTextCodec`, `RichTextSessionAdapter` | `rich_text_codec*.{h,cpp}`, `rich_text_session_adapter.*` |
| Layout (one per view) | `RichTextLayout` | `rich_text_layout.h`, `rich_text_shaping.cpp`, `rich_text_pagination.cpp`, `rich_text_table_flow.cpp`, `rich_text_pages.cpp`, `rich_text_mapping.cpp`, `rich_text_navigation.cpp`, `rich_text_painter.cpp` |
| Interactive surface | `RichTextArea` (QQuickItem) | `rich_text_area*.{h,cpp}`, `rich_text_format_state.*`, `rich_text_visible_pages.h` |
| Page drawing and pages | `RichTextTile`, `RichTextPageModel` | `rich_text_tile.*`, `rich_text_page_model.*` |
| Exchange | clipboard, HTML, objects | `rich_text_clipboard.*`, `rich_text_html.*`, `rich_text_objects.*` |
| Controls | `RichTextEditor`, `RichDocumentView`, `FormatBar`, `PageNavigator` | `src/qml/` |

## Model

A document is a list of body blocks, and each block is a paragraph or a
table. A table has rows of cells, and each cell holds one or more
paragraphs. Tables do not nest. The body always ends with a paragraph.
Paragraphs carry a stable UUID, their UTF-16 text, and runs (`Span`: length
plus an index into the document's interned character-format table). They
also carry a paragraph format, a mark format (the typing format of an empty
paragraph) and a revision that every change bumps.

| Concept | Representation |
| --- | --- |
| Character attributes | `CharFormat`: family, size (pt), CSS weight, tri-state italic/underline/strike, colour, background, link `href`, super/subscript, `objectId`, character `styleId`. Empty values inherit from the character style, the paragraph style, heading defaults, then document defaults ([styles](publishing.md#styles)). |
| Paragraph attributes | `ParagraphFormat`: alignment (start/end/left/right/center/justify), direction (auto/ltr/rtl), start/end/first-line indents, space before/after, line height (multiple or exact pt), heading level 0–6, list ID and level 0–8, keep-with-next, keep-together, page-break-before, paragraph `styleId` and its direct `overrides`. |
| Publishing structure | Named styles, merged cells (`TableCell.rowSpan`/`columnSpan`), table styles, text frames and page columns: [publishing.md](publishing.md). |
| Semantic content | Equations (inline objects of kind `equation`), reference fields (`CharFormat.fieldId`), comment and tracked-change marks (`commentIds`, `insertionId`, `deletionId`) and their records: [references-review.md](references-review.md). |
| Lists | `ListDefinition` with 1–9 levels (disc, circle, square, decimal, lower/upper alpha, lower/upper roman; start; indent). Numbering follows document order per list and level. |
| Objects | U+FFFC in the text whose format carries `objectId`; `RichObject` holds kind, placement, size, offsets, gap, source URI and alt text. See [Objects](#objects). |
| Soft line break | U+2028 inside a paragraph (Shift+Enter). |
| Position | `(paragraph UUID, UTF-16 offset, affinity)`. Affinity only picks the caret line at a soft wrap or bidi boundary. |
| Sections | Body ranges separated by `pageBreakBefore` paragraphs. `moveSection` moves one as a single undoable step and keeps block identity. |

Every edit is a transaction of invertible, precondition-checked steps
(`StepText`, `StepSpans`, `StepParagraphFormat`, `StepSplit`, `StepMerge`,
`StepBlocks`, `StepMove`, `StepList`, `StepObject`, `StepDocument`, `StepTable`,
`StepStyle`, `StepFrames`, `StepAnnotation`). Commit also rewrites stale
reference-field results inside the transaction ([D-115](decisions-domains.md#d-115--field-results-are-rewritten-inside-the-committing-transaction)).
A step whose precondition fails, such as a stale paragraph or a changed
format, rejects the whole transaction. The document then keeps its previous
state. Registered `PositionAnchor`s are mapped forward by every applied
step. This is how a selection in one view survives edits made in another.
Each commit emits `contentChanged(ChangeSet)`. Views invalidate from that
set.

### Structured table edits

`RichTextDocument.tables` (every table in document order) and
`tableSnapshot(id)` return the snapshot `Tk.TableEditor` consumes:
`{id, rows:[{id,label,header}], columns:[{id,label,width}], cells:[{id,rowId,
columnId,rowIndex,columnIndex,rowSpan,columnSpan,text,paragraphIds}],
headerRows, canMerge, styleId, styles, maxRows, maxColumns}`; `cells` lists
visible cells only ([merged cells](publishing.md#merged-cells)). Table, row, column and cell IDs
are stable across edits and persistence; `paragraphIds` lets a host move a
text caret into a cell (`RichTextArea.goToParagraph`). The editor's requests
map onto `insertTableRow(tableId, index)`, `removeTableRow(tableId, rowId)`,
`insertTableColumn(tableId, index)`, `removeTableColumn(tableId, columnId)`,
`setTableColumnWidth(tableId, columnId, width)`,
`setTableCellText(tableId, cellId, text)`, `mergeTableCells(tableId,
cellIds)`, `splitTableCell(tableId, cellId)` and `setTableStyle(tableId,
styleId)`. Each returns false and sets `lastError` without changing the
document when the request is invalid.

- Every grid edit is one `StepTable` undo step that swaps the whole grid.
  Unchanged cells keep their paragraph objects, so their IDs, revisions and
  anchors are untouched. Removed cells and their inline objects come back on
  undo. A row inserted inside the header block becomes a header row. New
  cells copy the neighbouring cell's typing format, paragraph format (without
  list membership) and background. A new column also copies the width of the
  column on its left.
- An anchor (caret or selection end) in a removed cell moves to the start of
  the nearest surviving cell: same row and column where possible, else the
  next row or column, or the previous one at the end.
- Widths are relative weights from 1 to 1,000,000. Tables keep at least one
  row and one column and allow at most 10,000 rows and 64 columns.
- `setTableCellText` replaces the text of a one-paragraph cell in one step,
  in the format of the text it replaces. Text that would split the paragraph
  (`\n`, `\r`, U+2029) or contains U+FFFC is refused. Multi-paragraph cells
  and inline objects remain editable in the rich-text views.
- The change set names the table and its added and removed paragraphs, so
  views re-lay the table at its new column widths and repaginate from it.
- Merged cells keep one slot per row and column; a row or column inserted
  inside a merged cell joins it, and deleting a merged cell's first row or
  column moves it on with its ID ([merged cells](publishing.md#merged-cells)).

History is local by default: at most 512 transactions or 64 MiB, with
typing coalesced while the merge key and caret continue. With
`externalUndo` a host keeps the history. It records each
`committed(transactionId, label)` and replays it with
`applyHistoryChanges(transactionId, reverse, expectedRevision)`. It releases
retired IDs with `releaseHistoryTransaction`. This mirrors the grid bridge
of D-040, because `UndoService` records `PropertyDocument` properties only.

## Persistence

The native format is JSON with schema `qindatk.richtext`, versions 1 and 2
(the extension `.qtkrich` is used by the example). Version 2 adds styles,
merged cells, table styles, text frames and page columns
([publishing.md](publishing.md#persistence)), and reference fields,
sources, comments and tracked changes ([references-review.md](references-review.md#persistence));
a document without them is still written as version 1. `RichTextCodec::encode` is
canonical. Formats are re-interned in document order, lists and objects are
sorted by ID, and keys are sorted. Save → reopen → save is therefore
byte-identical. `tst_rich_text_codec` checks canonical encoding on a document that has a
heading, a bold run, a link, a numbered list, inline, float and page
objects, a table with a header row, Hebrew text and a page break.

Table `columnIds` (one UUID per column weight) is an additive member of
version 1. A decoder written before it reports it as an ignored member.
When it is absent, as in files written before WP-051, this decoder derives
each ID as UUIDv5 of the table ID and the column index, so reopening an old
file gives the same IDs every time; the next save stores them. The session
adapter's live encoding writes them too.

`FormatRegistry` registers `qindatk.richtext.v1` for `.qtkrich` import and
export. Import wraps the validated rich-text payload in a reference session's
`org.qindatk.richtext` extension; export reads that extension and emits
canonical `RichTextCodec` bytes. Other session data requires explicit lossy
export acknowledgment.

```json
{ "schema": "qindatk.richtext", "version": 1, "documentId": "<uuid>",
  "page": { "widthPt": 595.276, "heightPt": 841.89, "marginsPt": [72, 72, 72, 72] },
  "defaults": { "family": "sans-serif", "sizePt": 11 },
  "formats": [ {}, { "weight": 700 }, { "href": "https://example.org" } ],
  "lists": [ { "id": "l1", "levels": [ { "kind": "decimal" } ] } ],
  "objects": [ { "id": "o1", "kind": "image", "placement": "floatEnd",
                 "widthPt": 150, "heightPt": 190, "source": "data:…", "alt": "Figure 1" } ],
  "blocks": [ { "paragraph": { "id": "<uuid>", "text": "Bold text", "spans": [[4, 1], [5, 0]],
                               "mark": 0, "format": { "heading": 1, "list": "l1" } } },
              { "table": { "id": "<uuid>", "columns": [1, 1], "columnIds": ["<uuid>", "<uuid>"],
                           "headerRows": 1, "borderPt": 0.75, "paddingPt": 4, "rows": [ … ] } } ] }
```

- `formats[0]` must be `{}` (inherit everything). Spans are
  `[length, formatIndex]` and must cover the text exactly. A U+FFFC must
  carry a format with `object`, and no other character may.
- Paragraph `format` keys: `align`, `direction`, `indentStartPt`,
  `indentEndPt`, `firstLineIndentPt`, `spaceBeforePt`, `spaceAfterPt`,
  `lineHeight`, `lineHeightPt`, `heading`, `list`, `listLevel`,
  `keepWithNext`, `keepTogether`, `pageBreakBefore`, `style`. Defaults are
  omitted.
- Placements are `inline`, `floatStart`, `floatEnd` and `page`. Colours are
  `#rrggbb` or `#aarrggbb`.
- Decoding validates the whole input before it replaces anything. It
  enforces limits: 64 MiB, 10⁶ paragraphs, 10⁶ UTF-16 units per paragraph,
  65,536 formats and 100,000 objects. An error names its field path, for
  example `blocks[3].paragraph.spans`. Unknown members are reported as
  warnings and are not preserved. A future version must bump
  `CurrentVersion` and keep decoding version 1.
- Clipboard fragments use schema `qindatk.richtext.fragment`, which has the
  same blocks plus `openStart` and `openEnd`.

`RichTextSessionAdapter` stores the document as one member of a
`DocumentSession`'s extensions, under the key `org.qindatk.richtext` by
default. Save, Save As, autosave, recovery and stale-save protection are the
session's own (WP-014/WP-019). On attach, an opened or recovered session
supplies the content, and a new session receives the document. Every
committed change is written to the session synchronously, so a save
requested at any moment contains it. The live encoding reuses per-paragraph
JSON by revision, so an unchanged paragraph costs a shared-pointer
comparison. It is valid `qindatk.richtext/1` but not canonical. Measured on
500 pages: first encode about 51 ms; a keystroke plus flush has a median of
6.8–7.5 ms.

## Layout

`RichTextDocument.pageSetupMm` supplies canonical millimetre values for
`PageSetupDialog`. `applyPageSetupMm(settings)` commits document-wide page
size, margins, text columns and column gap through the rich-text history in
one undo step. Landscape and portrait choices orient the physical dimensions
before conversion to the model's point units. Bleed is page setup too
(WP-053); section/page scopes return `Unsupported` and the document stays
unchanged ([printing.md](printing.md#bleed-and-page-scope)).
Columns and linked text frames place the flow in page regions
([publishing.md](publishing.md#text-frames)).

<a id="running-page-regions"></a>
**Running page regions.** `PageSetup::headerText` and `footerText` (JSON
`page.headerText`, `page.footerText`, at most 500 characters, format
version 2) are one line of plain text centred over the content width in the
top and bottom margin of every page, in the document font at 85% size.
`{page}` expands to the page number and `{pages}` to the page count
(`estimatedPageCount()` until pagination completes).
`RichTextLayout::pageRegionText(page, footer)` returns the expanded text and
`paintPage` paints it, so views, print sheets and PDF pages show the same
line. The regions sit outside the content box and never change pagination.
When a completed count changes, `finalizePages` re-stamps every page's
revision. `applyPageSetupMm` keeps them. They are document-wide like the
rest of the page setup; first-page or odd/even variants and fields other
than the two numbers are not modelled. Reports use them for their page
header and footer bands ([forms and reports](forms-reports.md#page-bands)).

`RichTextDocument.exportPdf(path)` paints every paginated page to a local PDF
file using the same `RichTextLayout::paintPage` path as the on-screen view.
The file is committed atomically. It is the default ticket of
`Tk.PrintAdapter`'s PDF destination; printers, page ranges, paper and
scaling, cancellable jobs and preflight are in [printing.md](printing.md).

Each view owns a `RichTextLayout`, while the document stays authoritative,
so two views with different widths or modes never share line state.
Paragraphs are shaped with a standalone `QTextLayout` each (HarfBuzz
shaping, UAX #9 bidi, UAX #14 line breaking and font merging come from Qt).
Metrics are cached per paragraph and revision. At most 2,048 shaped glyph
layouts are kept; line boxes stay cached after their layout is dropped.

- **Units.** Model lengths are points; layout is 96-dpi pixels
  (`PxPerPt` = 4/3). Fonts resolve against a private 72-dpi paint device,
  so a font's "points" are layout pixels, sizes stay fractional, and layout
  is identical on every screen and `QT_FONT_DPI`.
- **Objects in lines.** Qt 6.11 scales absolute letter spacing by
  `int(fontDpi / screenDpi)` (`qtextengine.cpp`), which is 0 on the 72-dpi
  device. An inline object therefore gets its width from *percentage*
  letter spacing on its U+FFFC, exact to Qt's 1/64 px advances. A zero-width
  anchor (float or page object) is laid out as U+2060 WORD JOINER, one for
  one with the model text.
- **Pagination.** Pages are built from a `FlowState`: the block, line, table
  row and cell-line offsets, and the float exclusions active at a page
  start. The layout keeps keep-with-next (headings too) and keep-together
  paragraphs whole. Widow and orphan control keeps two lines together.
  `pageBreakBefore` starts a page. A float sits at its anchor paragraph's
  top edge and narrows the lines beside it. If the float does not fit, the
  paragraph moves to the next page with it. Page objects are placed in the
  content box of the page where their anchor paragraph starts. Tables
  repeat their header rows on continuation pages, and a row taller than a
  page is split by lines.
- **Local invalidation.** A change set marks dirty blocks, and a block-ID
  prefix/suffix diff covers structural edits. Pagination restarts at the
  page holding the block before the first dirty block. It stops as soon as
  a page starts in the same flow state as before; the remaining pages are
  reused unchanged. Measured on 500 pages: typing on page 250 re-shapes one
  paragraph and rebuilds one page. A new line on page 10 converges at the
  next section break, 16 pages later. Without section breaks every later
  page genuinely moves; those pages are rebuilt from cached line metrics
  without re-shaping.
- **Page identity.** A page key is UUIDv5 of its section (the section's
  first block ID) and its ordinal within that section. It is stable when
  earlier sections change length. A page's `revision` changes only when its
  content hash changes, and list numbers are part of that hash.
- **Progressive work.** A view lays out pages through the viewport
  synchronously and the rest in 8 ms slices. Getters never paginate;
  `isLaidOut()` tells whether a position has a page yet.
- **Mapping.** `caretBox`, `hitTest`, `selectionRects`, `objectRect` and
  `pagesContaining` (a binary search over page ranges) map model positions
  to pages and back. `tst_rich_text_layout` maps every grapheme position of
  the hard fixture to its page and back.
- **Qt findings.** In a paragraph with bidi runs,
  `QTextEngine::insertionPointsForLine` stops inside surrogate pairs, so
  visual movement continues to the next grapheme boundary. Qt 6.11.1's
  `QTextBoundaryFinder` misses GB12/GB13 when a regional indicator follows
  an Extended_Pictographic character. `GraphemeBoundaries`
  (`rich_text_graphemes.*`) corrects this.

## Views

`RichTextArea` is the interactive surface of both controls. It has
`paged: false` in `RichTextEditor` and `paged: true` in `RichDocumentView`.
It owns the selection (two document anchors), keyboard, pointer and
input-method handling, scrolling and zoom. It exposes `visiblePages`, a
model of keyed tiles whose roles are `pageIndex`, `pageKey`, `revision`,
`tileRect`, `sourceRect` and `pageRect`. QML draws each tile with
`Tk.RichTextTile`, so only pages in the viewport are materialised: at most
two tiles for a 900×700 view on 500 pages.

- **Keys.** Arrows move visually in bidi text by grapheme; Ctrl+arrows move
  by word. Up/Down keep a goal x. Home/End go to the line edge, and
  Ctrl+Home/End to the document edge. Page Up/Down scroll. Shift extends.
  Backspace and Delete remove a whole grapheme (Ctrl: a word). Enter splits
  the paragraph; in an empty list item it leaves the list. Shift+Enter
  inserts a soft break; Ctrl+Enter inserts a page break. Tab and Shift+Tab
  change the list level. Ctrl+B/I/U, Ctrl+Z, Ctrl+Shift+Z, Ctrl+X/C/V and
  Ctrl+A work as expected. Ctrl+K emits `linkRequested()`; `FormatBar`
  answers it with its link editor.
- **Navigation.** `goToPage` and `PageNavigator` scroll the view and keep
  the logical selection; typing returns to the caret. `currentPage` is the
  page navigated to while it is in view, otherwise the caret's page while
  the caret is in view, otherwise the page at the viewport's centre. A
  selection change ends the navigated state. `cursorRectangle` is in item
  coordinates and notifies on scrolling as well as on caret moves.
- **Pointer.** Click, drag across pages, double-click to select a word and
  triple-click to select a paragraph. Ctrl+click on a link emits
  `linkActivated(href)`; the view never opens URLs itself.
- **Input methods.** Preedit is shown inside the paragraph in the view's
  own layout and never enters the document. Commit replaces the selection
  as one transaction. The area answers `ImCursorRectangle`,
  `ImAnchorRectangle`, `ImSurroundingText`, `ImCursorPosition`,
  `ImAnchorPosition`, `ImCurrentSelection`,
  `ImTextBefore/AfterCursor`, `ImAbsolutePosition`, `ImFont`, `ImHints`
  (multi-line) and `ImReadOnly`.
- **Accessibility.** The area's role is `EditableText`. Its `text`,
  `cursorPosition`, `selectionStart` and `selectionEnd` use plain-text
  offsets: paragraphs are joined by `\n` and objects count as U+FFFC.
  `QAccessibleQuickItem` reads these properties by name, and
  `setCursorPosition` maps an offset back to a position.
- **Publishing structure.** `selectedParagraphIds` (style targets),
  `caretFrameId`, `caretContext` (frame, table cell and style at the caret,
  also in `RichDocumentView`'s accessible description), `frameLayout`,
  `oversetCharacters`, `frameAt(x, y)` and `goToFrame(id)`
  ([publishing.md](publishing.md)).
- **Colours** are plain properties (`inkColor`, `pageColor`, `linkColor`,
  `selectionColor`, `caretColor`, `frameGuideColor`, `oversetColor`, …). The QML controls bind them to
  `Tk.Theme.color.*` roles. Text with an explicit colour in the document
  (run colour or document default) keeps that colour.

## Commands

Each formatting action is one command ID. The area runs it through
`invokeCommand(id, args)` as exactly one transaction, so it is one undo
step. The selection and focus stay put. `commandState(id)` returns
`enabled`, `checked`, `mixed`, `checkState`, `value`, `label`, `iconName`
and `shortcut`. `registerCommands(registry, scopeToken)` puts the same IDs
into a `CommandRegistry` scope, which host menus and the command palette
use. The IDs are public API.

| ID | Arguments | Notes |
| --- | --- | --- |
| `text.bold`, `text.italic`, `text.underline`, `text.strike`, `text.superscript`, `text.subscript` | — | Toggle. A mixed selection turns the attribute on; a collapsed caret sets the pending typing format. |
| `text.fontFamily`, `text.fontSize` | `value` (family; size in pt, 1–1638) | |
| `text.color`, `text.highlight` | `value` (colour string; empty clears) | |
| `text.link` | `href` (or `value`; empty removes) | Ctrl+K requests the editor through `linkRequested()`. |
| `text.clearFormatting` | — | Keeps links and objects. |
| `paragraph.align` | `value`: `start`, `end`, `left`, `right`, `center`, `justify` | |
| `paragraph.heading` | `value`: 0–6 | |
| `paragraph.bulletList`, `paragraph.numberedList` | — | Toggle. Joins the previous paragraph's list when the kind matches. |
| `paragraph.indent`, `paragraph.outdent` | — | List level. |
| `insert.table` | `rows`, `columns`, `headerRows` | |
| `insert.pageBreak` | — | Ctrl+Enter. |
| `insert.object` | `source`, `kind`, `widthPt`, `heightPt`, `alt`, `placement` | |
| `edit.undo`, `edit.redo`, `edit.cut`, `edit.copy`, `edit.paste`, `edit.selectAll` | — | |
| `paragraph.style`, `text.style` | `value` (style ID; empty removes) | Paragraph or character style on the selection ([styles](publishing.md#styles)). |
| `navigate.nextFrame`, `navigate.previousFrame` | — | Ctrl+Alt+Page Down/Up: next or previous text frame of the story. |

`formatState` (`RichTextFormatState`) resolves the selection:
`fontFamily`, `fontSize`, `color`, `link`, `alignment` and `heading`, each
with a `…Mixed` flag. `bold`, `italic`, `underline`, `strike`,
`superscript` and `subscript` are `Qt.CheckState` values, and `list` is
`none`, `bullet`, `number` or `mixed`. `fontMissing` is set when the
selection has a single family and that family is not installed.

## Clipboard

Copy publishes the WP-016 transfer format
(`TransferPayloadCodec::InternalMime`). It holds one `Range` item whose
payload is the fragment JSON and its plain text, alongside `text/plain` and
`text/html` for other applications. Paste prefers the internal fragment,
then HTML, then plain text. Pasted paragraphs and objects get new IDs. A
pasted list reuses an identical definition, and gets a new ID only when its
ID is already taken by a different list. Plain text takes the caret's typing format. HTML is imported through
`QTextDocument`'s parser into a fragment: runs, links, headings, alignment,
lists, tables without spans, and inline images. Unsupported constructs
become text. Imported HTML is hostile input ([HTML import](#html-import)). `ClipboardService` hosts can pass their own
`ClipboardAdapter` with `setClipboardAdapter`.

<a id="html-import"></a>
### HTML import (D-319)

`RichTextHtml::toFragment` (paste, `NotebookDocument.insertHtml`) never
lets HTML make the engine load anything. The parser's document loads no
resource (no `file:` read, no style sheet, no network), and an image keeps
its source only when `RichTextHtml::isImportableImageSource` accepts it: a
`data:image/...` raster URI (not SVG, which can reference other files) or a
URI whose scheme the application registered with
`RichTextHtml::registerImageScheme` (for example `asset` when it renders
`RichTextAssetRenderer`'s `asset:` sources; registering `http`, `https` or
`file` is the application's explicit choice). Every other image — remote,
`file:`, `qrc:`, protocol-relative `//host/...`, relative, `javascript:` —
stays as a placeholder object with its alt text and size and an empty
source. `tst_rich_text_html_import` covers hostile HTML.

## Objects

| Placement | Behaviour |
| --- | --- |
| `inline` | Sits on the baseline and occupies its width in the line; the line grows to its height. |
| `floatStart`, `floatEnd` | Placed at the start or end edge from the anchor paragraph's top. Following lines wrap beside it with `gapPt`. |
| `page` | Placed at `offsetXPt`, `offsetYPt` in the content box of the page where the anchor paragraph starts. |

The document stores geometry, placement, alt text and a source URI only.
Drawing goes through the `RichTextObjectRenderer` seam
(`rich_text_objects.h`). The reference `RichTextImageRenderer` decodes
`file:`, `qrc:` and `data:` images into a 64 MiB bounded cache. When a view's
`assetProvider` is set, `RichTextAssetRenderer` requests each `asset:<resourceId>`
image asynchronously through that provider, decodes on the GUI thread and
passes an immutable image snapshot to the painted tile. Resource invalidation
re-requests the image. Missing or failed content remains a labelled placeholder.
The host still owns resource registration and object geometry editing.

## Fonts and fallback

A family that is not installed shapes with Qt's fallback and never loses
glyphs; `tst_rich_text_layout` checks every script of the fixture.
`RichTextDocument.missingFontFamilies()` lists the missing families, and
`FormatBar` marks the selection's font with `formatBarFontMissing`. The
family list comes from `Tk.FontCatalog`.

## Open integration seams

These depend on packages that were unfinished when WP-050 was built. The
seams are typed; nothing here is a stand-in contract.

| Dependency | State and seam |
| --- | --- |
| WP-019 FormatRegistry | `.qtkrich` import/export is registered. HTML remains an exchange seam, not a registered provider. |
| WP-024 FontPicker | `FontPicker` binds through `PropertyBinding`, which does not fit a selection. `FormatBar` uses `FontCatalog` with a `ComboBox`. |
| WP-025 find/replace | `RichTextFindReplaceService` connects `FindReplaceBar` to this document. It searches editor-compatible UTF-16 text within paragraphs, returns paragraph IDs and offsets, skips object-containing matches and invalid grapheme boundaries, rejects stale revisions, and commits replace-all as one undo step. Matches cannot cross paragraph boundaries. |
| WP-033 shared framework | Not available; the controls have not been verified against it. |
| WP-040 canvas / WP-017 assets | `asset:` images resolve through `assetProvider` on either rich-text view. Canvas geometry editing remains a host adapter. |
| UndoService | `PropertyDocument`-only; use `externalUndo` (D-050). |
| WP-051 | Named styles, merged cells, table styles, linked text frames and page columns are document operations ([publishing.md](publishing.md)). Section and page scopes and bleed belong to WP-053. |
| WP-052 | Equations, reference fields, comments and tracked changes are document content ([references-review.md](references-review.md)). Footnotes need a page note area and are refused. |
| WP-053 | Bleed is page setup; section and page scopes stay refused (D-133). Printing and preflight: [printing.md](printing.md). |
