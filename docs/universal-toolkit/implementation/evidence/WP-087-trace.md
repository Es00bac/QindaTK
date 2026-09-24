<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-087 trace exploration slice

This slice adds a second, independent part of WP-087 after the asset
dependency index. `TraceIndex` converts explicit clock domains to signed
64-bit canonical nanoseconds with checked multiplication and addition. It
ingests bounded snapshots off-thread, discards superseded worker results,
retains the newest 20,000 valid records and reports malformed/truncated
counts. Stable IDs, parent IDs and thread IDs support nested queries across
threads. `queryRangeNs` works in exact decimal nanoseconds; normalized
fractions are used only for display. JSON export preserves decimal strings.

`TraceView` adds a timeline, filters, zoom and pan, click selection, exact
inspector and export request. The fixture mixes a UI clock and a device
clock, with known answers for a GPU child span and nested event.

## Verification

- Configured and built `tst_trace_index`, `tst_qml` and `qtk-preview` with
  direct Ninja limits matching configured `MAKEOPTS` (`-j24 -l24`).
- `tst_trace_index` and `example_trace-explorer` passed 2/2 CTest entries.
- The focused `tst_trace_view.qml` runner passed 3/3 cases, including zoom,
  selection, exact inspector data and export signal.
- Preview geometry at comfortable density: `TraceView#traceView 14,14
  972x392`, `Flickable#traceTimeline 0,92 972x282`,
  `Rectangle#traceRecord_composite 546,184 213x22`,
  `Caption#traceInspector 0,379 972x13`.
- Opened and inspected
  [compact](WP-087-trace-compact.png) and
  [comfortable](WP-087-trace-comfortable.png) captures. Both show ten
  non-overlapping record rows across six thread labels, the selected GPU
  child span and its exact nanosecond inspector. The timeline scrolls for
  longer snapshots.

## Remaining WP-087 scope

This trace slice accepts host-provided snapshots. It does not yet collect
kernel/process profiler data, import Chrome Trace/Perfetto files, align
unsynchronized clock domains statistically, persist a trace archive, or
provide flamegraph/critical-path analysis. Asset dependency ingestion and
view remain a separate already implemented slice.

## Merged integration

Cherry-picked as `5a2a78d`. The merged tree rebuilt `tst_trace_index`,
`tst_qml` and `qtk-preview` with direct build limits matching the configured
`MAKEOPTS` (`-j24 -l24`). Focused CTest passed 2/2 (`tst_trace_index`,
`example_trace-explorer`); the targeted QML runner passed 3/3. WP-087 remains
active while its WP-074 and WP-083 dependencies and host extraction adapters
remain open.
