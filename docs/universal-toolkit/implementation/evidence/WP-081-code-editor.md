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

This slice does not provide multi-cursor, folding, completion or an LSP
process adapter. Those remain WP-081 work; this commit should not mark the
package accepted.
