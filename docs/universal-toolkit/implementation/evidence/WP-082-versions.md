<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-082 named property versions and restore slice

Branch `work/wp082-ipc-transport`, following `ca4769e`.

Implemented:

- `PropertyVersionHistory` stores up to 64 immutable typed reference snapshots
  atomically with actor UUID, UTC time, source document revision and local
  operation count. It lists, inspects and diffs checkpoints and compares one
  with the current committed document.
- `restore(id)` first creates a durable safety checkpoint of the current
  committed state. It then submits one bounded `Restore` operation through
  `LocalPropertySyncClient`. The operation retains its identity across
  offline reconnect, enters the existing durable service log, and publishes
  all winning property values in a single document revision on each replica.
  Existing scalar packets remain unchanged on disk and wire.
- `Tk.VersionBrowser` displays named versions, provenance and current
  differences, with Save and Restore actions. The gallery has a section for
  the control.

Focused proof:

- Build `tst_property_version_history` and `qtk-preview` with configured
  `cmake --build build/wp082-ipc --target ... -- -j24 -l24`.
- CTest `tst_local_property_sync`, `tst_property_sync_socket` and
  `tst_property_version_history`: **3/3 passed**. The version test creates two
  versions, checks typed inspect/diff, restores while the editor is offline,
  reconnects, and confirms the second replica receives one restore operation
  and one document revision for two changed properties. It reopens the
  checkpoint store and finds the automatic safety version.
- `qtk-preview examples/gallery/Main.qml --check` passed. The direct section
  preview dumped `VersionBrowser#versionBrowser 0,0 622x174` at compact and
  `0,0 618x200` at comfortable density. Both PNGs were opened and inspected:
  [compact](WP-082-version-browser.png),
  [comfortable](WP-082-version-browser-comfortable.png).

Limits: the named checkpoint file is local to its owner; the restore operation
is shared. Snapshots are capped at 40 KiB and require the same entity/property
shape on every replica. Structural or rich-text domain versions, shared
checkpoint metadata, branches and history compaction remain WP-082 scope.
