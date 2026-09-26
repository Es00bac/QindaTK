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

### Host-scheduled rows (D-303)

A host whose engine owns the schedule adds optional presentation fields.
Each is validated with the rest of the row; one bad value rejects the whole
replacement and `lastError` names the field. Each is also a model role.

| Field | Type | Meaning |
| --- | --- | --- |
| `finishUtc` | UTC ms string ≥ `startUtc` | The bar ends here instead of `addWorkingMinutes(startUtc, durationMinutes)` — for engines with several shifts a day, exceptions or resource calendars the `GanttCalendar` cannot express. |
| `baselineFinishUtc` | UTC ms string ≥ the baseline start | The same for the baseline lane. |
| `kind` | `"task"` (default), `"summary"`, `"milestone"` | Summary rows draw a bracket (`ganttSummaryBar`, with `ganttSummaryProgress`); milestones a diamond at `startUtc` (`ganttMilestone`). A zero-duration row without `finishUtc` (or with `finishUtc == startUtc`) is a milestone whatever its kind, unless it is a summary. |
| `critical` | bool | Bar, milestone and progress in `danger`/`dangerSubtle`; a link whose two ends are critical is drawn in `danger`. |
| `inactive` | bool | Outline only (`textMuted`), no progress; the label is muted. |
| `outlineLevel` | integer 0–64 | Indents the built-in label by `space.lg` per level; summary labels are bold. |
| `barText` | string | A caption right of the bar (`ganttBarText`), e.g. resource names. |
| `linkTypes` | array parallel to `dependencies` | `"FS"` (default) end→start, `"SS"` start→start, `"FF"` end→end, `"SF"` start→end. A length mismatch is rejected. Links end in an arrowhead at the successor. |

The fields change drawing only. Scheduling — lag, constraints, typed links,
levelling — is the host engine's (see *Transaction boundary*).

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

`localDate(utc)` returns the calendar-zone ISO date of an instant,
`dayStartUtc(isoDate)` the UTC instant of that local midnight (the first
valid local instant when a DST jump skips midnight; a spring day is 23 hours,
a fall day 25), and `isWorkingDate(isoDate)` is `isWorkingDay` for an ISO
string. Invalid input returns an empty string or false and sets `lastError`.

Use decimal strings at the API boundary. JavaScript `Number` may lose exact
milliseconds beyond 2⁵³. `visibleStartUtc` is a calendar coordinate;
`pixelsPerWorkingMinute` controls horizontal scale. The ruler labels local
instants through `formatLocal`.

## Time scales

`timeScale` is `"working"` (default) or `"calendar"`:

- **working** — x counts working minutes of the calendar from
  `visibleStartUtc` at `pixelsPerWorkingMinute`; a weekend or holiday has no
  width; the ruler is one row of local instants at `tickStepMinutes`.
- **calendar** — x is elapsed time at `pixelsPerDay` (default
  `size.control`), so weekends keep their width and are shaded
  (`ganttNonWorking`, from `isWorkingDate`, drawn once under all rows; not
  drawn below 3 px per day). When `headerHeight` is at least 1.5 rows the
  ruler has two tiers (`ganttRulerMajor` over `ganttRulerMinor`): weeks over
  days from 16 px per day (day labels gain the weekday from 30 px), months
  over week starts from 3 px, years over months below. Labels use
  `Qt.locale()` and the locale's first day of the week; at most 800 civil
  days are materialized per frame.

`visibleEndUtc` (read-only) is the instant at the right edge of the track.
`scrollBy(minutes)` shifts the window (elapsed minutes on the calendar
scale, working minutes on the working scale); a horizontal wheel or
Shift+wheel over the rows scrolls by a day (calendar) or a tick (working).

## Beside a table

Set `labelWidth: 0` to hide the built-in label column, give the view the
table's row height (`rowHeight`, default `size.row`) and header height
(`headerHeight`, default `size.timeRuler`), and bind the two vertical
scrolls: `contentY` is the rows' read/write scroll and `contentHeight` their
height. Clicking anywhere in a row selects it through the shared
`SelectionModel`; a double-click emits `taskActivated`.

## Markers

`markers` is a list of `{utc, label, role}`: a vertical line over the rows
(`ganttMarker_<index>`, `size.focusRing` wide) in the named `Tk.Theme.color`
role (default `accent`) and, when `label` is set, a chip in the header
(`ganttMarkerLabel_<index>`, in the upper tier of a two-tier ruler). Hosts
use it for today and a status date; the view never reads the clock.

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
It is a reference planner: it treats every link as FS whatever `linkTypes`
says and drops `finishUtc` from rows it moves, so they fall back to the
duration;
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
