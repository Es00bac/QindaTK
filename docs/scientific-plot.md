<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Scientific plotting (WP-085 first slice)

`Tk.ScientificDataset` accepts one-dimensional, ordered float64 samples:
`{id, x, y, sigmaY}`. `id` is stable and unique; X values are finite and
strictly increasing. `y` and `sigmaY` are numeric, finite, and `sigmaY` is
nonnegative. `missingY: true` retains a row with no drawable estimate. A
missing or null `sigmaY` means the measurement has no supplied interval; it
still appears as a point. The toolkit does not calculate uncertainty from
measurements. The supplied endpoints are `y ± sigmaY`, explicitly labeled
as one standard deviation, not a confidence interval.

Set `sourceId`, `xUnit`, `yUnit` and optional parameter map before
`replaceSamples(rows)`. Replacement is atomic and bounded to 100,000 rows.
`shape` reports `[N,2]`, meaning N samples with X and Y coordinates;
`precision` reports `float64`. The model exposes every row in a virtual data
table and full-resolution CSV, including missing Y values. Its `intervals`
projection is an `UncertaintyModel` used by the existing renderer. This keeps
one calibrated axis and export implementation for scientific and declared
interval displays.

`Tk.ScientificPlot` accepts that dataset, an explicit `title`, and axis domains.
Its two `ChartAxis` objects support numeric and logarithmic coordinates. The
units must match the dataset. `dataAt(pixelX,pixelY)` converts a point inside
the plot to exact data coordinates and units. `projected(id)` maps a sample to
pixels. Click or arrow-key selection retains the sample ID; the full source
table provides the text alternative. Painting is bounded by the existing
uncertainty renderer's visible sample limit.

For a logarithmic axis, set strictly positive domain limits. Samples and
uncertainty endpoints outside that positive domain cannot be projected and
are omitted from the figure; they remain in the full source table and export
metadata. Choose a linear axis when zero or signed measurements matter.

`exportPng(path,size)` and `exportSvg(path,size)` use the same painter as the
screen. The PNG text chunk `QindaTK.IntervalData` and SVG metadata element
`qindatk-intervals` contain an embedded `figure` object with the source ID,
units, float64 precision, shape, host parameters, all source samples and a
SHA-256 digest of the canonical sample array. Metadata is deterministic for
identical inputs. It also records figure title, axis kinds, domains, viewport,
mode and output pixel size. A host can save `metadataJson()` and `csvData()` as
sidecars. Use a fixed output size and declared font environment for visually
reproducible exports.

This slice covers one 2D measurement series and symmetric Y uncertainty.
Multiple series, asymmetric/X uncertainty, 3D scatter/surfaces, array chunk
providers and figure layout composition remain WP-085 work.

Example: [scientific-plot/Main.qml](../examples/scientific-plot/Main.qml).
