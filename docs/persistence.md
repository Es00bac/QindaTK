<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Document sessions, persistence and recovery

QindaTK supplies one document lifecycle for any registered domain model:
the reference `PropertyDocument`, the spatial `CanvasDocument`, or an
application domain that implements the
[model, command and codec contracts](document-models.md). The reference
document file is JSON; the canvas file is the
[canvas native format](canvas.md#native-format). Neither is a format for
arbitrary image, audio, video or office project data.

## Services and ownership

| Type | Responsibility | Thread and ownership |
| --- | --- | --- |
| `DocumentCodec` | Schema identity; capture an immutable `DocumentCapture`; bounded `inspect`/`decode`; `create`/`adopt` a live model | Capture/adopt on the GUI thread; encode, inspect and decode on any thread. `ReferenceDocumentCodec` stays the reference JSON codec behind it. |
| `DocumentStore` | Load bytes or commit an immutable save request and return a revision-tagged receipt | `save` runs on a worker; inject a store for fault tests or another backend. |
| `AtomicDocumentStore` | Local-file adapter with SHA-256 baseline checks and `QSaveFile` replacement | Checks the payload's header through the request's codec. Its `save` method receives only copied values and `SnapshotPtr`. Direct-write fallback is disabled. |
| `DocumentSession` | One model and codec, URI/title, dirty/saving/read-only/external-change state, Save/Save As, autosave coordination | GUI thread; owns its model (`model`, `kind`, `schemaId`; `document` is the `PropertyDocument` or null) and moves its single history into the manager's `UndoService`. |
| `DocumentManager` | Create/open/restore any registered kind, activate, duplicate view identities, close negotiation and Save All | GUI thread; owns sessions, undo, recovery manager and the codec registry it uses. |
| `RecoveryManager` | Validated snapshot frames of any registered schema, compaction, inspect/restore/discard | GUI facade over a thread-safe `RecoveryJournal`; save and autosave workers append through that journal. |

`DocumentManager::createReferenceDocument()` creates a blank reference model.
Applications generally define descriptors and entities on its
`PropertyDocument` before the first save. `createDocument(schemaId)` creates a
blank session of any registered kind. `openDocument(path, schemaId)`
recognizes the file by content, decodes it with full validation and checks
the canonical local path and document UUID, so reopening an active file
selects the existing session; `openReferenceDocument(path)` does the same for
the reference schema only. `duplicateView(id)` returns another stable view UUID for
the same session; it does not duplicate the document or undo history.
`removeView(documentId, viewId)` removes a non-last view identity without
closing a dirty session; the final view closes through `closeDocument` and its
save/discard negotiation.

`DocumentSession::requestSave()` needs an existing URI. Use
`requestSaveAs(path, overwriteExisting)` for a new destination. The default
Save As refuses an existing file; passing `true` is an explicit overwrite.
The normal Save path checks the SHA-256 of the version originally opened or
last committed. A different or missing file yields a conflict and leaves
the session dirty. `checkExternalChange()` and the file watcher report this
state early, but the hash check in the writer is authoritative. File watchers
are advisory because replacement can remove a watch.

## Snapshot and completion rule

On the GUI thread, a save captures an immutable `DocumentCapture` whose
`SnapshotHeader` names the schema, document UUID, revision and reset
generation. A worker encodes it, appends the recovery frame, writes a
`QSaveFile` temporary file and commits it by replacement. The receipt
identifies the saved header and must match the capture. On the GUI thread
the session updates the saved URI/hash/revision after success. It marks undo
clean and discards recovery data only if the live revision, reset generation
and extension generation still match the capture. A later (stale) receipt
marks clean only the history position it captured, so undoing back to exactly
that position is clean and edits made while the worker was writing stay
dirty ([clean marks](document-models.md#saving)).

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
journal frame is version 2: the schema ID, document UUID, revision, reset
generation, payload length and payload SHA-256, protected by a header
SHA-256, then the complete snapshot ([layout](document-models.md#recovery-frames)).
Version-1 reference frames written by older builds stay readable. Inspection
scans the valid prefix and checks both hashes, the document identity, one
schema per journal, monotonic revisions and, through the registered codec,
that the payload's own header matches the frame. A truncated or corrupt tail is ignored
and reported as `truncatedTail`; a newer valid checkpoint or journal revision
is offered. A frame whose schema has no registered codec is still offered with
`codecAvailable: false` but cannot be restored by that build. Compaction
writes a checkpoint atomically before replacing the journal with an empty
file, so interruption at either step leaves a validated revision available.
When no valid frame remains, the damaged files are kept as `.corrupt` copies.
Journal entry retention defaults to 32 and can be set via
`maxJournalEntries`; `maxJournalBytes` controls the size threshold. Each
session's `autosaveDelayMs` controls its quiet-period debounce (100 ms to ten
minutes); the timed autosave encodes and appends on a worker, while
`autosaveNow()` records synchronously.

`inspectId(id)` exposes a recovery choice (schema, revision, reset
generation, `truncatedTail`, `codecAvailable`, `validEntries`) without
mutating the saved document. `restoreRecovered(id, originalPath)` decodes the
latest valid revision with its schema's codec and creates a dirty session. If
the original path still holds the same schema and document UUID at an older
revision, or exactly the recovered bytes, it becomes the session's save
baseline; otherwise Save As is required or a normal Save reports conflict.
`discardId(id)` removes recovery files. Restore never overwrites the original
file, an invalid recovery entry cannot be used as a document revision, and a
failed restore (no codec, a decode or bounds failure) leaves the files for a
capable build. Autosave records after a two-second quiet period and at
the beginning of an explicit save; edits made immediately before a process
crash can fall inside that debounce window.

<a id="domain-recovery"></a>
## Domain stores and recovery

A domain application brings its own `DocumentStore` and serializer and keeps
these rules. `Tk.WorkbookSession` with `WorkbookDocumentStore` is the
reference ([workbook.md](workbook.md#sessions)).

- The store's `save` runs on a worker with only the request's copies. It
  validates the payload against the snapshot header, honours the file
  expectation (missing, matching SHA-256, or any: the current file's hash
  becomes the baseline) and replaces the file atomically through
  `FormatIO::writeAtomic`.
- The session marks the model saved only when its revision and reset
  generation still match the captured snapshot. A failed save, a conflict or
  a later edit leaves it dirty, and a close that asked to save stays open.
- `RecoveryManager::addPayloadValidator(validator)` lets the manager accept
  a domain payload. For each frame and each `record`, reference-document
  payloads decode as before; otherwise each validator is tried. A validator
  returns no error for a valid payload and fills `header.schemaId` and
  `documentId` (plus revision and reset generation if the payload records
  them; the frame supplies the revision otherwise). Frames are otherwise
  unchanged: hash-checked, identity- and revision-checked, and a torn tail
  is ignored and reported as `truncatedTail`.
- A domain application reads recovered bytes with `inspect(id)` and adopts
  them itself; `restore()`/`restoreRecovered` stay reference-document only.
  The recovered revision reopens as unsaved at the original URL, so a normal
  Save still checks the file's baseline.
