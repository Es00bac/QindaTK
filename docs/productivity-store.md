<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local productivity store

`Tk.LocalProductivityStore` is an offline-first persistence seam for messages,
drafts, calendar events and tasks. It stores the snapshots consumed by the
mail, calendar and task QML controls; it does not send mail, sync an account,
expand recurrence rules or perform time-zone calculations.

## Record contract

Each collection is a list of maps with a non-empty unique `id` within that
collection. Message records also provide `messageId`, plain-text body and trust
metadata. Drafts retain recipients, subject, body and attachment metadata
(stable attachment ID, name, MIME type, byte size and state), not attachment
bytes. Events retain local start/end values, an IANA zone, calendar ID,
recurrence rule and occurrence exceptions verbatim. Tasks retain stable ID,
status and integer order alongside title, assignee, due date and priority.

The versioned JSON envelope is `qindatk.productivity/1`; collections are
`messages`, `drafts`, `events` and `tasks`. Unknown record fields survive a
save/load round trip. Revisions are decimal strings at the QML boundary.

## Rich draft bodies

`RichMailComposer` edits its body with `RichTextEditor` and `FormatBar`. Its
`snapshot()` keeps `body` as plain text for existing consumers and adds
`richDocumentJson` containing the canonical `qindatk.richtext/1` document.
Store that field on the same draft row with its existing stable `id` and
`attachments` metadata. Reassigning the loaded row to `composer.draft` restores
formatting and keeps the composer connected to the same draft. Old records
without `richDocumentJson` remain readable: their plain `body` is imported as
unformatted paragraphs and subsequent saves add the rich document field.
The JSON remains an ordinary draft snapshot field, so the store's existing
atomic save/recovery behavior applies without changing its envelope schema.

## Persistence and recovery

`saveTo(path)` writes a complete snapshot through `QSaveFile` with direct
write fallback disabled. Before replacing an existing valid primary, the store
atomically writes that previous snapshot to `path + ".recovery"`. A malformed,
unsupported or oversized primary falls back to a validated recovery snapshot;
the `status` becomes `recovered` and `lastError` explains the primary failure.
Both files have a 16 MiB read bound. Empty paths, malformed JSON, unknown
schemas, duplicate IDs, oversized files and I/O failures return explicit
errors. A first save has no prior recovery snapshot to preserve.

Each successful load or save also records the primary file's revision and
SHA-256 content identity. `saveTo` takes a per-file `QLockFile` before checking
that baseline and writing. If another store has changed or replaced the file,
save returns `false`, `status` becomes `conflict`, and `lastError` asks the
caller to reload and reapply the edit; it leaves both the primary and recovery
files untouched. A lock that cannot be acquired within five seconds reports
`busy`. After a conflict, `load()`/`loadFrom(path)` replaces the local snapshot
with the latest saved version. The caller then reapplies or merges its pending
edit and retries. This prevents silent lost updates among writers using this
store; it does not merge records automatically or coordinate arbitrary tools
that ignore the lock file.

The default path is in `QStandardPaths::AppLocalDataLocation`. Applications
may set `filePath` or call `loadFrom`/`saveTo` for another local path. Callers
decide when to save; `upsert` and `remove` update memory and revision but do
not hide I/O behind data-change notifications.

`moveTask(taskId, toStatus, targetOrder)` updates status and compacts order in
one in-memory operation. Each move keeps a bounded whole-task-list checkpoint;
`undoTaskMove()` restores the previous local ordering. The caller saves after
the move/undo and acknowledges the matching `TaskBoard` request only after the
save succeeds.

`shiftEventLocal(eventId, minutes)` changes an event's start and end wall-clock
fields together, rejects nonexistent or ambiguous times in the event's IANA
zone, and retains bounded local undo. `undoEventChange()` restores the prior
event collection. The caller saves and acknowledges the matching
`CalendarView` request after persistence succeeds. Provider sync and edits to
recurrence exceptions remain host responsibilities.

`examples/local-productivity/Main.qml` shows message actions, formatted draft
autosave/reopen, calendar proposal rejection without a calendar provider,
task move/undo and reload against the local store. The fixture never sends to
a real recipient.

## Attachment payloads

`Tk.LocalAttachmentStore` stores bytes separately from the attachment metadata
kept in a draft record. `importFile(id, path)` and `importBytes(id, name,
mimeType, bytes)` return an explicit `{ok, error, id, name, mimeType, size}`
result. `read(id)` verifies the metadata schema, stable ID and recorded size
before returning the original `QByteArray` as `bytes` and base64 text. `remove`
requires the payload to validate before deleting it. Empty or over-8-MiB
payloads are rejected.

IDs map to a SHA-256 directory name beneath `rootPath` (defaulting to
`AppLocalDataLocation/attachments`), so ID text cannot escape the storage root.
Each payload and metadata pair is written into a staging directory, then
published together; replacement temporarily renames the old directory and
restores it if publishing the new version fails. The store does not rewrite a
draft record: the host persists its returned stable ID and metadata using
`LocalProductivityStore`. It does not upload, scan, or encrypt files.

`examples/local-attachment-store` imports a small sample file, attaches the
returned metadata to a local draft snapshot, and rereads the payload by ID.

## Calendar range projection

`Tk.LocalCalendarProjectionModel` connects `LocalProductivityStore` and
`RecurrenceService`. Call `project(rangeStartUtc, rangeEndUtc, limit)` with ISO
UTC boundaries. Each returned row retains the source event map and adds a
stable `eventId`, stable `occurrenceId` (the event ID plus the exact local
occurrence wall time), local `startLocal`/`endLocal`, the original IANA `zone`
and UTC start/end values. One-off events are range filtered directly;
recurring events pass their stored `recurrenceRule` and `exceptions` to
`RecurrenceService`. A store `dataChanged` signal reprojects the current
range. Invalid event times/zones, invalid recurrence and an exceeded result
limit set `error` and reject that projection.

`examples/local-calendar-projection` navigates seven-day UTC windows over a
saved local event snapshot, including a skipped weekly occurrence. The
projection displays wall times and zone in `CalendarView`; navigation is
handled by the fixture host.
