<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Notebooks and structured blocks

`NotebookDocument` (C++, `QML_ELEMENT`) is the persistent notebook model
behind `Tk.NotebookView` and `Tk.BlockEditor`. It holds sections, pages and
nested blocks with stable identity, records every change in the shared
`UndoService`, and joins `DocumentSession` save, autosave and recovery as the
`qindatk.notebook.v1` domain ([document models](document-models.md)).
Package: WP-057. Decisions:
[D-170](decisions-domains.md#d-170--notebook-blocks-are-a-flat-depth-first-list-with-parent-ids),
[D-171](decisions-domains.md#d-171--notebook-history-steps-share-unchanged-pages),
[D-172](decisions-domains.md#d-172--cross-block-selections-and-pastes-follow-rich-text-fragment-rules),
[D-173](decisions-domains.md#d-173--block-views-are-virtualized-and-keep-drafts-by-block-id).

<a id="model"></a>
## Model

| Level | Identity | Fields |
| --- | --- | --- |
| Notebook | session document UUID | `title`, ordered sections and pages |
| Section | UUID | `title`; `collapsed` is saved view state, never an undo step |
| Page | UUID | `sectionId`, `title`, `tags` (≤ 64, unique, trimmed), ordered blocks |
| Block | UUID, unique in the notebook | `type`, `parentId`, `data`, `locked` |

Pages keep one global order; a section lists its pages in that order. Blocks
of a page are stored **flat in reading order** (depth-first). `parentId`
names an earlier block of the same page, and every block between a parent
and its child is also a descendant of that parent. `decodeNative` rejects any
other order, and every structural edit re-checks it (`NotebookTree::repair`).
IDs never change on edit, move, save, reopen or recovery; a paste gives the
pasted blocks new IDs.

Limits: 1,000 sections, 10,000 pages, 10,000 blocks per page, 250,000 blocks,
1 Mi UTF-16 units of text per block, 8 MiB of data per block, 64 MiB per file.

<a id="blocks"></a>
## Blocks

| Type | `data` | Editing |
| --- | --- | --- |
| `text`, `quote` | `text`, `runs` | text, inline formats, split, merge |
| `heading` | `text`, `runs`, `level` 1–6 | as text; the split tail is a `text` block |
| `list`, `todo` | `text`, `runs`, `ordered` / `checked` | as text |
| `code` | `text`, `language`, evaluation `status`/`output`/`result`/`error`/`evaluatedHash` | text, split, merge, evaluation |
| `table` | `columns`, `rows` (strings), `rowIds` (stable) | cells, rows, columns |
| `chart` | `sourceBlockId`, `kind` (`line`/`bar`/`scatter`), `xColumn`, `yColumn`, `title` | data from a table block |
| `image` | `resourceId` (logical), `caption` | caption; media is relinked through the resolver |
| `embed` | `targetId` (a `notebook:` link or a host ID), `title` | open |
| `ink` | `strokes` | preserved |
| `divider` | — | — |

Any other type loads and saves verbatim and shows as a preserved,
unsupported block (`supported: false`).

**Runs.** Text-like blocks except `code` carry inline formatting as
`runs: [{length, bold?, italic?, underline?, strike?, code?, href?}]`. Run
lengths sum to the text length; `runs` is omitted for plain text. Internally
the runs use the rich-text span algebra (`RichText::Spans`), so split, merge,
typing (`setBlockText` keeps the formatting of the unchanged prefix and
suffix) and `formatText` keep formats exact.

Operations (each one undo step): `insertBlock(pageId, afterBlockId, type, data)`
(after the given block's subtree, at its level), `setBlockData`,
`setBlockText`, `replaceText`, `formatText`, `setBlockType` (between text-like
types), `setBlockLocked`, `moveBlock(blockId, pageId, parentId, index)` (moves
the subtree, also across pages), `moveBlockBy`, `indentBlock` (under the
previous sibling), `outdentBlock` (after the parent's subtree), `splitBlock`
(offset snapped to a grapheme boundary; the children follow the tail),
`mergeBlocks` (adjacent text-like blocks; the second's children move to the
first), `removeBlock` (the subtree), and table `setTableCell`,
`insertTableRow`, `removeTableRow`, `insertTableColumn`, `removeTableColumn`.
Read-only blocks refuse every edit but unlocking.

<a id="selection"></a>
## Cursor and selection

Positions are `(blockId, offset)` in UTF-16 units, snapped to extended
grapheme boundaries (`RichText::GraphemeBoundaries`). Non-text blocks have
the single position 0. `moveCaret(blockId, offset, direction)` implements
`left`/`right` (by grapheme, crossing into the neighbouring block's end or
start), `up`/`down` (the previous or next block at the same offset, clamped),
`blockStart`/`blockEnd` and `start`/`end`, and reports `moved`.

A selection is `{anchorBlockId, anchorOffset, focusBlockId, focusOffset}`
inside one page, ordered by flat index then offset. Blocks strictly between
the endpoints are whole (with their descendants, which lie between them); a
text endpoint contributes the selected part; any other endpoint is whole.

- `selectionText` joins the parts' plain text (tables tab-separated).
- `selectionRange` is the clipboard payload `{format: "qindatk.notebook.blocks",
  version: 1, openStart, openEnd, blocks: [{id, type, parent, data}]}`;
  `parent` indexes an earlier block of the range or is −1.
- `selectionHtml` is the rich-text HTML projection (`RichTextHtml::fromFragment`).
- `deleteSelection` removes the middle, trims the endpoints and joins two
  text endpoints; surviving children of removed blocks move to the nearest
  surviving ancestor. It returns the caret.

<a id="clipboard"></a>
## Clipboard and paste

`copySelection(clipboard, selection)` places one range item through
`ClipboardService.copyRange` with the plain-text projection;
`cutSelection` copies and deletes (two steps: nothing, then the deletion).
`paste(clipboard, pageId, blockId, offset)` reads `rangePasteData()`: a
notebook range is inserted by `insertRange`, any other range or text by
`insertText` (one text block per line). `insertHtml` imports HTML through
`RichTextHtml::toFragment`: paragraphs become text blocks with runs, headings
heading blocks, list items list blocks, tables table blocks and images image
blocks.

Every paste is **one undo step**. When the target is a text block, it is
split at the caret; an open first fragment joins the head and an open last
fragment joins the tail. References inside the pasted range (parents, a
chart's `sourceBlockId`) are rewritten to the new IDs.

<a id="services"></a>
## Search, links, assets, evaluation and charts

- `search(query, {caseSensitive, wholeWords, limit})` returns hits
  `{pageId, blockId, field, offset, length, excerpt}` over page titles, tags
  and every block's plain text; `searchPageIds` returns the pages in hit order.
  `NotebookView` uses it for its authoritative search result.
- Links are `notebook:<pageId>` or `notebook:<pageId>#<blockId>`
  (`linkTo`). They appear as a run's `href` or an embed's `targetId`.
  `resolveLink`, `backlinks(pageId)` and `brokenLinks()` (reason
  `missing-page` or `missing-block`) read them; deleting a page leaves links
  broken until undo.
- `resourceIds()` lists the logical media IDs; `assetStatus(resolver)`
  reports each image block's `availability` from `ResourceResolver`
  (`ready`, `missing`, `modified`, or `unregistered`) without touching files.
  A missing file never changes the block; relinking changes the resolver's
  mapping, not the notebook.
- `evaluateBlock(blockId, runtime)` runs a JavaScript code block in
  `ScriptRuntime` (WP-081). The script sees a JSON copy of the page as
  `app.document` (`blocks: [{id, type, text, columns?, rows?}]`); the
  output, result, error and status are recorded as one undo step and
  `blockEvaluated(blockId, success)` is emitted. Other languages are refused.
- `chartSeries(blockId)` projects a chart block's source table into
  `{valid, error, kind, name, xName, categorical, points, xMin, xMax, yMin,
  yMax}`. Point IDs are the table's `rowIds`; a non-numeric value is a gap.
  `BlockEditor` feeds it to `Tk.ChartView` (WP-030).

<a id="history"></a>
## History, persistence and recovery

A step stores the whole state before and after; Qt's implicit sharing keeps
unchanged pages shared, so a step retains only the pages it touched
(`retainedBytes`). Steps are checked by stamp, not content, so undo on a
large notebook is constant-time. Typing into one block merges under the key
`text:<blockId>`; a standalone document keeps its own history (200 steps)
until a session adopts it.

<a id="format"></a>
The native file (`.qtknb`, format id `QindaTK.Notebook`, version 1) is one
JSON object: `format`, `version`, `documentId`, `revision`,
`resetGeneration`, `title`, `sections [{id, title, collapsed?}]`,
`pages [{id, sectionId, title, tags, blocks [{id, type, parentId?, locked?, data}]}]`
and `extensions`. `DocumentManager.createDocument("qindatk.notebook.v1")`,
`openDocument`, save, autosave, `restoreRecovered` and close work unchanged.
`toJson`/`loadJson`/`saveToFile`/`loadFromFile` serve standalone use;
`loadJson` replaces the content as one undoable step, and `resetFromJson`
replaces it outside history (a template: no undo steps, a new reset
generation, same document ID, unsaved in a session).

`examples/notebook-authoring` is the live reference: a session-owned notebook
from a field template with both views bound, clipboard, script runtime and
resolver attached, and undo, redo, save, dirty state and backlinks in its
app bar.

<a id="views"></a>
## Views

`Tk.NotebookView` and `Tk.BlockEditor` work in two modes.

- **Notebook mode** (`notebook: doc`): the view reads `sections`/`pages` or
  `blocks(pageId)` from the model and applies every edit to it. The view owns
  `currentPageId` and back/forward history, and `submitSearch()` fills
  `searchResultPageIds` from `searchPageIds`. `BlockEditor` also takes
  `clipboard`, `scriptRuntime` and `resolver`.
- **Host mode** (no `notebook`): as before, the host owns IDs, order and data
  and the views only emit identified request signals.

<a id="keyboard"></a>
`BlockEditor` keys inside a text block: Ctrl+Enter splits at the caret;
Backspace at the start merges into the previous block; Up/Down on the first
or last line and Left/Right at the ends move into the neighbouring block;
Shift+Up/Down there extend a cross-block selection; Tab/Shift+Tab indent and
outdent; Alt+Up/Down move the block; Ctrl+C/X/V copy, cut and paste through
the model; Ctrl+B/I/U format the selected text. In host mode the moves are
`blockNavigateRequested(id, "previous"|"next")` and nesting is
`blockNestRequested(id, "indent"|"outdent", requestId)`.

The block list is a `ListView` with `reuseItems` whose model is the ordered
block IDs; it is replaced only when the order changes, so a text commit
never rebuilds delegates. Uncommitted text is kept in `drafts` by block ID
and restored when a delegate is created or reused; a structural edit first
commits the draft of the block it touches. Formatted runs show as a styled
preview while the block is not being edited.

## Scope notes

Performance and memory measurement for large notebooks (WP-091), other
platforms and Qt versions (WP-088), screen-reader runs and translation
(WP-092) and demo applications (WP-090) belong to the deferred qualification
packages. Ink strokes are preserved but not drawn; drawing them belongs to
the ink and canvas packages.
