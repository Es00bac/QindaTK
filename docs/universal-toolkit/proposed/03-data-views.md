<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Data views and visualization

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

Current [DataTable/Graph/Meter](../current/07-data-telemetry.md) remain useful for
readouts and telemetry. These proposals add general models, two-dimensional
editing and analytical visualization. A layout Grid is not an editable data
view, and telemetry history is not an arbitrary chart dataset.

## Proposed visualization family coverage

ChartView/ChartSeries are a framework contract, not a claim that one renderer
already implements all chart families. Implement the needed families as series
or adapters; share axes, selection, legends and export. These are hypothetical
capabilities within the named proposals, not extra shipped types.

| Family | Required data/semantics | Likely component path |
| --- | --- | --- |
| Line, step, area, stacked area | Explicit x/y, gaps, interpolation and stack baseline | ChartView + ChartAxis + ChartSeries |
| Bar, grouped/stacked bar, waterfall | Categories, ordered groups, totals and positive/negative contribution | ChartSeries with categorical axes |
| Scatter, bubble, vector field | Point IDs, x/y, size or direction and units | ChartSeries with hit/selection index |
| Pie/donut, treemap, sunburst | Part-to-whole values and, where needed, hierarchy | Specialized series; explicit totals and accessible tabular alternative |
| Heatmap and matrix | Cell coordinates, values, missing-data and color scale | Heatmap with tile/model adapter |
| Histogram, density, box, violin | Bin edges or declared statistical summaries/estimator | Histogram or DistributionPlot |
| Error bars and confidence/credible bands | Interval endpoints and their stated meaning | ChartSeries metadata and an uncertainty renderer |
| Radar, polar, radial plots | Angular/radial dimensions with explicit ordering/units | Alternate ChartAxis/plot-coordinate adapter |
| OHLC/candlestick | Time, open/high/low/close, trading-calendar gaps | Specialized ChartSeries; application supplies calendar/market semantics |
| Bullet/gauge/KPI readouts | Current/target/range and threshold meaning | Meter plus labels/markers, or specialized chart delegate |
| Network, flow and Sankey diagrams | Node/edge IDs, direction, quantity and layout | NodeGraph/DiagramCanvas plus flow/layout engine |
| Geographic and spatial data | Geometry, coordinate reference system and attribute values | MapView and geographic renderer |
| Gantt/calendar/resource views | Intervals, working calendars, dependencies and allocation | GanttView, CalendarView and ResourceTable |
| 3D scatter/surface/volume | Typed coordinates/arrays, units and sampling/interpolation | ScientificPlot, SceneView3D or VolumeView adapters |

Every family needs selection/inspection, keyboard access, legend/axis semantics,
and export tests on known data. Derived aggregation, statistics and layouts must
retain provenance rather than becoming hidden calculations in a drawing delegate.

<a id="dat-01"></a>
## DAT-01 — VirtualList

**Priority:** P1. **Purpose:** Reusable large-list view with selection, loading and keyboard behavior.

**Contract sketch:** Model adapter, stable IDs, delegate, selection model, loading/empty/error states, activation and context-menu requests.

**Dependencies and boundary:** Qt item-view machinery where suitable, ListRow and asynchronous model pagination.

**Completion check:** Scroll a paged list while rows are inserted/removed; selection and pending results remain attached to the correct IDs.

<a id="dat-02"></a>
## DAT-02 — VirtualTree

**Priority:** P1. **Purpose:** Model-driven hierarchy with expansion and lazy children.

**Contract sketch:** Root/model, expanded IDs, SelectionModel, column descriptors, loading indicators, rename and move requests.

**Dependencies and boundary:** Hierarchy adapter and TreeRow visuals; domain validates parent changes.

**Completion check:** Navigate with arrows, expand lazy nodes, reorder by keyboard/pointer and preserve selection through filtering.

<a id="dat-03"></a>
## DAT-03 — AssetGrid

**Priority:** P1. **Purpose:** Virtual thumbnail browser for media, pages, presets or components.

**Contract sketch:** Asset IDs, display mode, tile extent, caption/metadata roles, ratings and selection; demand-driven preview requests.

**Dependencies and boundary:** AssetProvider and SelectionModel; grouping/search supplied by model/proxies.

**Completion check:** Browse a large catalog without one delegate/decoder per asset; late previews never land on recycled tiles.

<a id="dat-04"></a>
## DAT-04 — EditableGrid

**Priority:** P1. **Purpose:** General two-axis virtualized cell editor.

**Contract sketch:** Standard row/column model, stable cell addressing, edit delegates, range selection, header/frozen regions and commit/validation hooks.

**Dependencies and boundary:** Model adapter, SelectionModel, PropertyBinding and ClipboardService; spreadsheet formulas belong elsewhere.

**Completion check:** Edit/paste a rectangular range, scroll both axes, undo, and keep frozen headers aligned through column resizing.

<a id="dat-05"></a>
## DAT-05 — HeaderView

**Priority:** P1. **Purpose:** Synchronized row/column headers for data and sheet views.

**Contract sketch:** Sections use stable IDs, labels, resize/reorder/sort state and hidden/frozen flags; emit validated layout requests.

**Dependencies and boundary:** EditableGrid or table view layout adapter.

**Completion check:** Reorder/resize columns while scrolled; labels, hit testing and accessible associations stay synchronized.

<a id="dat-06"></a>
## DAT-06 — RangeSelectionOverlay

**Priority:** P1. **Purpose:** Keyboard and pointer representation of cell or item ranges.

**Contract sketch:** Active/anchor/ranges, focus rectangle, fill/resize handles and drag preview; map screen points through the view adapter.

**Dependencies and boundary:** SelectionModel and grid geometry; domain controls allowed operations.

**Completion check:** Discontiguous ranges and offscreen extension survive recycling and Escape restores the previous selection/edit state.

<a id="dat-07"></a>
## DAT-07 — FilterBar

**Priority:** P1. **Purpose:** Composable typed filters with visible active criteria.

**Contract sketch:** Field schema, operator/value tokens, AND/OR grouping and saved presets; emit validated filter expressions.

**Dependencies and boundary:** Query/proxy adapter; UI does not concatenate raw database query strings.

**Completion check:** Invalid filters remain editable and explained; applying/removing a filter preserves stable selection policy.

<a id="dat-08"></a>
## DAT-08 — QueryBuilder

**Priority:** P2. **Purpose:** Visual construction of structured queries and transformations.

**Contract sketch:** Schema fields, typed predicates, joins/grouping/sort/projection and preview request; display execution diagnostics.

**Dependencies and boundary:** Query AST and data-source adapter validate and execute the request.

**Completion check:** A visual query roundtrips to its structured form; canceling preview cannot overwrite a newer result.

<a id="dat-09"></a>
## DAT-09 — PivotView

**Priority:** P2. **Purpose:** Interactive aggregate table and field arrangement.

**Contract sketch:** Rows/columns/value/filter descriptors, expansion and totals; field wells emit aggregate-layout edits.

**Dependencies and boundary:** Aggregation/data-model engine supplies results and dimensional metadata.

**Completion check:** Move a field between axes, verify totals against known data, and retain layout through save/reopen.

<a id="dat-10"></a>
## DAT-10 — ChartView

**Priority:** P1. **Purpose:** Host for analytical charts with shared plot layout and interaction.

**Contract sketch:** Chart model, axes, series, legend, annotations, selection and export context; pointer/keyboard inspection uses data coordinates.

**Dependencies and boundary:** Chart renderer and model adapters; computations remain in data services.

**Completion check:** Resize, zoom and select data with synchronized axes/legend, accessible tabular alternative and faithful export.

<a id="dat-11"></a>
## DAT-11 — ChartAxis

**Priority:** P1. **Purpose:** Explicit numeric, categorical, temporal or logarithmic axis.

**Contract sketch:** Domain, transform, tick/label policy, units and locale; support linked axes, reversed direction and invalid-domain feedback.

**Dependencies and boundary:** Chart layout/scale model; time axes receive an explicit time-zone policy.

**Completion check:** Log axes reject invalid domains; zoom/pan updates labels without implying false spacing between categories.

<a id="dat-12"></a>
## DAT-12 — ChartSeries

**Priority:** P1. **Purpose:** Data contract for line, area, bar, stacked, scatter, bubble and related plots.

**Contract sketch:** Stable point IDs, x/y/category/value roles, missing-value policy, style and error-band metadata; immutable or incremental updates.

**Dependencies and boundary:** Dataset adapter and ChartView renderer; aggregation/decimation is explicit.

**Completion check:** Known datasets preserve extrema, gaps and identities when downsampled, selected and exported.

<a id="dat-13"></a>
## DAT-13 — ChartLegend

**Priority:** P1. **Purpose:** Accessible legend and series visibility control.

**Contract sketch:** Series IDs/names/style samples, grouping and visible state; toggle requests route through chart state.

**Dependencies and boundary:** Chart model and categorical theme palette.

**Completion check:** Toggling a legend item affects only its series; keyboard and non-color cues identify every series.

<a id="dat-14"></a>
## DAT-14 — Heatmap

**Priority:** P2. **Purpose:** Dense two-dimensional scalar or categorical data display.

**Contract sketch:** Cell/tile provider, explicit x/y axes, color scale, missing-data representation and selection regions.

**Dependencies and boundary:** Matrix/tile provider and domain aggregation; legends state numeric units.

**Completion check:** Known extrema and missing values remain distinguishable across zoom, theme and image export.

<a id="dat-15"></a>
## DAT-15 — Histogram

**Priority:** P1. **Purpose:** Binned distribution view for images, signals and datasets.

**Contract sketch:** Explicit bins/edges/counts, channels, normalization and log display options; selection emits a domain range.

**Dependencies and boundary:** Analysis backend computes bins; ColorContext is required for image-channel meaning.

**Completion check:** Display an empty, constant and clipped dataset with correct bin boundaries and documented normalization.

<a id="dat-16"></a>
## DAT-16 — DistributionPlot

**Priority:** P2. **Purpose:** Box/violin/error-band and statistical comparison display.

**Contract sketch:** Groups, summary statistics or density samples, interval meaning and units; expose underlying values for inspection.

**Dependencies and boundary:** Statistics backend supplies computation/provenance; toolkit renders and selects.

**Completion check:** Known quantiles/intervals match source data and the legend states what each mark represents.

<a id="dat-17"></a>
## DAT-17 — GanttView

**Priority:** P2. **Purpose:** Planning timeline with task rows, dependencies and baselines.

**Contract sketch:** Task IDs, starts/durations/progress, working-calendar mapping, links and edit requests; sync rows with a data grid.

**Dependencies and boundary:** Scheduling engine validates constraints and recalculates dates.

**Completion check:** Move a task over a nonworking day, preview affected dependencies, then commit/undo the whole valid schedule edit.

<a id="dat-18"></a>
## DAT-18 — EventLogView

**Priority:** P1. **Purpose:** Virtualized structured events with severity and source navigation.

**Contract sketch:** Append stream, time/source filters, paused-follow mode, bounded retention and detail/export requests.

**Dependencies and boundary:** Log provider and explicit retention policy.

**Completion check:** High-rate events do not force per-event UI work; paused inspection preserves position while new events arrive.

<a id="dat-19"></a>
## DAT-19 — DiffView

**Priority:** P2. **Purpose:** Side-by-side or inline difference presentation.

**Contract sketch:** Typed diff model, aligned regions, changes and accept/reject/navigation requests; content delegates support text or structured data.

**Dependencies and boundary:** Diff/merge engine and document transactions.

**Completion check:** Accept one hunk, recompute against new revisions and preserve conflict identity; canceled edits do not alter originals.

<a id="dat-20"></a>
## DAT-20 — ResourceTable

**Priority:** P2. **Purpose:** Editable allocation matrix for people, machines or budgets.

**Contract sketch:** Resource IDs, time buckets, capacity/usage/conflict roles and allocation edit requests.

**Dependencies and boundary:** Planning/constraint model; units and aggregation are explicit.

**Completion check:** Change an allocation and verify related totals/conflicts update atomically and undo correctly.
