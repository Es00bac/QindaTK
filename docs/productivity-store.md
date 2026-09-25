<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local productivity store

`Tk.LocalProductivityStore` is an offline-first persistence seam for messages,
drafts, calendar events and tasks. It stores the snapshots consumed by the
mail, calendar and task QML controls; it does not send mail, sync an account,
expand recurrence rules or perform time-zone calculations. Delivery and
account sync are the SMTP, IMAP, POP3, CalDAV and iCalendar providers in
[productivity-providers.md](productivity-providers.md), which read and write
these records.

## Record contract

Each collection is a list of maps with a non-empty unique `id` within that
collection. Message records also provide `messageId`, plain-text body and trust
metadata. Drafts retain recipients, subject, body and attachment metadata
(stable attachment ID, name, MIME type, byte size and state), not attachment
bytes. Events retain local start/end values, an IANA zone, calendar ID,
recurrence rule, occurrence `exceptions` and single-occurrence `overrides`. Tasks retain stable ID,
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

## Calendar edits

Every calendar edit below changes memory only, keeps a bounded
whole-event-list checkpoint for `undoEventChange()`, and returns
`{ok, error, revision}`. The caller saves and then acknowledges the matching
`CalendarView` or `RecurrenceEditor` request (an account record is written
through its provider first; see
[productivity-providers.md](productivity-providers.md)).

| Call | Effect |
| --- | --- |
| `shiftEventLocal(eventId, minutes, scope, occurrenceId)` | Moves by wall-clock minutes (up to 366 days). `scope` is `series` (default), `occurrence` or `following` (ignored for a single event). |
| `setEventRecurrence(eventId, rule)` | Stores a `RecurrenceService` rule (`frequency`, `interval`, `weekdays`, `endMode`, `count`, `until`); `{}` makes the event single. |
| `setOccurrenceExcluded(eventId, occurrenceId, excluded)` | Adds or removes an EXDATE-style entry in `exceptions`. |
| `setOccurrenceOverride(eventId, occurrenceId, fields)` | Sets `title`, `description`, `location`, `startLocal`/`endLocal` of one occurrence; `{}` restores it. |

Scopes keep editing one occurrence separate from changing the series:

- **series** moves `startLocal`/`endLocal`. For a recurring event the rule's
  anchors follow: weekly `weekdays` rotate and `until` moves by the change of
  the start date, and the `exceptions` and override `recurrenceId` keys move
  by the same wall time, so the same instances stay excluded or overridden.
- **occurrence** records an override `{recurrenceId, startLocal, endLocal}` in
  the event's `overrides` list. `recurrenceId` is the instance's original
  local start, so the occurrence keeps its `occurrenceId` wherever it moves; a
  second move adds to the existing override. The series is unchanged.
- **following** splits the series at the occurrence: the original ends the
  day before (UNTIL), or keeps the occurrences already generated (COUNT, with
  exclusions counted as RFC 5545 does); a new event `<eventId>~<yyyyMMdd>`
  starts at the moved occurrence with the rest of the count, the later
  exclusions and overrides, and no remote identity except `remoteAccount`, so
  CalDAV creates it as a new resource. On the first occurrence it equals
  **series**.

Wall-clock arithmetic happens before the zone check, so 09:00 stays 09:00
across a DST change; a result inside a spring-forward gap or a fall-back fold
is refused. All-day events (`startLocal` "yyyy-MM-dd", as iCalendar
`VALUE=DATE` maps them) move by whole days only (`minutes % 1440 == 0`) and
keep whole local days in the projection. An occurrence ID must name an
instance the rule produces (`RecurrenceService.occurrenceAt`); excluding an
overridden instance removes its override. A rule change drops exclusions and
overrides the new rule no longer produces. `upsert` of an event clears the
calendar undo history, as before.

`examples/local-productivity/Main.qml` shows the five views over one store:
conversations and lazy attachments, token recipients and attachment jobs,
scoped moves with undo, a persisted recurrence editor, and task reorder with
conflict resolution. `ProductivityHost.qml` routes every request; with no
account configured, mail refresh and send reach the providers and come back
rejected by them. The example never sends to a real recipient.

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
`RecurrenceService`. An occurrence with an override takes the override's
fields and times, keeps its `occurrenceId`, and carries `overridden: true`
and `recurrenceId`; an instance moved into the range from outside it is
found through its override. All-day rows span whole local days (the zone's
start of day, end exclusive). A store `dataChanged` signal reprojects the
current range. Invalid event times/zones, invalid recurrence and an exceeded result
limit set `error` and reject that projection.

`examples/local-calendar-projection` navigates seven-day UTC windows over a
saved local event snapshot, including a skipped weekly occurrence. The
projection displays wall times and zone in `CalendarView`; navigation is
handled by the fixture host.
