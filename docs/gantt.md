<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Gantt scheduling view

`GanttView` draws a virtual list of task labels, planned intervals, progress,
baselines, finish-to-start dependency links and an optional allocation overlay.
It does not own the project document. The same `GanttTaskModel` can be supplied
to `DataTable`; `get(index)` exposes the identical source row. Supply one
`SelectionModel` to the Gantt and use `tasks.indexOfId(selection.currentId)`
for the table's `currentIndex`. The shared ID must be a UUID string, because
`SelectionModel` uses entity UUIDs. Grid sort/filter is a host model concern;
both views must receive the same ordered projection.

## Task records

`replaceTasks(rows)` accepts up to 100,000 records. A record has required
`taskId` (unique canonical UUID string, without braces), `startUtc` (signed 64-bit decimal UTC milliseconds
as a string), and `durationMinutes` (nonnegative working minutes). Optional
fields are `label`, `progress` in `[0,1]`, `baselineStartUtc`,
`baselineDurationMinutes`, and `dependencies` (array of predecessor task IDs).
The model rejects duplicate/missing dependencies and cycles before replacing
anything. `get(index)` returns committed data; `displayGet(index)` returns
preview data while a preview is active. `baselineStartUtc` and duration remain
unchanged during rescheduling. A zero-duration task is a milestone.

## Calendar mapping

`GanttCalendar` uses an IANA `timeZoneId`, a weekday bitmask (Monday bit 0),
local `workStart`/`workEnd` and local ISO-date `holidays`. The default is
Monday–Friday, 09:00–17:00, UTC, without holidays. Projection counts actual
elapsed UTC minutes within each day's local working interval. A weekend or
holiday contributes zero width and a forward move lands at the next working
instant. At a spring DST jump, a 01:00–05:00 local window is three elapsed
hours; the fall repeated hour makes a 00:00–03:00 window four elapsed hours.
Tests fix both known answers in America/New_York. If a calendar's
start/end local time itself is ambiguous or nonexistent at a transition, Qt's
`QDateTime` resolution applies; hosts should choose unambiguous boundary
times for contractual schedules.

Use decimal strings at the API boundary. JavaScript `Number` may lose exact
milliseconds beyond 2⁵³. `visibleStartUtc` is a calendar coordinate;
`pixelsPerWorkingMinute` controls horizontal scale. The ruler labels local
instants through `formatLocal`.

## Transaction boundary

`GanttTaskModel` requires a `GanttTransactionAdapter`. A host subclasses its
four virtual methods: `previewMove(id, proposedStart, committedRows, calendar)`
returns `{tasks: entireOrderedRows}` or `{error: message}`; `commitMove(preview,
beforeRows)` atomically accepts or rejects the edit in the host document;
`cancelMove(preview)` discards external draft state; and `undoMove()` returns
the previous entire schedule. Preview must preserve row order and task IDs.
The model validates it, retains committed rows, and exposes preview rows only
to the view. A changed model revision prevents stale commits. Commit replaces
all rows only after the adapter accepts; cancel leaves committed rows intact.
The bundled `GanttSimpleScheduleAdapter` snaps to work time, propagates
finish-to-start successors when they would start too early, and keeps an undo
stack. Moving a predecessor earlier does not pull its successors earlier.
It is a reference planner;
resource leveling, lag, constraints and persistence belong to the host engine.
Keep one reference adapter per schedule document; its undo stack is in memory
and is not reconciled with a different document loaded into the same model.

The user can drag a task bar to preview a new start or press Left/Right for a
one-hour working-time move. Preview remains visible on release. Enter commits,
Escape cancels, and Ctrl+Z requests undo. Applications may place explicit
Commit, Cancel and Undo actions, as in [the example](../examples/gantt/Main.qml).

## Allocation overlay

`allocationProvider` is an optional object with `valueForTask(taskId)` returning
a fraction in `[0,1]`; a negative return value means no data. The overlay is
painted in the task row and never changes scheduling. The provider may combine
resources however the host defines; the toolkit does not infer units or level
resources from a paint value.

Stable part names for dumps and tests are listed in [controls.md](controls.md).
