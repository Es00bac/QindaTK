<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Settings and workspace state

`SettingsModel` and `WorkspaceSession` are independent of document files.
Applications can replace their byte stores by injecting `StateStore`; the
default `AtomicStateStore` writes with `QSaveFile` and disables direct-write
fallback. A failed decode leaves the currently loaded settings or workspace
state intact. The reference document file remains owned by `DocumentSession`.

## Settings

Register a `SettingDescriptor` before loading: key, `SettingScope` (`App`,
`Document`, `Workspace`), exact `QMetaType`, default value, optional validator
and `restartRequired`. Optional `label`, `category` and `description` provide
presentation metadata; an omitted category uses the key's first dotted
segment, and an omitted label uses the key itself. The read-only `settings`
list enumerates registered rows in scoped-key order with `scopedKey`, `key`,
`scope`, `instanceId`, `defaultValue`, `valueType`, presentation metadata,
`restartRequired`, `currentValue` and `stagedValue`. A preferences editor can
search this schema without inventing setting types or enumerating orphaned
values. App scope has no instance ID. Document and workspace
scopes require a nonempty instance ID without a colon. Exported keys are
`app:<key>`, `document:<instance>:<key>` and
`workspace:<instance>:<key>`. In-memory committed values remain at their
defaults until changed or imported. `beginStage`, `setStagedValue`, `apply`
and `cancel` implement a preferences dialog. `resetKey` and `resetScope`
restore registered defaults. Invalid staged input cannot be applied; a
failed `importValues` changes no known value. Unknown scoped keys are kept
as orphaned values and reported through `unsupportedKeysImported`, so an
uninstalled plugin's preferences survive a save and later reinstall.

`PreferencesDialog` starts a fresh stage when opened. Escape, Cancel, or
closing its popup discards that stage and resets its search/category filters.
Return/Enter validates the focused editor and applies the full stage only on
success. Validation reveals a setting hidden by filters and focuses its editor
so the error can be corrected.

`SettingsPersistence` stores `{version:2, values, extensions}`. Each value
is tagged with an exact scalar type (`bool`, `int`, `uint`, `longlong`,
`ulonglong`, `double`, `string`, `bytes`, `strings`). Integer values are
decimal strings. Unsupported QVariant types fail save; callers must store a
secure-storage reference instead of a secret value. A version-1 file with
bare bool/string/number values migrates on load. Numeric type information
was not available in v1, so applications should register descriptors first
and review numeric migrations before relying on them. The `extensions`
object roundtrips; arbitrary unsupported top-level fields are not part of
the v2 contract.

## Workspace

`WorkspaceSession` stores `{version:2, documents, views, windows,
activeWindowId, activeDocumentId, dockState, dockWorkspace, extensions}`. Document records
contain UUID and URI references only. View records contain a stable UUID,
document UUID, window ID and application-owned `state` object. Window records
contain window ID, screen ID, `geometry` (x/y/width/height), `maximized`,
`activeViewId` and `dockLayoutId`. `dockState` is the serialized DockModel
arrangement. No document content or undo history is in this file. Unknown
fields in workspace records and the top level survive a load/save; unknown
panel IDs remain pending in DockModel until that plugin registers a panel.
Version 1 migrates missing optional window fields to defaults. Invalid
identities, duplicate IDs, broken view references and unsupported versions
are rejected before replacing the in-memory snapshot.

Call `capture(manager, dock, windows, views, activeWindowId)` and `save()`
when the workspace changes. On startup, register panel definitions, call
`load()` and then `restore(manager, dock)`. A deterministic list of screens
can be passed to `load`/`setState` as
`{id, availableRect:{x,y,width,height}}`; otherwise current application
screens are used. Missing screen IDs resolve to the first supplied screen.
Restored windows are moved and reduced as needed to fit its available rect.
The application creates native windows and applies the returned normalized
records; `WindowHost` integration is scheduled separately. A missing URI or
unavailable document ID remains in the snapshot and appears in
`unavailableDocuments`. Restore never writes a document file. Registered
documents recover their stable view IDs, including multiple views of one
document. `reset()` clears the in-memory workspace without touching files.

DockModel owns persisted panel mode, zone, lane, group and floating rectangle.
`DockHost` owns only the transient presentation flag for floating shade.
Shade survives a frame rebuild in the same host, then resets on a workspace
switch or a new host. It is never serialized. Floating rectangles are
host-local; after restore or host resizing, DockModel constrains them so a
header stays reachable in the host viewport.
