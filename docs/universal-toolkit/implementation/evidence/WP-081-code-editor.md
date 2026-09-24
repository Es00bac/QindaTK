<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-081 CodeEditor slice

The reusable `CodeDocument` holds source text through Qt's `QTextDocument`.
`CodeEditor` attaches the native `TextEdit` document so typing, IME,
selection, undo/redo, syntax formatting and local UTF-8 save/reopen operate on
one buffer. External providers publish syntax spans and diagnostics against a
revision; stale revisions are rejected. Edits clear old results. The view adds
a bounded line-number gutter, diagnostic markers, find navigation and cursor
status. The [live fixture](../../../../examples/code-editor-live/Main.qml)
uses a small sample provider; the toolkit API accepts other parsers.

Focused Qt 6.11.1 evidence in the isolated lane:

- `tst_code_document`: 1/1 CTest suite passed (Unicode, edit history,
  stale-provider rejection, local UTF-8 save/reopen).
- `tst_qml -input tests/qml/tst_code_editor.qml`: 3/3 passed, including the
  attached native buffer and a highlighter revision regression.
- `qtk-preview examples/code-editor-live/Main.qml --dump` shows
  `CodeEditor#liveCodeEditor 0,0 818x605`, `codeLineGutter 37x557`,
  source text, 8 syntax spans and a
  line-9 diagnostic.
- [Screenshot](WP-081-code-editor.png) opened and inspected: source colors,
  line numbers, find bar, diagnostic and cursor status are visible.

## EXT-01 completion lane

The native `CodeDocument` now groups edits at multiple UTF-16 cursor positions
into one undo transaction. The QML view offers Ctrl+Alt+Down and Alt+click
carets, displays secondary carets, and routes typing, Delete, Backspace and
Enter to that transaction. Native IME composition remains on the primary
caret; a text change outside the grouped command clears secondary carets so
they cannot drift to stale offsets. The model rejects offsets inside UTF-16
surrogate pairs.

`publishCompletions(revision,start,end,candidates)` and `applyCompletion`
use the model revision and expected caret position. Editing clears old
suggestions. A suggestion popup accepts Tab/Enter or a click. The provider is
host supplied, as syntax and diagnostics already are.

`foldLines` hides actual QTextDocument blocks without changing the source
buffer. `CodeEditor` maps visual gutter rows back to source line numbers. A
public one-pixel font geometry pulse makes QtQuick TextEdit recompute its
content height after block visibility changes; it uses no private Qt slot.
Editing while folded restores all blocks before continuing. Folding ranges
must be disjoint; nested folding and persisted fold state are not included.

Focused final Qt 6.11.1 evidence:

- `tst_code_document`: 6/6 passed, including multi-caret grouped undo,
  surrogate-boundary rejection, completion revision/caret guards and stale
  syntax/diagnostic behavior.
- `tst_qml -input tests/qml/tst_code_editor.qml`: 5/5 passed. The live
  `TextEdit.contentHeight` contracts and expands on fold/unfold while
  `CodeDocument.text` remains identical; typed multi-caret edits, completion
  apply and Alt+click are exercised.
- `qtk-preview examples/code-editor-live/Main.qml --dump` shows gutter
  source lines `1, 2, 5, 6, 7, 8, 9, 10` after a fold, a secondary caret,
  and `codeCompletionPanel 119,121 120x34` with two items.
- [Final screenshot](WP-081-code-editor-completion.png) was opened and
  inspected. The function body is visibly collapsed, source coloring and
  diagnostic remain, and suggestions and a second caret are shown.

Limitations: the gutter assumes fixed monospaced line height and no wrap;
multiple carets are point carets rather than independent selections;
`contentsChanged` compares the plain buffer to distinguish syntax formatting
from edits, so very large files still pay a full text comparison on each edit.
This lane was not checked against Qt 6.8. LSP process integration remains a
host concern. This evidence does not mark WP-081 accepted.
