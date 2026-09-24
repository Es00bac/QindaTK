<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Reference workbench UI lane

This local host seeds two actual `DocumentManager` reference sessions with
typed `PropertyDocument` descriptors and stable entity IDs. `AssetGrid` and
`EditableGrid` are projections of each session's document. A committed grid
cell is validated against its stable entity/property IDs and applied with one
`EditTransaction`; the grid relinquishes its sparse projection so document
changes remain authoritative. Multi-cell commits use one `UndoService` macro;
rejecting a cell cancels the macro and restores the grid projection. The
inspector's Name field uses
`PropertyBinding` to commit one document edit.
Apply Name is one scoped `CommandRegistry` action presented by the toolbar,
keyboard shortcut, and inspector context menu. Its accessible toolbar action
uses the same `CommandButton` activation path. Each invocation commits through
the active inspector binding as one document transaction; switching sessions
retargets the active command scope and undo history. The Save action writes
the active native reference document to a user-provided absolute path.

Configure against a staged QindaTK package, then build with configured
`MAKEOPTS` unchanged:

```sh
cmake -S examples/workbench -B build/workbench -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-stage -DCMAKE_BUILD_TYPE=Debug
cmake --build build/workbench
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/workbench/qindatk_workbench --dump --grab /tmp/workbench.png
```

The executable accepts `--density compact|comfortable`, `--check`, `--dump` and
`--grab path`; without these flags it opens an interactive window. The QML
file relies on its local C++ host context, so launch through this executable.

`--check` exercises a grid Name edit and a two-cell Name paste. It verifies one
undo entry for the paste, no lingering sparse overrides, document/grid/
inspector/asset updates, exact undo of both cells, and that the inactive
document stays unchanged. It checks sorting, filtering, selection restoration,
per-document row selection, and re-sorting after an edit and undo. It also
exercises toolbar, context-menu and shortcut command edits after a document
switch. It does not automate an operating-system accessibility client.
Clipboard transfer, async close, failed-save recovery,
workspace restore, and saved-content reopen remain outside this example's
current coverage.
`CommandRegistry` handler registration remains C++ only. Native persistence
and history include the seeded document entities. The grid keeps section
structure local to the workbench while values remain backed by the active
document.
The document grid sorts by Name and filters across visible columns. Selection is
remembered by stable row ID per document, including while filtering temporarily
hides the selected row.
