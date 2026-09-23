<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Chart data foundation

WP-030 introduces a typed C++ foundation for general charts. The public
headers are `chart_axis.h`, `chart_series.h`, `chart_data.h`, and
`chart_histogram.h`. `ChartAxis` and `ChartSeries` are registered as QML
types, but dataset ingestion and geometry in this stage are C++ APIs. A
`ChartView`/legend/renderer will use these contracts in the visual stage.
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

## Current boundary

The typed foundation does not yet provide visual drawing, zoom/pan gestures,
legend controls, accessibility navigation, or image/vector export. The table
and CSV functions provide the data for an accessible alternative and data
export, but a later ChartView must connect them to interaction and the format
and job services. The renderer must test GPU and software paths and keep
telemetry `Graph` compatibility before WP-030 can be accepted.
