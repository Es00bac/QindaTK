<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# DistributionPlot

`Tk.DistributionModel` accepts up to 64 groups, 10,000 finite source values
per group and 100,000 values total through `replaceGroups(groups)`. Each group
has a stable `id`, display `label`, a declared common `unit`, and a nonempty
`values` list. Invalid input returns `false`, sets `error`, and preserves the
last valid model. Empty input clears the model. Group order can change without
changing IDs; `Tk.DistributionPlot.selectedGroupId` follows the ID.

The model computes min, Q1, median, Q3, max, mean and a 64-point Gaussian
kernel density estimate from source values. Quantiles use Hyndman–Fan Type 7:
linear interpolation at `(n - 1) × p`. KDE uses a Gaussian kernel with
Silverman's `0.9 × min(sample SD, IQR / 1.34) × n^-1/5` bandwidth. For a
constant or otherwise zero-bandwidth group, a declared fallback bandwidth is
`max(range / 10, max(abs(median), 1) × 0.01)`. Its sampled domain extends
three bandwidths past each observed extreme. All computations and drawing are
bounded; non-finite or overflowing domains are rejected.

An optional `interval` map must contain finite `low` and `high` (`low ≤ high`)
and a nonempty `meaning`, such as “95% confidence interval of mean”. An
optional finite `estimate` supplies the center mark; otherwise the computed
mean is used. Groups with intervals in one model must use the same meaning.
The model does **not** infer or calculate confidence intervals. A host
statistics backend supplies their endpoints and provenance. The legend states
the meaning and common unit; interval mode draws the supplied band and
estimate. Box mode draws min–max whiskers and a Q1–Q3 box with median. Violin
mode draws the declared Gaussian KDE and median line.

`get(row)` and model roles expose `groupId`, `label`, `unit`, `sampleCount`,
`minimum`, `q1`, `median`, `q3`, `maximum`, `mean`, `density`, `bandwidth`
(the actual KDE bandwidth in the declared unit), `hasInterval`,
`intervalLow`, `intervalHigh`, `intervalMeaning`, and `estimate`.
`sourceValues(groupId)` returns original values in source order as rows with
`index`, `value`, and `unit`; the view's Values table loads these bounded rows
only while that table is open.
`summaryCsv()` and the view's `csvData()` export the current summary with
interval metadata. A production format/job service owns file output.

`Tk.DistributionPlot` has `distributionModel`, `mode` (`box`, `violin`, or
`interval`), `tooltip`, `selectedGroupId`, `showDataTable`, and
`showSourceTable`. Mouse selection and Left/Right or Up/Down emit
`groupSelected(groupId)`; programmatic selection and model reorder do not.
Summary and source-value tables use virtual `Tk.DataTable` controls as the
accessible numerical alternative. The plot and legend expose accessible
names/descriptions. Stable objectNames: `distributionPlot`,
`distributionToolbar`, `distributionBody`, `distributionCanvas`,
`distributionSummaryTable`, `distributionSourceTable`,
`distributionLegend`, and `distributionError`.

Example: [distribution-plot/Main.qml](../examples/distribution-plot/Main.qml).
