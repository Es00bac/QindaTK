<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Scientific plotting (WP-085)

Scientific plots show measured and computed float64 data with declared units,
explicit gaps and host-supplied uncertainty, and export figures that carry
their own provenance. Decisions: [D-138 to D-141](decisions-domains.md#d-138--scientific-data-is-float64-with-explicit-gaps-and-exact-units).

| Type | Role |
|---|---|
| `ScientificDataset` | A series of samples (trace or scatter, 2D or 3D) |
| `ScientificGrid` | A sampled surface z(x, y) on a rectilinear lattice |
| `ScientificPlot` / `ScientificPlotRenderer` | 2D figure: series, surface colour map, annotations |
| `ScientificFigureComposer` | Multi-panel figure export |
| `ScientificPlot3D` / `ScientificScene3D` | 3D surface and scatter plot |

## Data semantics

**Precision.** Every coordinate, value and uncertainty offset is a C++
`double` (IEEE float64) and is exported with 17 significant digits, which
round-trips exactly. `precision` reports `float64`.

**Shape and dimensions.** A `ScientificDataset` has `dimensions` 2 (`x`, `y`)
or 3 (`x`, `y`, `z`), declared before samples; `shape` is `[N,dimensions]`.
A `ScientificGrid` has `shape` `[rows,columns]`: column `c` is `xs[c]`, row
`r` is `ys[r]`, and row 0 is the **smallest** Y (unlike `HeatmapModel`,
whose row 0 is the top). Values are row-major.

**Ordering.** `kind: Trace` requires strictly increasing X; traces are drawn
as lines, support `Band` mode and binary-searched range reads. `kind: Scatter`
accepts any finite X, including repeats, and is drawn as markers.

**Missing values.** Input must be finite; `NaN` and infinities are rejected,
never treated as gaps. A missing Y is declared with `missingY: true`; the
row stays in the table, CSV and metadata, and is drawn as a gap marker on the
X axis. It breaks a trace line and is never drawn as zero. A missing grid
node is `null`. Interpolation that would give a missing node a nonzero weight
returns missing, so a hole is never filled; nodes next to a hole keep their
exact values.

**Uncertainty.** The toolkit never estimates uncertainty. Offsets are
distances from the estimate, nonnegative and finite, with finite endpoints:
`sigmaY` (symmetric) or `sigmaYLow` + `sigmaYHigh` (asymmetric), and likewise
`sigmaX` or `sigmaXLow` + `sigmaXHigh`. Mixing both forms, or giving only one
side, rejects the batch. Error bars are labelled "host supplied ±1σ"; they
are not confidence intervals. Trace samples also project to an
`UncertaintyModel` (`intervals`) for `UncertaintyView`; Scatter samples never
do. Changing `kind` re-projects `intervals` (Scatter empties it, Trace fills
it from the current samples).

**Units.** Units are strings compared exactly. The renderer never converts
units: every series, the surface and every annotation must match the axis
units, or `problem` names the mismatch and the figure draws only that message.

**Bounds.** A dataset holds at most 100,000 samples; a grid 2–4,096 nodes per
axis and at most 1,048,576 nodes. Replacement is atomic: any invalid row
rejects the whole batch and leaves the previous data, metadata and model rows
unchanged. `samplesInRange(xMin, xMax, limit)` returns at most 10,000 rows;
`ScientificGrid.window(...)` returns at most 512 × 512 real nodes at an
integer stride.

## 2D figures

```qml
Tk.ScientificPlot {
    dataset: measured          // series 0 (first-slice API)
    series: [model, probes]    // more series
    surface: field             // optional colour map (ScientificGrid)
    annotations: notes         // ChartAnnotationModel: Point, VerticalLine, HorizontalLine
    xLabel: "Time"; yLabel: "Response"
    title: "Exponential decay"
}
```

`xAxis`/`yAxis` are `ChartAxis` objects (numeric or logarithmic) whose units
follow the first series (or the surface). Ticks are calibrated from the
visible domain: 1/2/5 × 10ⁿ steps on linear axes; every decade on log axes,
plus 2 and 5 multiples while fewer than three decades are visible. Axis
titles read `Label (unit)`. On a log axis, samples with non-positive values
cannot be projected; they are counted by `omittedCount()`, named in the
footer and summary, and stay in the table and metadata.

The surface is sampled once per raster pixel centre (raster at most
1,024 × 1,024 pixels) with `surfaceInterpolation` (`Nearest`: nearest node,
exact midpoints go to the lower index; `Bilinear`). Colours come from the
theme's thermal ramp over the grid's finite Z range; the colour bar shows the
range and Z unit. Missing nodes show the missing colour.

Series colours cycle through theme roles. A legend appears with two or more
series. Trace markers are drawn while they cannot touch (painted samples ×
4 × point radius ≤ plot width); denser traces read as lines. Selection:
click a marker, or Left/Right to step samples and Up/Down to move to the
next series at the nearest X. `selectedSample` includes the
series index and source ID. `dataAt(px, py)` returns exact data coordinates,
units and, over a surface, the interpolated Z.

**Downsampling.** At most `paintPointLimit` samples per series are painted.
A trace larger than the limit is split into `limit / 5` equal index buckets;
each keeps its first, minimum-Y, maximum-Y and last present sample and its
first gap (an M4 envelope), so no peak and no gap disappears. A scatter series
keeps every k-th sample plus the samples with extreme X and Y.
`paintedIndices(series)` exposes the exact choice, and export metadata
records the method and painted counts.

**Textual alternative.** `showDataTable` swaps the figure for a table of the
series chosen by `tableSource` (−1: the surface nodes). `summary` is a plain
sentence description (axes, series, ranges, surface, notes, omissions) used
as the accessible description. `csvData()` is a long table of every series.
The tables read each row with the model's `get(index)` (`Tk.DataTable`'s row
contract): `ScientificDataset.get` is `sample()` plus its units and
`ScientificGrid.get` is the row-major node plus units.

## 3D plots

`ScientificPlot3D` shows a `ScientificGrid` surface and/or a 3D scatter
`ScientificDataset` (`dimensions: 3`) inside the domain cube `xMinimum` …
`zMaximum` (`fitDomain()` sets it from the data).

- **Transform.** Data (x, y, z) maps to scene (X, Y, Z) = (nx, nz, −ny) ×
  100 with n = 2(v − min)/(max − min) − 1: data Z is up. `toScene`,
  `fromScene` and `project(x, y, z)` expose it. The scene boundary is float32;
  data stays float64 on both sides of it.
- **Interpolation.** The surface is resampled on an explicit
  `resolution` × `resolution` lattice (2–256) spanning the grid, with
  `interpolation` `Nearest` or `Bilinear`. With a resolution equal to the
  grid size and a uniform grid, lattice nodes are the grid nodes. A cell
  with a missing corner is a hole (`holeCount`), never a zero.
  `latticeNode(r, c)` and `csvData()` give the displayed lattice.
- **Camera.** An orbit camera around the cube centre: `azimuth`, `elevation`
  (clamped to ±89°), `distance`, vertical `fieldOfView`, near/far clip. Drag
  or arrow keys orbit; wheel or +/− zoom; click selects the nearest point.
- **Points.** At most `pointLimit` points are drawn (default 4,096, at most
  16,384). Beyond it the scene keeps every k-th present sample plus the
  samples with extreme X, Y and Z and the selected point; `pointCount` is
  the drawn count and `sourcePointCount` the present count, and the footer,
  summary and export metadata (`pointSampling`) say when points are thinned.
  Selecting a drawn point is not a mesh change: the GPU buffer rewrites only
  the two affected points' vertices. Selecting a thinned point (from the
  table) adds it with one rebuild.
- **Rendering.** On RHI backends Qt Quick 3D draws the surface, points and
  frame from `ScientificGeometry3D` with a camera bound to the scene's
  camera properties; otherwise (and with `forceSoftware`) the software view
  draws the same scene with the export painter. The GPU renderer follows the
  WP-076 render-resource lifecycle ([scene-3d.md](scene-3d.md)): one renderer
  per view, counted by `renderGeneration`, replaced (`resetRenderer()`) when
  the window's scene graph is invalidated, so every GPU buffer is rebuilt
  from `ScientificScene3D`. `tst_scientific_geometry3d` checks the buffers
  headless; `tst_scientific_plot3d_render` draws, loses and recovers the
  scene graph and needs a display
  (`QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi`); the headless suite skips it.
- **Textual alternative.** `showDataTable` shows the table chosen by
  `tableSource`: `"surface"` (source grid nodes) or `"points"` (every 3D
  sample, drawn or not); with both a surface and points a switcher offers
  both. `csvData()` is the displayed lattice with units, `pointsCsvData()`
  the full point source. `summary` names up to eight points with their
  coordinates and units, and the selected point.
- **Labels.** Corner labels (`axisLabels()`) name the domain limits with
  units. They are drawn last on background chips and clamped inside the
  view, so the surface never hides or cuts them.

## Export and provenance

`exportPng(path, size)` and `exportSvg(path, size)` (2D figure, composer and
3D scene) paint with the same code as the screen, at an explicit pixel size
up to 8,192². Metadata is embedded as a PNG text chunk
(`QindaTK.ScientificFigure`, `QindaTK.ScientificComposite`,
`QindaTK.Scientific3D`) or a base64 JSON `<metadata>` element
(`qindatk-scientific-figure`, `qindatk-scientific-composite`,
`qindatk-scientific-3d`). It contains every source's ID, label, kind, shape,
precision, units, host parameters, full samples or a SHA-256 digest of the
lattice values, and all render parameters: title, axis kinds/domains/units,
viewport, mode, sampling method and painted counts, surface interpolation,
colour domain and raster size, annotations, the selection (`selection`, or
`selectedPoint` in 3D), and a `style` object with every colour and colour
ramp (`#AARRGGBB`), the metrics map (margins, font family and size, line
width, radii) and the font family it resolved to; for 3D also the domain,
mapping, lattice resolution, triangle/hole counts, point sampling and full
camera. Identical inputs, size and fonts give byte-identical PNG and SVG
files, also from freshly built objects (`freshInstancesAndRenderProvenance`,
`freshInstancesExportIdenticalBytes`). `metadataJson(width, height)` and
`csvData()` provide sidecars.

`ScientificFigureComposer` lays out `panels` (each `plot.renderer`) in
`columns`, labels them (a), (b), …, and embeds each panel's metadata.

## Scope notes

Qualification is planned elsewhere: performance measurement at scale belongs
to WP-091, output fidelity (comparing the GPU image with the export) to
WP-089, screen-reader runs and translation to WP-092, and render-context loss
on each claimed platform/backend to WP-088. Out-of-core arrays larger than the
in-memory bounds above are not provided: hosts window their data into these
models.

Examples: [scientific-plot/Main.qml](../examples/scientific-plot/Main.qml),
[scientific-plot-3d/Main.qml](../examples/scientific-plot-3d/Main.qml).
