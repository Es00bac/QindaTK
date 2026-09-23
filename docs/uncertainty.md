<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Uncertainty intervals (VIS-07)

`Tk.UncertaintyModel` stores host-supplied intervals. Its `meaning` is a
typed choice: `Confidence`, `Credible`, or `Domain`. Statistical meanings
require `coverage` strictly between zero and one; `Domain` requires a
nonempty `meaningDetail` such as “instrument tolerance.” The toolkit does
not infer intervals or equate confidence with credibility. `sourceDatasetId`
and each row's `sourceId` preserve provenance. `xUnit` and `yUnit` must
match the axes. Numeric and logarithmic axes are supported; category and
DateTime axes are outside this adapter's current scope.

Use `replaceIntervals([{id, sourceId, x, estimate, low, high}, ...])` with
strictly increasing finite X and a finite estimate. A present interval has
finite ordered endpoints (`low ≤ high`); an omitted or null pair denotes a
missing interval. One missing endpoint, reversed endpoints, nonfinite input,
duplicate IDs, and more than 100,000 rows are rejected atomically. Missing
intervals keep an estimate and source identity. Signed estimates/endpoints
are supported. A missing row splits a continuous band and is marked with a
distinct open point; it is never interpreted as a zero-width interval.

`Tk.UncertaintyView` draws point error bars or a continuous band. It uses
`ChartAxis` mapping and matching units, normalized `setViewport`, `pan`,
and `zoom` methods. `paintPointLimit` is clamped to 32–4096; the renderer
looks up visible X rows and samples them in bounded buckets while retaining
missing breaks. `visiblePaintCount()` exposes the current bounded draw count.
`projected(id)` reports exact data-to-pixel positions, and hit testing and
selection return stable interval IDs. The virtual `DataTable` includes exact
endpoints, missing status, source IDs and declared meaning; `csvData()`
exports full-resolution rows and metadata.

Screen, PNG, and SVG use the same painter. All raster/vector output sizes
are capped at 8192 pixels per side. PNG stores the complete
`QindaTK.uncertainty.v1` JSON payload in its `QindaTK.IntervalData` text
chunk. SVG stores the same payload in the `qindatk-intervals` metadata
element as base64 JSON. The payload includes units, meaning, coverage or
domain detail, dataset ID, interval IDs, source IDs, endpoints and missing
flags. The visible footer names the meaning, unit and missing count.

Example: [uncertainty/Main.qml](../examples/uncertainty/Main.qml).
