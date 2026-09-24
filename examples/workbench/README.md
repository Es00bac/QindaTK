<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Reference workbench UI lane

This local host seeds two `DocumentManager` reference sessions with typed
`PropertyDocument` descriptors and stable row IDs. `AssetGrid` and
`EditableGrid` are projections of each session. Grid edits use one
`EditTransaction`; multi-cell paste uses one `UndoService` macro. The inspector
Name field uses `PropertyBinding`, and the same scoped rename command is
available from the toolbar, context menu, Ctrl+Shift+R and the accessible
toolbar button.

The inspector lifecycle panel provides:

- Native Save As, open by path, save-failure recovery, and workspace capture,
  save and restore. Save the documents before capturing a workspace. Restore
  reopens any saved document that is not already in the manager.
- Copy selected asset to the other document. `TransferPayloadCodec` validates
  and encodes the stable-ID object payload; `PropertyCollectionDropTarget`
  remaps the row ID and commits a native `QVariantList` object collection in
  one undo step. The reference codec saves and recovers this bounded
  structured collection. Resource fields and unresolved references are
  rejected until a domain adapter supplies their payload and resolver.
- Close during task demo. A delayed snapshot-bound analysis is invalidated when
  its document closes. The host publishes a completion only while the source
  document remains open and active; otherwise it reports that the stale result
  was ignored.

Configure against a staged QindaTK package and build with configured
`MAKEOPTS` unchanged:

```sh
cmake -S examples/workbench -B build/workbench -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-stage -DCMAKE_BUILD_TYPE=Debug
cmake --build build/workbench
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/workbench/qindatk_workbench --check
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/workbench/qindatk_workbench --density compact --grab /tmp/workbench.png
```

The executable accepts `--density compact|comfortable`, `--check`, `--dump`
and `--grab path`; without these flags it opens an interactive window. The QML
file relies on its local C++ host context, so launch through this executable.

`--check` exercises grid editing and paste, selection retention, command
routing, clipboard payload ID remapping and undo, native Save As and reopen,
workspace save and restore with a closed document reopened, failed-save
recovery, and close-during-job stale-result suppression. The host API is
declared in `workbench_host.h`, with implementation split between
`workbench_host.cpp` and `workbench_lifecycle.cpp`; the lifecycle controls are
in `WorkbenchLifecyclePanel.qml` to keep the main scene within the file-size
limit.

The cross-document adapter stores remapped object rows in a native structured
collection. Explicit nested `$ref` links between copied rows are remapped;
embedded resources and references outside the transfer are rejected. Native
operating-system accessibility-client activation also remains
outside this local `--check`; the semantic command is exercised through its
QML toolbar/context-menu/shortcut and accessible activation paths.
