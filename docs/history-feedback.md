<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# History, notifications and task feedback

`UndoService` exposes the active document's transient edit history through
`historyEntries`, `historyIndex` and `cleanIndex`. Each entry has a stable
`entryId`, label, post-step `index`, `applied` and `clean` flags. A merged
gesture keeps its entry ID. Undoing and then creating a different edit retires
the abandoned branch; entity removal or model reset clears affected history.
`navigateToHistoryEntry(id)` rejects an ID that was retired, and
`navigateToHistoryIndex(index)` walks the same guarded undo/redo operations as
the command path. Index zero means the baseline before the retained steps.
History is per document and remains in memory; it is not a version archive or
part of the saved reference file. The clean cursor can be `-1` after memory
pruning or structural invalidation. A failed navigation stops at the last
successfully applied step and reports `lastError`; callers should read the
current index after a failure.

`NotificationModel` is a bounded `QAbstractListModel` for application
notifications. It exposes `count`, `unreadCount`, `maxEntries` and roles
`notificationId`, `severity`, `title`, `message`, `source`, `timestamp`,
`read`, `actions`, `repeatCount`. `post(title,message,severity,source,actions)` returns a
stable ID. Severity is one of `info`, `success`, `warning` or `error`, and
actions are `{id,label}` records. Invalid severity, duplicate action IDs or
empty content are rejected without publishing. `markRead(id)`, `dismiss(id)`,
`get(row)` and `clear()` are explicit. The oldest entry is removed when the
limit is reached. Repeated warning/error records with identical severity,
title, message, source and actions retain one ID, increment `repeatCount`,
become unread and move to the newest position. `pendingToastCount` and
`takeNextToast()` expose a bounded, deduplicated 32-notice presentation queue;
taking or expiring a toast leaves its history entry intact. A `Toast` bound to
`notificationModel` consumes this queue and presents its first command action.
`maxEntries` is constrained to 1–1000; title, message,
source and action records have input caps so an entry cannot exhaust the
history budget. This model is in-memory application feedback; a host that
requires restart persistence stores its own records. Action IDs describe
choices; the host routes them through its scoped `CommandRegistry` after
rechecking availability.

`JobModel` is directly enumerable by views using its `jobId`, `label`,
`jobState`, `progress`, `priority`, `documentId`, `viewId`, `error` and `result`
roles. `job(id)` returns a map whose status key is `state`. A task view may
call `cancelJob(id)`. Retry must be an application command that revalidates
the source, document revision and destination before submitting a new job;
the view emits a retry request instead of replaying old worker closures.
Closing or dismissing a task view does not cancel its job. A successful
result can include `committed: true`, which matters if cancellation arrived
after an atomic output commit.
