<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Chart data foundation

WP-030 introduces a typed C++ foundation for general charts. The public
headers are `chart_axis.h`, `chart_series.h`, `chart_data.h`, and
`chart_histogram.h`. `ChartAxis` and `ChartSeries` are registered as QML
types, while the visual stage adds the bounded QML dataset adapter and
`ChartView`/legend/renderer over these contracts.
The existing sample-order telemetry `Graph` and `GraphSeries` are unchanged.

## Axes and units

`ChartAxis` has Numeric, Category, DateTime and Logarithmic kinds. Numeric
and logarithmic domains are finite, increasing doubles. Logarithmic domains
must be positive. Category axes have ordered unique labels; coordinates are
category indices and map to band centers. Invalid domains return an error and
NaN mapping, so rendering cannot silently imply valid spacing. `reversed`
flips normalized coordinates. Numeric/log ticks use a 1/2/5 ladder and log
ticks use powers of ten. Category ticks show every label. Time labels use the
declared IANA `timeZoneId`; an invalid zone invalidates the axis.

DateTime coordinates are signed 64 bit **UTC milliseconds**. C++ callers set
`setUtcDomain()` and use `mapUtc()` / `unmapUtc()`. QML domain properties are
decimal strings (`minimumUtc` and `maximumUtc`), preserving values beyond
JavaScript's exact integer range. `ChartTick::utcMs` is authoritative for
time ticks. The double `map()`/`unmap()` API rejects DateTime axes. The
renderer must use `ChartPoint::utcMs`, never convert it through a QML Number.

Series declare `xUnit` and `yUnit`, which must exactly match their axes before
projection. An empty unit means dimensionless. Bubble `size` is data-unit area
with its separate `sizeUnit`; renderers choose the pixel area scale explicitly.
Vector `dx`/`dy` use the same units as X/Y and require a numeric X axis.

## Series and geometry

`ChartSeries` has a unique nonempty series ID within a plot. Each `ChartPoint`
has a unique nonempty ID within its series. `replacePoints()` rejects an
invalid batch atomically. `appendPoint()` evicts the oldest point at the
configured capacity (default 100,000); removal and replacement retain IDs of
surviving points. Gaps are explicit points with IDs and stop a line/area path.
Ordinary nonfinite coordinates are rejected, rather than drawn as zero.

`ChartData::project()` returns normalized plot coordinates and baseline
coordinates for each point in input order. It covers Line, Step, Area,
StackedArea, Bar, StackedBar, Waterfall, Scatter, Bubble and Vector kinds.
The renderer joins Line/Area points directly, draws Step with horizontal then
vertical segments between adjacent non-gap points, fills Area to `baselineY`,
and uses the projected `barWidth` for categorical bars. Scatter and bubble
points use their center; vectors use `endX`/`endY`. Bubble `size` stays in data
units. Coordinate values outside the domain remain outside [0,1] for clipping;
invalid log values become marked gaps.

Stacked area/bar series require a `stackKey` and share one declared baseline
per key and kind. For each x/category, positive and negative contributions
accumulate separately; a gap contributes nothing. Only visible series
participate. Bar series are grouped in series order within an 80% category
band; stacked bars and waterfalls occupy one 80% band. Waterfall point `y` is
a contribution from the preceding running total. A point with `total=true`
instead gives an absolute total from the declared baseline and resets the
running value. Missing waterfall points leave the running value unchanged.

`ChartHitIndex` indexes projected finite points into a 64×64 normalized grid,
resolves `(seriesId, pointId)` identity and returns the nearest point within a
declared radius. Rebuild it when source data or chart projection changes.
`ChartData::decimate()` preserves each contiguous run's endpoints and global
minimum/maximum, all explicit gaps and their IDs. If the point budget cannot
represent those obligations, it returns an error instead of hiding data.
Full-resolution source data remains available for accessible table and CSV
export through `ChartData::table()` and `ChartData::csv()`. CSV uses stable
IDs, explicit units and RFC-style quoted fields with CRLF rows.

## Histogram

`ChartHistogram` accepts backend-supplied finite, strictly increasing edges
and either samples or counts. Bins are `[lower, upper)` except the last bin,
which includes the final upper edge. Underflow, overflow and nonfinite input
are counted separately. Count, probability and density modes are explicit;
probability divides by in-range count, and density additionally divides by
each bin width. Empty data displays zero in every mode. Channel and unit
labels come from the caller; image analysis must provide color-context meaning
for channels. This core does not infer bins or image color spaces.

## QML visual adapter

`Tk.ChartDataset` is the bounded QML/model ingestion adapter. Use `seriesId`
(the QML-safe form of the series ID) and a unique `pointId` on each point.
`appendPoint(map)`, `replacePoints(list)`, `removePoint(id)` and `get(row)`
update a `QAbstractListModel`. Capacity is clamped to 2–100,000 points per
series; appending at capacity removes the oldest retained point. A date point
must pass `utcMs` as a signed 64-bit decimal string; numeric `x` is for numeric
axes only. Rejected input leaves the existing rows unchanged and sets
`lastError`. When no dataset color is set, `ChartView` resolves it from the
current Theme roles.

`Tk.ChartView` joins its default `ChartDataset` list to the typed axes and
draws `ChartData::project()` output through `ChartRenderer`, a
`QQuickPaintedItem`. It retains full source data for hit testing, `selectedPoint`,
the virtual `tableModel`, and `csvData()`. Rendering is decimated separately
with a configurable 32–10,000 point paint limit; the framework caps one plot
at 64 datasets and 200,000 projected points. `setViewport()` works in
normalized axis fractions, `zoom()` keeps an anchor fixed, the wheel zooms,
and middle-drag pans. Line, step, area, stacked-area, grouped/stacked bars,
waterfall, scatter, bubble and vector marks use core-projected coordinates.
`setHistogram()` accepts explicit edges and counts, uses `ChartHistogram`
normalization, and adds bins to the accessible table. It requires a numeric or
logarithmic X axis with a matching unit and a numeric Y axis whose range
includes zero; logarithmic X bins must have positive lower edges.

`renderImage(size)` returns a bounded `QImage` through the same paint routine
used by the widget and emits `exportImageReady`. `exportPng(path, size)` and
`exportSvg(path, size)` write bounded 1–8192px output through the same painter
to an atomic file. The QML `exportRequested` signal remains a seam for host
format and job services; these renderer methods provide file output without
integrating an application-wide export job/format pipeline. `showDataTable`
switches to a virtual `Tk.DataTable` over full-resolution point and histogram
rows, including bubble size and vector dx/dy columns. The sample-order
telemetry `Tk.Graph` remains a separate compatible control.

GPU and software visual checks plus the installed consumer check are
acceptance gates for WP-030; source availability alone does not mark this
package accepted.

## Heatmap matrix adapter (DAT-14)

`Tk.HeatmapModel` declares rows/columns, numeric X/Y domains and units, or
ordered X/Y category labels. Row zero is the top of the matrix. Numeric
coordinates are band centers; Y values descend from the declared maximum.
Domains, color bounds and category lists must be valid and complete; `valid`
and `error` expose malformed declarations and rendering stays empty until fixed.
Values are finite numbers or members of `valueCategories`. The caller declares
`colorMinimum`/`colorMaximum` in `valueUnit`; colors clamp at those bounds.
Unset cells and explicit null cells are missing, never zero. `setCell` requires
a stable unique ID and rejects invalid input without changing the matrix.

The sparse model stores only supplied cells. `tile()` aggregates a bounded
viewport into at most 4096 bins and reports minimum, maximum, present count,
missing count and category state. At overview zoom `Tk.HeatmapRenderer` paints
both extrema in split tiles and hatches tiles containing missing cells. It
creates no per-cell QML delegates. `requestTile()` emits only the latest
request and revision; `cancelTile()` invalidates pending delivery. Onscreen,
PNG and SVG rendering use the same data-coordinate paint path. Image dimensions
are limited to 1–8192 pixels per side. `Tk.Heatmap` exposes cell navigation and
announces the selected ID, value or missing state, coordinates and units as an
accessible cell alternative. Pointer drag and Shift+arrow select a rectangular
region; `selectedRegion` reports half-open row and column bounds.

## Part-to-whole and hierarchy displays (WP-031)

`Tk.PartWholeModel` accepts an ordered forest of `{id, parentId, label, value}`
records. IDs are unique and stable; replacing the set is atomic. Values must be
finite and nonnegative. A numeric zero is a known zero share. An absent or null
leaf value is **missing** and has unknown magnitude. Negative or nonfinite
values are rejected with `lastError`; the model never takes an absolute value
or silently treats missing as zero.

An internal node may omit `value`, in which case its known total is derived
from children. A declared parent value must equal the complete sum of its
children within a relative tolerance of 1e-9. A parent with missing descendants
cannot declare a total; its derived `knownTotal` is the sum of known children
and `complete` is false. A view therefore labels the denominator as *known*
and reports `missingCount` and `zeroCount`. Sibling order is input order. The adapter rejects
cycles, absent parents, depth beyond 8 and more than 4096 nodes.

`Tk.PartWholeView` selects the children of a sole root as the top-level
comparison; otherwise it compares the forest roots. Pie and donut angles use
`360 × part / known total`; donut has a half-radius hole. Treemap uses
alternating horizontal/vertical partitions whose areas are proportional to
known totals. Sunburst uses proportional nested arcs and one radial band per
visible depth. Missing and zero nodes have no area/angle, but retain stable
identity and appear in the accessible `Tk.DataTable`, with a distinct status.
`geometryFor(id)` exposes the normalized area or angle for numerical checking.
Selection/hit testing preserve stable IDs. Screen, PNG and SVG all use one
painter and the same geometry. The view caps output to 8192 pixels per side.

## Polar coordinate family

VIS-08 uses a dedicated [PolarView and PolarDataModel](polar.md) because
Cartesian `ChartAxis.map()` fractions do not encode bearing, wraparound,
category sectors or radial distance. The polar transform is shared by paint,
inverse coordinates and hit testing. Its declared units and category order
travel with full-resolution CSV and the accessible table.
