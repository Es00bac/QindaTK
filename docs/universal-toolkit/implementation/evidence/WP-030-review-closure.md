<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-030 review-queue closure candidate

Status: integrated on the universal implementation branch; dual-Qt and final
repository gates remain before acceptance.

## Contracts

- Sharing one `ChartAxis` with two `ChartView`s links its domain and
  normalized viewport. The other axis remains independent. Domain edits
  reset the shared viewport; zoom and pan propagate through the dedicated
  `viewportChanged` signal without recursive domain changes.
- `ChartAnnotationModel` preserves unique IDs, finite source coordinates,
  exact decimal UTC millisecond strings, declared units and atomic batch
  replacement. `ChartRenderer` paints point and axis-line annotations in
  source coordinates, exposes coordinate hit/selection, full source CSV and
  an accessible annotation table. Invalid axis or unit combinations report
  `annotationError`.
- `ChartHistogramSelection` reports first/last bin indices, exact source
  edges, inclusivity, unit, channel and normalization. Click/drag selection
  follows painted bins; selecting the last bin includes the final upper edge.
  Invalid replacement clears the previous range.

## Verification

Isolated worktree from integration `224a681`; build directory
`build/wp030-review` with Qt 6.11.1. `portageq envvar MAKEOPTS` reported
`-j24 -l24`; `cmake --build build/wp030-review` completed without an
override. Focused CTest cases `tst_chart_data`, `tst_chart_dataset`,
`example_chart-view`, `example_chart-review`, and `example_gallery` passed
**5/5**. `tst_qml -input tests/qml/tst_chart_view.qml` passed **5/5**.

Known-answer tests prove: a shared X viewport changes both renderers while
unshared Y stays independent; pan propagates and a domain change resets the
range. Annotation replacement rejects duplicate IDs without resetting the
model, selects a source point at the expected pixel, reports unit mismatch,
and maps exact UTC `9007199254741493` inside a 1000 ms domain past 2⁵³.
The histogram range reports edges `[1,4]`, bin indices `1–2`, and inclusive
final edge; bin 0 alone reports `[0,1)`; invalid selection preserves the
previous range and invalid histogram replacement clears it. QML simulates
a pointer click on the middle histogram bin and checks `[1,3)`.

Compact and comfortable `qtk-preview --dump --grab` captures were visually
inspected in `build/wp030-review/evidence/`. They show a data-coordinate
point label and vertical deadline on the line plot, the four actual source
histogram bins, and matched X ticks across both views. Dump geometry:
The two selected bins have distinct accent outlines and a text range
`Selected 1–3 s` in both densities.

| Density | Trend view | Histogram view |
| --- | --- | --- |
| Compact | `ChartView#reviewTrend 0,0 544x536` | `ChartView#reviewHistogram 552,0 544x536` |
| Comfortable | `ChartView#reviewTrend 0,0 541x523` | `ChartView#reviewHistogram 551,0 541x523` |

Both `ChartRenderer#chartPlot` children fill their views. The established
software/RHI and installed-consumer evidence remains in
[WP-030-visual.md](WP-030-visual.md) and [WP-030.md](WP-030.md). This lane
does not alter those backends or the host export-job seam.

## Shared integration checkpoint

The shared Qt 6.11.1 CMake reconfigure and build passed after merging this
lane with WP-027. Focused `tst_chart_data`, `tst_chart_dataset` and
`example_chart-review` passed **3/3**; the linked-axis, annotation and bin
range QML case passed. Root captured and opened the integrated
[compact dark](../../../screenshots/wp030-integration-compact.png) and
[comfortable light](../../../screenshots/wp030-integration-comfortable.png)
screenshots. The dump reports `ChartView#reviewTrend 0,0 544x536` and
`ChartView#reviewHistogram 552,0 544x536` at compact density. Both images
show the point and deadline annotations, four histogram bins, selected bin
outlines and the source-domain label `Selected 1–3 s`.
