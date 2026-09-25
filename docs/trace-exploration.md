<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Trace exploration

`Tk.TraceIndex` accepts a snapshot of timestamped spans and events. Configure
one to sixteen named clock domains first with `setClockDomains`, each with
decimal-string `nanosPerTick` and signed `offsetNs`. A source tick converts to
canonical nanoseconds as `ticks × nanosPerTick + offsetNs`. Conversion is
checked for 64-bit overflow. Exact tick and nanosecond values stay strings in
the API and JSON export; the view uses normalized floating-point fractions
only to draw the timeline.

Call `ingestAsync(records)` with up to 50,000 maps. Each map needs a stable
`id`, `kind` (`span` or `event`), `name`, `threadId`, `clockDomain`, and decimal
`startTicks`; spans also need `endTicks`. Optional `category` and `parentId`
describe filtering and nesting. `analysisReady(requestId,count)` signals
publication. A later snapshot or `clear()` invalidates an earlier worker
result; `analysisDiscarded(requestId)` reports it. Invalid rows increment
`rejectedCount`. At most 20,000 valid records are retained, keeping the newest
by canonical start time; older records increment `truncatedCount`. The cap
is a snapshot retention policy, not a streaming archive.

`cancel()` stops the running analysis and keeps the published snapshot; a
newer ingest, `setClockDomains` or `clear()` also cancels. The worker polls
the cancel flag every 1,024 rows
([D-165](decisions-domains.md#d-165--trace-cancellation-is-cooperative-and-keeps-the-current-snapshot)).

## Files

`ingestJsonAsync(text)` and `ingestFileAsync(pathOrFileUrl)` read, parse and
analyze on a worker, up to 64 MiB. They accept two shapes, and clock
domains come from the data, not from `setClockDomains`:

- this index's own `exportJson()` output. It round-trips exactly: the same
  records, clock domains and drop counts.
- Chrome Trace Event JSON (`{"traceEvents": [...]}` or a bare array), as
  written by Chrome, Perfetto and Qt Creator. `ts`/`dur` microseconds become
  the `chrome` domain at 1 ns per tick, rounded to the nearest nanosecond.
  `X`, matched `B`/`E` and instant (`i`, `I`, `R`) events are kept, and
  `thread_name` metadata labels threads as `Name (pid:tid)`. Counters,
  async and flow phases and unmatched begins count as rejected. Parents come
  from time containment on the same thread. IDs are `e<n>`, the event's
  1-based position in the file
  ([D-164](decisions-domains.md#d-164--chrome-trace-event-json-is-the-one-foreign-trace-format)).

A file or text that fails (invalid JSON, unreadable, over 64 MiB, over a
record limit) sets `lastError` and keeps the current snapshot; the next
successful import clears the error. Chrome files have two limits, each with
its own message: more than 200,000 raw events is refused as
`Chrome trace has more than 200,000 events` (a record can take a `B` and an
`E` event), and the converted records then meet the 50,000-record input
limit (`Trace input exceeds 50,000 records`).

`record(id)` includes exact source and converted times and `durationNs`. `childrenOf(spanId)`
and `ancestorsOf(id)` follow span IDs across threads; an ancestor omitted by
truncation is unavailable. `queryWindow(fromFraction,toFraction,threadId,
textFilter,limit)` returns overlapping records in time order, capped at
1,000 per call. `queryRangeNs(fromNs,toNs,threadId,textFilter,limit)` uses
exact decimal nanoseconds and inclusive bounds. `exportJson()` contains the
current retained records, clock metadata and drop counts;
`exportJsonAsync()` serializes a copy of the snapshot on a worker and
delivers it with `exportReady(requestId, json)`.

## Tracks and layout

Each thread is a track of lanes, assigned during analysis: a record takes the
first lane free at its start and never one at or above its same-thread
parent's, so nesting reads as a flame chart and marks in a lane never
overlap. A thread has at most 32 lanes; records past that share the last.
`tracks(threadId)` returns `[{threadId, firstRow, lanes}]`.
`layoutWindow(from, to, columns, threadId, textFilter, firstRow, rowCount)`
returns `{rows, matched, drawn, tiles}` for a window given as fractions of
the snapshot. `tiles` are marks `{row, x0, x1, count, id, kind, name,
threadId}` with `x0`/`x1` clipped to the window; records that would share a
pixel column merge into one mark whose `count` says how many and whose `id`
is the longest. `matched` counts every record in the window that passes the
filters, so nothing is hidden behind a cap. `recordAt(from, to, columns, row,
column, …)` returns the record under a pixel, using the same pixel extent the
mark is drawn with: a mark narrower than a pixel fills the column it starts
in, so every drawn pixel selects its record
([D-204](decisions-domains.md#d-204--trace-views-draw-aggregated-lane-marks-never-one-item-per-record)).

`Tk.TraceView` binds `index` and provides thread/name filtering, zoom
(buttons or Ctrl+wheel, down to a 50 ns window), pan (slider or
Shift+wheel), a shared `Tk.TimeRuler` in microseconds from the trace start
with the selected record as its playhead, click selection, an exact-time
inspector (times, duration, depth, ancestor path, child count), a Cancel
button while loading, drop/reject counts and an `exportRequested(json)`
signal for the host to save (sent when the worker export finishes). The
tracks are one Canvas painting `layoutWindow` marks for the visible rows,
with thread labels; there is no item per record. A caption gives how many
records are in view and how many marks are merged; double-clicking a merged
mark zooms until its records separate.
Hosts can supply trace snapshots from profilers, renderers or logging
systems without changing the control. The known-answer fixture is
`tests/fixtures/trace/known_trace.json`; it demonstrates a GPU span nested
under a UI span using different clock domains.
