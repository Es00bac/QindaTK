<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Code editor

`Tk.CodeEditor` is a reusable source editing control. Its `document` is a
`Tk.CodeDocument`; the control attaches its native Qt `TextEdit` document to
that model. Qt handles keyboard editing, IME composition, selection, and undo
in the same `QTextDocument` that the model saves. A host decides which file to
open, which syntax provider to call, and when to publish diagnostics.

```qml
Tk.CodeDocument { id: source }
Tk.CodeEditor { document: source; tooltip: "Source editor" }
```

`CodeDocument` exposes `text`, `revision`, `lineCount`, `modified`, `filePath`,
`errorString`, `syntaxTokens`, `diagnostics`, and `tokenColors`. Its commands
are `replace(start,length,text)`, `undo()`, `redo()`,
`findNext(text,from,caseSensitive)`, `lineColumn(position)`, `load(path)`, and
`save(path)`. File load/save use local UTF-8 text and atomic replacement on
save. Positions and lengths are UTF-16 offsets, matching Qt text cursors and
QML strings. `findNext` returns `{start,end}` or an empty object and wraps once.

Provider results must carry the revision they analyzed:

```qml
source.publishSyntaxTokens(revision, [
    { start: 0, length: 8, kind: "keyword" }
])
source.publishDiagnostics(revision, [
    { start: 12, length: 4, severity: "error", message: "Unknown name" }
])
```

Both calls return `false` for a stale revision or invalid range. Editing clears
old provider results immediately. The highlighter uses `tokenColors[kind]`;
unmapped kinds retain the normal text color. Diagnostics receive a computed
one-based `line` field and appear as a gutter marker. The host can produce
tokens and diagnostics asynchronously; it should publish only on the GUI
thread. No particular language server or parser is built into this control.

`CodeEditor` exposes `editor` (the Qt TextEdit), `cursorPosition`,
`selectionStart`, `selectionEnd`, `findNext(query,caseSensitive)`, and
`goToLine(line)`. It has `showFindBar`, `showLineNumbers`, and `readOnly`.
`addCursorBelow()` adds a secondary cursor at the same column on the next
line; Ctrl+Alt+Down invokes it. Alt+click adds another caret. Typing,
Backspace, Delete and Enter edit all carets in one native undo step. Escape
clears secondary carets. `editCursors(positions,text,operation)` is the model
command if a host supplies its own multi-caret UI; positions are UTF-16
offsets and operations are `insert`, `backspace`, or `delete`. An active IME
composition stays with the primary native caret.

`foldLines(firstLine,lastLine)` hides the lines after `firstLine` through
`lastLine` in the native text layout; `unfoldLines(firstLine)` restores them.
The source text, UTF-16 positions, save output and undo history stay intact.
Select multiple lines and press Ctrl+Shift+[ to fold; Ctrl+Shift+] or clicking
the anchor line number unfolds. The document exposes `folds`,
`visualLineCount`, and `lineForVisualRow(row)` for gutters. A text edit while
folded unfolds the view, so edits and IME never continue in hidden text.

A host may publish revision-tagged suggestions with
`publishCompletions(revision,start,end,[{label,insertText}])`. The source
range is `[start,end)` in UTF-16 offsets, and `end` must be the current caret
to apply. The editor shows a suggestion panel and accepts the selected item
with Tab or Enter. `applyCompletion(index,cursorPosition)` rejects stale
revisions or moved carets; ordinary edits clear suggestions.
Its stable object names are `codeEditor`, `codeFindField`, `codeFindNext`,
`codeViewport`, `codeLineGutter`, `codeLineNumber`, `codeScroll`,
`codeSourceEdit`, `codeCompletionPanel`, `codeCompletionItem`, and
`codeStatus`.

## Optional local language server

`Tk.CodeLanguageService` launches one explicitly configured local executable
without a shell. It is optional; opening a `CodeEditor` never starts a
process. Attach the same document to the editor and service:

```qml
Tk.CodeDocument { id: source }
Tk.CodeLanguageService {
    id: lsp
    document: source
    program: "clangd"
    arguments: ["--background-index=false"]
    languageId: "cpp"
}
Tk.CodeEditor { document: source; languageService: lsp }
Tk.Button { text: "Start clangd"; onClicked: lsp.start() }
```

`start()` sends LSP `initialize`, `initialized`, and `textDocument/didOpen`.
Initialization has a bounded timeout.
Source edits send full `didChange` notifications with increasing document
versions. Closing or swapping a document sends `didClose`. `stop()` cancels
an outstanding completion, closes the document, sends `shutdown` and `exit`,
and kills an unresponsive server after a bounded timeout. The service exposes
`status` (`stopped`, `starting`, `ready`, `stopping`, `error`), `errorString`,
`documentUri`, and `requestCompletion(cursorPosition,prefixStart)`. Ctrl+Space
in `CodeEditor` requests completion when `languageService` is attached and
ready; its prefix scan covers ASCII identifier characters. Hosts may supply
a different prefix range through the service method.

LSP positions use UTF-16, matching `CodeDocument`. Diagnostics must carry an
LSP version matching the last sent change; an unversioned result is ignored
because it cannot be assigned safely after a newer edit. Completion responses
are rejected after a revision or document change, and a newer request cancels
the older one. The adapter accepts plain completion text edits whose range
matches the requested prefix. Snippet items, additional text edits, workspace
operations and other server-specific features remain outside this focused
adapter. Process stderr is drained but is not treated as protocol data.

The [local clangd example](../examples/code-editor-lsp/Main.qml) starts only
when its Start button is clicked. It demonstrates real C++ diagnostics and
completion in the existing gutter and suggestion panel.

The [live example](../examples/code-editor-live/Main.qml) publishes sample
keyword/string/comment spans and a diagnostic against a real editable buffer.
The gutter assumes a fixed monospaced line height
and disables wrapping, so fonts with unusually varied metrics may need a
host-specific gutter implementation.
