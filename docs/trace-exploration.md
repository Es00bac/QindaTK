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

`record(id)` includes exact source and converted times. `childrenOf(spanId)`
and `ancestorsOf(id)` follow span IDs across threads; an ancestor omitted by
truncation is unavailable. `queryWindow(fromFraction,toFraction,threadId,
textFilter,limit)` returns overlapping records in time order, capped at
1,000 per call. `queryRangeNs(fromNs,toNs,threadId,textFilter,limit)` uses
exact decimal nanoseconds and inclusive bounds. `exportJson()` contains the
current retained records, clock metadata and drop counts.

`Tk.TraceView` binds `index` and provides thread/name filtering, zoom,
click selection, exact-time inspector and an `exportRequested(json)` signal
for the host to save. Its scrolling timeline renders at most 500 visible
records at a time, one row per record so nested spans on the same thread
remain readable.
Hosts can supply trace snapshots from profilers, renderers or logging
systems without changing the control. The known-answer fixture is
`tests/fixtures/trace/known_trace.json`; it demonstrates a GPU span nested
under a UI span using different clock domains.
