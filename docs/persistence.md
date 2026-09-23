<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Reference document sessions, persistence and recovery

QindaTK supplies a reference document lifecycle for applications built on
`PropertyDocument`. Domain applications can implement `DocumentStore` and their
own versioned serializer while keeping the session and recovery rules. A
reference document file is JSON; it is not a format for arbitrary image,
audio, video or office project data.

## Services and ownership

| Type | Responsibility | Thread and ownership |
| --- | --- | --- |
| `ReferenceDocumentCodec` | Capture immutable versioned bytes and decode a complete property document | Capture/decode on the caller's thread; do not touch a live QObject from a worker. |
| `DocumentStore` | Load bytes or commit an immutable save request and return a revision-tagged receipt | `save` runs on a worker; inject a store for fault tests or another backend. |
| `AtomicDocumentStore` | Local-file adapter with SHA-256 baseline checks and `QSaveFile` replacement | Its `save` method receives only copied values and `SnapshotPtr`. Direct-write fallback is disabled. |
| `DocumentSession` | One document, URI/title, dirty/saving/read-only/external-change state, Save/Save As, autosave coordination | GUI thread; owns its `PropertyDocument`, shares the manager's `UndoService`. |
| `DocumentManager` | Open/create/activate, duplicate view identities, close negotiation and Save All | GUI thread; owns sessions, undo and recovery manager. |
| `RecoveryManager` | Append validated snapshot frames, compact into checkpoints, inspect/restore/discard | Currently synchronous on the GUI thread; callers should use a suitable debounce for large documents. |

`DocumentManager::createReferenceDocument()` creates a blank reference model.
Applications generally define descriptors and entities on its
`PropertyDocument` before the first save. `openReferenceDocument(path)` checks
the canonical local path and document UUID, so reopening an active file selects
the existing session. `duplicateView(id)` returns another stable view UUID for
the same session; it does not duplicate the document or undo history.

`DocumentSession::requestSave()` needs an existing URI. Use
`requestSaveAs(path, overwriteExisting)` for a new destination. The default
Save As refuses an existing file; passing `true` is an explicit overwrite.
The normal Save path checks the SHA-256 of the version originally opened or
last committed. A different or missing file yields a conflict and leaves
the session dirty. `checkExternalChange()` and the file watcher report this
state early, but the hash check in the writer is authoritative. File watchers
are advisory because replacement can remove a watch.

## Snapshot and completion rule

On the GUI thread, a save captures a `SnapshotHeader` (document UUID, revision,
reset generation) and complete immutable bytes. The worker writes that
snapshot to a `QSaveFile` temporary file and commits it by replacement. The
result identifies the saved revision. On the GUI thread, the session updates
the saved URI/hash/revision after success, but marks undo clean and discards
recovery data only if the live revision and extension generation still match
the captured state. Edits made while the worker was writing stay dirty.

An in-progress save can be cancelled before its commit check. A cancellation
request after the commit boundary cannot undo a successful replacement. A
failed or cancelled save does not clear dirty state. The OS can still report a
write error or lose data in a hardware/power failure; this reference adapter
does not claim a full storage durability guarantee. An unrelated process can
modify the same path in the narrow interval between the final baseline check
and rename. Applications requiring interprocess locking or cloud conflict
resolution must provide a stronger store adapter.

`DocumentManager::requestSaveAll()` emits `saveAllFinished` with an outcome
per dirty session. A failure does not cancel another document's save.
`closeDocument(id, Cancel)` leaves a dirty session open; `Discard` closes it;
`Save` starts an asynchronous save and closes only after success and if no
newer edit remains. A new unsaved document needs a Save As path before closing
with Save. Undo history is session state and is not serialized into the
reference file.

## Reference format

The current `qindatk.reference-property-document` schema is version 2. The
top level records `format`, `version`, `documentId`, decimal-string `revision`
and `resetGeneration`, `properties`, `entities`, and an `extensions` object.
Properties have typed defaults and writable/animatable metadata; entities
carry typed values and per-target read-only/animated flags. IDs are UUIDs.
Integer values are decimal strings, including 64-bit values above the QML
Number precision limit. Doubles are finite decimal strings, byte arrays are
base64. The reference codec supports bool, signed/unsigned 32/64-bit integer,
double, string and bytes. Unsupported property types fail capture rather than
silently converting. Decoding rejects malformed, future-version, duplicate or
out-of-schema data; version 1 migrates to version 2 in memory. Unknown optional
application data belongs in `extensions`, which roundtrips unchanged.

## Recovery format and choices

The manager's recovery directory defaults to its application data location;
apps may set `recoveryManager.directory`. Recovery files use the document UUID
as their name and never replace the user's document file. Each append-only
journal frame has a magic/version, byte length, revision, SHA-256 and a full
reference snapshot. Inspection scans the valid prefix, validating both hash
and decoded document identity/revision. A truncated or corrupt tail is ignored
and reported as `truncatedTail`; a newer valid checkpoint or journal revision
is offered. Compaction writes a checkpoint atomically before replacing the
journal with an empty file, so interruption at either step leaves a validated
revision available. Journal entry retention defaults to 32 and can be set via
`maxJournalEntries`; `maxJournalBytes` controls the size threshold. Each
session's `autosaveDelayMs` controls its quiet-period debounce (100 ms to ten
minutes).

`inspectId(id)` exposes a recovery choice without mutating the saved document.
`restoreRecovered(id, originalPath)` creates a dirty session from the latest
valid revision. If the original path still has the same document UUID and no
newer revision, it becomes the session's save baseline; otherwise Save As is
required or a normal Save reports conflict. `discardId(id)` removes recovery files. Restore never
overwrites the original file, and an invalid recovery entry cannot be used as
a document revision. Autosave records after a two-second quiet period and at
the beginning of an explicit save; edits made immediately before a process
crash can fall inside that debounce window.
