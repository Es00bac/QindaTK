<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Reference workbench UI lane

This local host seeds two actual `DocumentManager` reference sessions with
typed `PropertyDocument` descriptors and stable entity IDs. `AssetGrid` and
read-only `EditableGrid` are projections of each session's document. The
inspector's Name field uses `PropertyBinding` to commit one document edit.
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

`--check` exercises toolbar, context-menu and shortcut name edits, verifies a
single revision increment and projected asset update, undoes each edit, then
switches documents and confirms the shortcut edits only the active document.
It does not automate an operating-system accessibility client. Clipboard
transfer, async close, failed-save recovery, workspace restore, and
saved-content reopen remain outside this example's current coverage.
`CommandRegistry` handler registration remains C++ only. Native persistence
and history include the seeded document entities, while the grid is
intentionally a read-only projection.
