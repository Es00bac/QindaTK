<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-081 optional local language service

The `CodeLanguageService` QML type runs one explicitly configured local LSP
server over framed stdio JSON-RPC. It initializes, opens the active
`CodeDocument`, sends full changes with increasing versions, closes/swaps the
document, and shuts down the server with a bounded kill fallback. It drains
stderr separately from protocol output. No process starts when a CodeEditor
is instantiated. Ctrl+Space in `CodeEditor` requests completion from an
attached service; the existing diagnostic gutter and suggestion popup display
its responses.

Initialization also has a bounded timeout, so an unresponsive launched
process cannot remain indefinitely in the starting state.

Diagnostics are accepted only for the current URI, server version and
`CodeDocument` revision. LSP UTF-16 ranges map to native source offsets;
out-of-range results are skipped. A newer edit or completion request cancels
the previous request, and late responses are ignored. The adapter rejects a
server that negotiates a non-UTF-16 position encoding. Diagnostic messages
without a version are ignored rather than guessed into a newer document.

Focused final checks on Qt 6.11.1 and clangd 22.1.8:

- `tst_code_language_service`: 3/3 passed. A real clangd process returned an
  undeclared-name error at the correct source offset after an emoji, then a
  completion for `alphaSymbol`. Editing canceled a pending completion; stop
  closed the process and cleared suggestions.
- Existing `tst_qml -input tests/qml/tst_code_editor.qml`: 5/5 passed.
- `example_code-editor-lsp` smoke passed with its server stopped by default.
- `qtk-preview examples/code-editor-lsp/Preview.qml --dump --wait 2500`
  showed `Server: ready`, gutter markers at lines 4 and 5,
  `codeCompletionPanel 52,82 120x19`, and `alphaSymbol` in its item.
- [Screenshot](WP-081-lsp-clangd.png) was opened and inspected on the final
  code: the ready server, two live diagnostics, and real completion are
  visible. The preview fixture auto-starts clangd solely for this capture;
  the normal example requires an explicit Start click.

This is a focused LSP subset: one local process/document, full text sync,
UTF-16, versioned diagnostics, plain completion text, and a few basic
server requests. It does not provide snippets, additional edits, semantic
tokens, workspace/project operations, remote servers or unversioned
diagnostic attribution. Qt 6.8 and an installed consumer were not checked.
This evidence does not mark WP-081 accepted.

Cherry-picked as `939423e`. The combined merged build and focused CTest 6/6
passed, including the real clangd process; the CodeEditor QML suite passed
5/5 in integration. The clangd screenshot above was opened and inspected.
