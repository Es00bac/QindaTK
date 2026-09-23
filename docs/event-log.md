<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Event log

`Tk.EventLogModel` is a GUI-thread `QAbstractListModel` for application event
streams. It is registered in the `QindaTK` QML module. Rows expose `eventId`,
`timestamp`, `severity`, `source`, `message`, and `details`. Caller IDs survive
filter changes; absent or duplicate IDs receive monotonic `event-N` IDs.
Timestamps are UTC `QDateTime` values internally and ISO 8601 strings in model
roles and CSV. A missing time uses current UTC. A malformed supplied time is
rejected.

## Bounds and ingestion

The default retention is 5,000 events and the configurable ceiling is 100,000.
The model also limits retained UTF-8 field payload to 64 MiB, an incoming
batch's temporary staged payload to 16 MiB, and an individual event to 16 KiB.
IDs are limited to 128 bytes, severity to 32, source to 1,024, and message or
details to 8,192. An event that exceeds any limit, has an unsupported
non-string field, or has an invalid timestamp is rejected whole. No content is
silently truncated. `appendEvent(map)` and `appendEvents(list)` return the
number of newest valid rows retained from the call; `lastRejectedCount` and
`eventsRejected(count)` report malformed rows. Older valid rows that do not
fit the 16 MiB staging window or retained row/payload limits are discarded by
the bounded append policy. Use `appendEvents` for high-rate producers.
Insertion or eviction is applied as one model change per call. Filtering is
synchronous over the bounded retained window.

The buffer evicts oldest events to satisfy both count and byte limits.
`selectedEventId` survives filtering. It clears only when the event leaves
retention or the owner calls `clearSelection()`/`clear()`.

## Filters and CSV

The model filters severity (case-insensitive exact match; empty or `all` means
any), source (case-insensitive substring), text (case-insensitive search of
message and details), and inclusive `fromUtc`/`toUtc`. Date range values are
normalized to UTC. Empty `exportCsv()` exports the filtered rows in stream
order; a nonempty ID list exports those retained IDs in stream order regardless
of the active filters. Fields follow RFC 4180 quoting. The returned string is
a data seam: the host app handles file naming, writing, and any background job.

## View behavior

`Tk.EventLogView` renders time, severity, source and message in a recycled
`ListView`; only viewport and cache delegates are instantiated. Click selects
by stable ID. Up/Down changes selection, Enter requests details, and a double
click emits `detailRequested(eventId, details)`. The CSV button emits
`exportRequested(csv)` for host integration. Accessible list and row names
include timestamps and event content.

`followTail: true` follows each accepted burst. Setting it false pauses the
scroll position only: events continue to arrive and retention still evicts.
The view records the top visible event ID and its pixel offset before a batch
and restores it afterward. If that row is evicted, it moves to the oldest
remaining visible event. Filters remain active while paused.

From/To controls accept ISO 8601 values with `Z` or a numeric UTC offset. A
missing timezone or invalid date marks the field erroneous and leaves the
previous model bound in place. Clearing the field removes that bound.
