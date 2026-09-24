<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Text search and replacement

`FindReplaceService` is a QML-registered adapter for a real
`TextEdit.textDocument` or `TextArea.textDocument`. The host sets its
`textDocument`, `readOnly` and `composing` properties; the service does not
own the editor or document. `revision` is the document's revision. Search
uses a snapshot of its plain text on a worker, so the GUI remains responsive.
Calling `search(query,options,scope)` returns a request ID; results arrive
through `resultsReady(requestId,revision,results,currentMatch,error)` only
for a still-current request and revision. `cancelSearch(requestId)` stops a
pending request, and a document edit invalidates displayed matches.

Options are `caseSensitive`, `wholeWord` and `pattern` (regular expression).
Scope is either empty for the whole document or `{start,length}` in UTF-16
text positions. Each result row has `id`, `start`, `length` and `preview`;
the host can use the range to move the editor cursor or paint a highlight.
`setCurrentMatch(index)` changes navigation without editing. Empty queries
clear matches. Input is bounded to 256 query code units, two million document
code units and 10,000 results. A bad pattern or invalid scope reports an
error instead of replacing text.

`replaceCurrent(expectedRevision,replacement)` and
`replaceAll(expectedRevision,replacement)` reject stale results, read-only
or composing editors. Both return `{ok,replaced,error,revision}` and emit
`operationFinished`. Replace All performs one `QTextCursor` edit block, so a
single document undo restores every match. Matches are applied from the end
of the document, preserving original offsets. Replacement text is literal;
regular-expression capture substitution is not part of this adapter's
contract. A domain editor with structured ranges can provide a separate
search adapter with the same revision and transaction rules.

`RichTextFindReplaceService` implements that interface for
`RichTextDocument`. Set `document` to the same model used by
`RichTextEditor` or `RichDocumentView`, then assign the service to
`FindReplaceBar.findReplaceService`. Search works on the editor's raw UTF-16
text projection of each paragraph, including table cells. Rows add
`paragraphId` and `offset` to the common fields; `start` uses the same
document-wide UTF-16 offset as `RichTextArea.selectionStart` and
`cursorPosition`. Matches containing object anchors or crossing invalid
grapheme boundaries are skipped. A match cannot
cross a paragraph or table-cell boundary. Replace All commits one rich-text
transaction, so a single document undo restores all matches. The host may
use `paragraphId` and `offset` to reveal the current match in either view.
