<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Publishing structure: styles, merged cells, text frames and columns

WP-051 adds publishing structure to the [rich-text model](rich-text.md):
named styles with inheritance, merged table cells, text frames that a story
flows through, and text columns in the page setup. Every operation is a
`RichTextDocument` transaction, so it is one undo step, it reaches every
view's layout, and it persists in `qindatk.richtext` version 2. The panels
(`StyleManager`, `TableEditor`, `TextFrameEditor`, `PageSetupDialog`) only
send requests; [examples/document-publishing](../examples/document-publishing/Main.qml)
binds all four to one document. Decisions: [D-089](decisions-domains.md#d-089--materialize-style-values-and-record-direct-overrides),
[D-090](decisions-domains.md#d-090--merge-cells-over-a-fixed-slot-grid),
[D-091](decisions-domains.md#d-091--flow-one-story-through-page-regions)
and [D-104](decisions-domains.md#d-104--write-richtext-version-2-only-for-publishing-structure).

| Part | Files |
| --- | --- |
| Rules (style resolution, frame and grid validation) | `src/core/rich_text_publishing.{h,cpp}` |
| Document operations | `rich_text_style_edit.cpp`, `rich_text_table_spans.cpp`, `rich_text_table_edit.cpp`, `rich_text_frames.cpp`, `rich_text_page_setup_mm.cpp` |
| Persistence | `rich_text_codec_publishing.cpp`, `rich_text_codec*.cpp`, `rich_text_session_adapter.cpp` |
| Layout | `rich_text_regions.cpp`, `rich_text_table_flow.cpp`, `rich_text_pagination.cpp` (width breaks), `rich_text_painter.cpp` |
| View | `rich_text_area_structure.cpp`, `rich_text_area_commands.cpp` |

## Styles

A `NamedStyle` has an ID, a unique name (compared without case), a kind
(`paragraph`, `character` or `table`) and an optional parent of the same kind.
Parents never form a cycle, and a style never names itself.

| Kind | Members | Applies to |
| --- | --- | --- |
| `paragraph` | alignment, direction, start/end/first-line indents, space before/after, line height (multiple or exact), heading level, keep-with-next, keep-together; and character members | whole paragraphs (`ParagraphFormat.styleId`) |
| `character` | family, size, weight, italic, underline, strike, colour, background, baseline | runs (`CharFormat.styleId`) |
| `table` | border width, cell padding, header-row background, band background (every second body row) | tables (`Table.styleId`) |

Resolution runs from the root of the parent chain to the style; a child's
member wins over its parent's. The value of a character attribute is the
run's direct value, else its character style's, else its paragraph style's,
else the heading defaults and document defaults. `effectiveCharFormat()`
gives that result; shaping, `formatState`, painting and HTML export use it.

Paragraph members are **materialized**: a styled paragraph's
`ParagraphFormat` holds the resolved values, so pagination, lists, HTML and
every existing reader see plain values. `ParagraphFormat.overrides` marks the
members set directly on that paragraph:

- `setParagraphFormat` (and the paragraph commands) on a styled paragraph
  sets the value and marks it as an override.
- Changing a style (`updateStyle`) or a parent is one transaction: a
  `StepStyle` plus a `StepParagraphFormat` for each paragraph of that style
  or of a style inheriting from it. Overridden members keep their values.
  Only those paragraphs are re-shaped; pagination restarts at the page
  before the first of them.
- Applying a paragraph style clears the paragraph's overrides. Removing a
  paragraph's style (an empty style ID) keeps its current values as direct
  formatting.
- `resetDirectFormatting(paragraphIds, keys)` clears overrides
  (`paragraph.<key>`) or direct run attributes (`character.<key>`); an empty
  key list clears all of them. Links, objects and character styles stay.
- `directOverrides(paragraphIds)` lists `{key, label, value}` rows for
  `StyleManager.directOverrides`; a member that differs between the
  paragraphs reports `mixed`.
- Enter at the end of a styled heading starts an unstyled body paragraph.

| Method (QML) | Effect |
| --- | --- |
| `styles`, `styleKinds` | Snapshots `{id, name, kind, parentId, parentName, builtIn, summary, preview, properties}` sorted by name; `properties` is the persisted form. |
| `createStyle(name, kind, parentId)` | Returns the new ID, or "" with `lastError`. |
| `updateStyle(id, patch)` | `patch`: `name`, `parentId` and `character`, `paragraph` or `table` maps in the persistence keys (below); a `null` member inherits again. The kind cannot change. |
| `applyStyle(id, targetIds, kind)` | Paragraph IDs for paragraph and character styles (a character style covers the paragraph's text), table IDs for table styles. An empty ID with `kind` removes that kind of style. |
| `stylePatchFromParagraph(paragraphId)` | The paragraph's look as an `updateStyle` patch ("match the caret"). |
| `setTableStyle(tableId, styleId)` | One `StepTable`. |

The view commands `paragraph.style` and `text.style` (argument `value`: a
style ID, empty to remove) apply styles to the selection as one transaction
and keep the selection; `commandState` reports the style at the caret.
Unknown style references are refused wherever an edit could bring them into
the document (formats, paragraph formats, tables), and a style leaves the
sheet only when no text or table uses it, so saved text never refers to a
missing style.
Copy carries the styles a fragment uses with their parents. Paste reuses a
destination style with the same ID and kind, else one with the same name and
kind, and otherwise creates it (renamed if its name is taken).

## Merged cells

A table keeps one slot per row and column. A merged cell is its top-left
slot (the anchor) with `rowSpan`/`columnSpan` greater than 1; the other
slots it covers keep their IDs but hold no paragraphs. `mapTableGrid`
checks the rules on every commit and load: spans stay inside the table and
never overlap, covered slots are empty and span 1, visible cells have at
least one paragraph, and a merged cell does not cross the boundary between
header and body rows.

- `mergeTableCells(tableId, cellIds)` takes visible cells that tile a
  rectangle exactly (merged cells may join a larger one). The anchor keeps
  its ID; the content moves into it in reading order, keeping every
  paragraph object, and cells holding a single empty paragraph add nothing.
- `splitTableCell(tableId, cellId)` keeps the content in the anchor and gives
  every covered slot back its ID with an empty paragraph shaped like the
  anchor's first one.
- Grid edits keep spans consistent: a row or column inserted inside a merged
  cell joins it; deleting a row or column inside one shrinks it; deleting the
  anchor's own row or column moves the merged cell, with its ID and content,
  to the next slot.
- `tableSnapshot` lists visible cells only, with their spans, and reports
  `canMerge: true`, `styleId` and `styles` (for `TableEditor`'s table-style
  box). `TableEditor` announces spans in each cell's accessible name.
- Tab and Shift+Tab move between visible cells in reading order;
  `caretContext` names a merged cell's extent.

Layout measures every visible cell at its spanned width. Rows joined by
merged cells form a row group, which stays on one page when it fits (with a
repeated header). A taller group breaks between rows; a merged cell that
crosses the break shows the lines that fit in its rows on each page and
continues with the rest (`PageItem.cellFirstLine`, `rowHeights`). A single
row taller than a page is split by lines as before. A row's height grows only
for the cells that end in it, so a merged cell never inflates its first row.
Borders are drawn per cell box, so merged cells have no inner grid lines.
HTML export writes `rowspan`/`colspan`; HTML import keeps them.

## Text frames

A `TextFrame` sits on a layout page at `xPt, yPt, widthPt, heightPt` from the
page's top-left corner and has `columns` (1–24), `gutterPt` and `insetPt`. A
frame names the `next` frame of its thread. The document is one story: it
starts in `FrameSet::storyStart` and follows `next` links, and frames outside
that thread stay empty. Threads never form cycles, a frame follows at most
one frame, a thread never continues on an earlier page, and the story starts
at a thread head.

| Method (QML, millimetres) | Effect |
| --- | --- |
| `frames`, `storyFrameId` | Snapshots `{id, label, pageId, pageIndex, xMm, yMm, widthMm, heightMm, columns, gutterMm, insetMm, nextFrameId, previousFrameId, threadId, storyId, storyIndex, storyStart, connectTargets}`. `connectTargets` lists the frames a thread may continue into. |
| `createTextFrame(page, geometryMm)` | Needs `xMm`, `yMm`, `widthMm`, `heightMm`; `columns`, `gutterMm`, `insetMm` are optional. The first frame starts the story. |
| `setTextFrameGeometry(id, geometryMm)`, `setTextFrameLayout(id, layout)` | Partial updates; undo labels "Resize text frame", "Move text frame", "Text frame columns". |
| `connectTextFrames(source, target)`, `disconnectTextFrame(id)`, `removeTextFrame(id)` | Connecting releases the source's old successor; removing closes the thread over the gap and hands the story start on. |

Each change is one `StepFrames` holding the whole frame set. With frames, a
paged layout has `FrameSet::pageCount()` pages, and its content box is the
whole page. The story fills the columns of its frames in thread order
(`RichTextLayout::regions`); text left after the last frame is **overset**:
`RichTextLayout::overset()` and `oversetCharacters()` (whole cells for a
table), and `RichTextArea.oversetCharacters`. A page break moves the story to
the next page's frames. The continuous editor ignores frames, so overset text
stays editable there.

A paragraph that continues into a region of another width keeps the line
breaks it had and lays out its remaining lines at the new width
(`WidthBreak`: one `QTextLayout` whose line widths change at the break), so
no text repeats or disappears. A table that breaks inside a cell keeps its
column widths in the next region until it breaks between rows.

The paged view draws frame outlines (`frameGuideColor`, theme accent for
story frames) and marks overset text on the story's last frame
(`oversetColor`, theme danger); PDF output draws neither. `caretFrameId`,
`frameAt(x, y)` and `goToFrame(id)` connect the page, the caret and
`TextFrameEditor`; Ctrl+Alt+Page Down/Up (`navigate.nextFrame`,
`navigate.previousFrame`) move the caret to the next or previous frame of the
story. Down from a column's last line continues at the top of the next column
or frame.

## Page setup and columns

`PageSetup.columns` (1–24) and `columnGapPt` divide the content box into text
columns filled left to right; each column is at least 36 pt wide. A page
break ends the page, not the column. `pageSetupMm` reports `columns` and
`gutterMm`, and `applyPageSetupMm` applies size, orientation, margins,
columns, gap and (WP-053) bleed as one undoable `StepDocument`. Section or
page scopes are refused (`Unsupported`) with the document unchanged: page
geometry is document-wide ([printing.md](printing.md#bleed-and-page-scope),
D-133). Units are canonical: points in the model, millimetres
in the panel, whatever the locale.

## Persistence

Version 2 adds these members to `qindatk.richtext` (all optional):

```json
{ "version": 2,
  "page": { "widthPt": 595.276, "heightPt": 841.89, "marginsPt": [36, 36, 36, 36], "columns": 2, "columnGapPt": 18 },
  "styles": [ { "id": "body", "name": "Body", "kind": "paragraph", "parent": "base",
                "paragraph": { "spaceAfterPt": 6 }, "character": { "sizePt": 10.5 } },
              { "id": "grid", "name": "Grid", "kind": "table",
                "table": { "borderPt": 1, "headerBackground": "#e8eef8", "bandBackground": "#f4f6fa" } } ],
  "formats": [ {}, { "style": "emphasis" } ],
  "blocks": [ { "paragraph": { "format": { "style": "body", "overrides": ["align"], "align": "center" } } },
              { "table": { "style": "grid", "rows": [ { "cells": [ { "rowSpan": 2, "columnSpan": 2 }, { "paragraphs": [] } ] } ] } } ],
  "textFrames": { "story": "a", "frames": [ { "id": "a", "page": 0, "xPt": 36, "yPt": 36, "widthPt": 250,
                  "heightPt": 360, "columns": 1, "gutterPt": 12, "insetPt": 0, "next": "b" } ] } }
```

- Styles are read first and sorted by ID when written; a paragraph or table
  reference to an unknown style, a cycle or an invalid member fails with its
  field path (`blocks[0].paragraph.format.style`, `styles`). A format naming
  an unknown character style loses the reference with a warning: a
  session's live encoding keeps unused formats, which may name a style whose
  creation was undone.
- A styled paragraph's format stores its resolved values too, so an older
  reader still shows the paragraph's look; decoding re-resolves the
  non-overridden members from the style.
- Covered slots are cells with `"paragraphs": []`; invalid spans fail at
  `blocks[i].table.rows`.
- Documents that use none of these members are written as version 1, byte
  for byte as before; `RichTextCodec::requiredVersion` decides. Version 1
  and 2 files and fragments are read.

## Verification

`tst_rich_text_styles`, `tst_rich_text_merged_cells` and
`tst_rich_text_frames` cover inheritance and overrides, cycles and invalid
edits, undo/redo byte equality, local repagination, persistence and paste;
merge/split and grid edits across spans, invalid spans, HTML, pagination of
a merged cell across three pages with every position mapped to its page and
back; frames of different widths with exact text coverage and overset counts,
resize with undo, thread validation, reopen equality, page columns, and the
view's frame navigation. `tst_document_publishing.qml` drives the panels
against a document, and `tst_publishing_example.qml` drives the example.
