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

## Persistence and recovery

`saveTo(path)` writes a complete snapshot through `QSaveFile` with direct
write fallback disabled. Before replacing an existing valid primary, the store
atomically writes that previous snapshot to `path + ".recovery"`. A malformed,
unsupported or oversized primary falls back to a validated recovery snapshot;
the `status` becomes `recovered` and `lastError` explains the primary failure.
Both files have a 16 MiB read bound. Empty paths, malformed JSON, unknown
schemas, duplicate IDs, oversized files and I/O failures return explicit
errors. A first save has no prior recovery snapshot to preserve.

The default path is in `QStandardPaths::AppLocalDataLocation`. Applications
may set `filePath` or call `loadFrom`/`saveTo` for another local path. Callers
decide when to save; `upsert` and `remove` update memory and revision but do
not hide I/O behind data-change notifications.

`moveTask(taskId, toStatus, targetOrder)` updates status and compacts order in
one in-memory operation. Each move keeps a bounded whole-task-list checkpoint;
`undoTaskMove()` restores the previous local ordering. The caller saves after
the move/undo and acknowledges the matching `TaskBoard` request only after the
save succeeds.

`examples/local-productivity/Main.qml` shows message actions, draft autosave,
calendar proposal rejection without a calendar provider, task move/undo and
reload against the local store. The fixture never sends to a real recipient.
