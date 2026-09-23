<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-032 EventLogView sublane

**State:** implementation integrated and focused verification complete. DiffView
and ResourceTable are tracked in the WP-032 package evidence.

## Contract

`Tk.EventLogModel` is a GUI-thread `QAbstractListModel`. Producers submit a
single event or a batch. A batch becomes one insert/reset notification, with a
pre-append and post-append signal for a view to keep its stable scroll anchor.
Caller event IDs are retained; missing or duplicate IDs receive monotonically
generated IDs. Timestamps are normalized to UTC. A missing timestamp uses the
current UTC time; an invalid supplied timestamp rejects the whole event.

Retention is bounded by both 100,000 events and 64 MiB of UTF-8 field payload.
An incoming batch stages at most the configured row count and 16 MiB of field
payload. Each event is limited to 16 KiB; oversized or malformed events are
rejected whole, with the most recent rejection count exposed and signaled.
`appendEvents` returns the number of newest valid events retained from that
batch; valid earlier rows outside the staging/retention window are discarded.
Retention eviction removes only the oldest events. Selection remains by stable
ID through filtering and clears only after its event is evicted.

Severity, source substring, text, and inclusive UTC range filters recompute
over the bounded retained window. They run synchronously; there is no worker
result to become stale. A live producer should call `appendEvents` for bursts.
The view follows the tail by default. Pause stores the top visible event ID and
pixel offset; it restores that anchor after a burst while ingestion and
eviction continue. If retention evicts the anchor, the view moves to the oldest
remaining visible row. CSV is an RFC 4180 data seam; applications own durable
file writing and background export scheduling.

## Focused verification

Qt 6.11 Debug/Ninja isolated build completed with plain `cmake --build` under
the configured `MAKEOPTS=-j24 -l24`. Focused C++ tests passed **7/7**. The
100,000-event burst at 12,000 retained rows took **581 ms** in the final local
run; model insertion emitted one batch notification. The additional staging
fixture exceeded the 16 MiB staging ceiling and retained the newest complete
rows only. Tests also cover count/byte bounds, selection eviction, UTC filter
identity, invalid timestamp and payload rejection, rejection signaling and CSV
escaping/selected-ID export. Focused QML tests passed **5/5**, including stable
selection, keyboard navigation, time-zone draft validation, pause position
restoration after eviction, filtering and the export signal. `all_qmllint`
exited successfully (it prints existing module warnings). `qtk-preview --check`
passed for the example.

Integrated Qt 6.11 focused C++ and QML runs passed. An added startup-tail
assertion first failed: an append during Window completion showed the oldest
rows despite `followTail: true`. The view now positions at the last row after
ListView count, content height and geometry settle; that assertion passes.
Its attached scrollbar now controls the ListView directly. Explicit Flex bases
keep time, severity and source columns aligned.

The inspected [compact](WP-032-event-log-compact.png) and
[comfortable](WP-032-event-log-comfortable.png) captures show the newest event
`demo-999` at the bottom, aligned columns, filters and themed alternating rows.
The matching `--dump` runs measured `EventLogView#eventLogDemo 8,32 1264x743`,
rows host `0,41 1260x698`, and 22px rows in compact; comfortable measured
`10,35 1260x737`, rows host `0,46 1256x687`, and 26px rows. Immediately after
the startup jump, **79** and **66** delegates existed for 1,000 rows,
respectively, including both initial and tail cache items. The assertion
`events.atYEnd` proves the live tail position; instantiated rows remain bounded
well below the source count.

`Tk.VirtualList` uses a generic `ListRow` delegate with one label, secondary
label and icon. `EventLogView` needs a timestamp, severity, source and message
in each row; it therefore uses a native recycling `ListView` while retaining
the same viewport-bounded delegate behavior.
