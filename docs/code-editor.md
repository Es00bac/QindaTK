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
Its stable object names are `codeEditor`, `codeFindField`, `codeFindNext`,
`codeViewport`, `codeLineGutter`, `codeLineNumber`, `codeScroll`,
`codeSourceEdit`, and `codeStatus`.

The [live example](../examples/code-editor-live/Main.qml) publishes sample
keyword/string/comment spans and a diagnostic against a real editable buffer.
The present slice has one cursor and no folding, completion, or LSP adapter;
those remain WP-081 work. The gutter assumes a fixed monospaced line height
and disables wrapping, so fonts with unusually varied metrics may need a
host-specific gutter implementation.
