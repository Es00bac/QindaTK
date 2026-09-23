<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Financial and target displays

`Tk.OhlcModel.replaceCandles(rows, priceUnit, intervalMeaning, timeZoneId,
referencePrice)` accepts up to 20,000 ordered session slots. Every slot has a
nonempty stable `id` and an exact signed 64-bit UTC millisecond decimal string
`utcMs`. A complete slot has finite `open`, `high`, `low`, `close`; high and low
must enclose open and close. An explicit `missing: true` slot has no prices.
The host supplies the calendar, interval label, time zone policy and price
unit (for example `USD/share`). The toolkit never inserts market holidays or
infers trading time. A non-null finite `referencePrice` draws a reference
baseline; otherwise there is no reference line. Invalid replacement is
atomic: `false` and `error` leave the last valid rows and generation intact.

`Tk.CandlestickView` uses **equal host session slots** on the horizontal axis;
elapsed time does not determine spacing. Rising candles are outlined in the
theme's success role, falling candles filled in danger; a missing slot has a
cross. Its price domain includes every valid high/low and the reference price.
`windowStart` and `windowCount` select a painted page of at most 512 slots.
The virtual `Tk.DataTable` exposes all source slots, including exact UTC
strings and missing flags. Pointer and arrow selection emit
`candleSelected(candleId)`; assigning selection or replacing/reordering rows
does not. Page Up/Down and toolbar buttons move the window. `csvData()`
returns the full source rows with unit, interval, time-zone and reference
metadata. The host owns file output and PNG/SVG export; it can use a bounded
`qtk-preview --grab` capture for view raster output.

`Tk.TargetModel.replaceTargets(rows)` accepts up to 256 targets. Each row
declares stable `id`, label, unit, finite `minimum < maximum`, a target within
that range, whether `higher` or `lower` is better, a `thresholdMeaning`, and
1–8 ordered named threshold bands. Each band provides `end` and `meaning`;
the first starts at minimum and the last must end at maximum. Current `value`
is finite and within range, or `missing: true` omits it. Bands are shown with
alternating neutral theme colors; no favorable status is inferred from their
position. Invalid replacement leaves the previous model intact.

`Tk.TargetDisplay` shows bounded pages of eight bullet rows or four semicircle
gauges. A bar/needle shows the current value, an accent marker shows the
target, and a missing value has no bar/needle. The selected row's legend
states unit, direction, threshold meaning and all band meanings. Pointer and
arrow selection emit `targetSelected(targetId)`; programmatic updates remain
silent. A virtual `Tk.DataTable` exposes every row, and `csvData()` supplies
source metadata to the host export service. There is no built-in image/vector
file writer in this lane.

The controls use these stable objectNames: `candlestickView`, `candleToolbar`,
`candleBody`, `candleCanvas`, `candleDataTable`, `candleLegend`, `candleError`,
`targetDisplay`, `targetToolbar`, `targetBody`, `targetCanvas`,
`targetDataTable`, `targetLegend`, `targetError`.

Example: [financial-displays/Main.qml](../examples/financial-displays/Main.qml).
