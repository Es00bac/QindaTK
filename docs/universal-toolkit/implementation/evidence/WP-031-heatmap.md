<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-031 DAT-14 Heatmap lane

Status: implemented and locally verified at this lane's revision. WP-031 remains
active; this evidence does not accept its DistributionPlot, GanttView or other
chart-family obligations.

## Data and interaction contract

`HeatmapModel` is a sparse numeric or categorical matrix with declared X/Y
numeric domains or ordered categories, X/Y/value units, a declared numeric
color scale, stable cell IDs and explicit missing cells. Invalid domains and
category declarations expose `valid`/`error`. `tile()` returns no more than
4096 viewport aggregate bins without visiting unstored cells. The latest
`requestTile()` wins and `cancelTile()` invalidates pending delivery. The
renderer uses one bounded painted item instead of per-cell QML delegates.
Tiles preserve both numeric extrema, category mixture and missing counts;
missing data is hatched in the visual output. Selection supports cells and
half-open rectangular regions by pointer drag, Shift+arrow and API.
Accessible descriptions state the current ID, coordinates, units, missing or
value state, and region dimensions. Screen, PNG and SVG use the same painter
and viewport data coordinates; output dimensions are bounded to 1–8192.

## Local verification

Environment: Gentoo, Qt 6.11.1, GCC 15.3.0, offscreen Qt platform and software
Quick backend. The configured `MAKEOPTS` was `-j24 -l24`; the build used plain
`cmake --build build/wp031-heatmap` without an override.

- Isolated CMake configure and full build: passed.
- `ctest --test-dir build/wp031-heatmap -R '^(tst_heatmap_model|example_heatmap)$' --output-on-failure`: 2/2 passed.
- `tst_qml -input tests/qml/tst_heatmap.qml`: 3/3 passed.
- Direct `qmllint` of `Heatmap.qml`: exit 0, with one Qt metadata warning
  for the `HeatmapRenderer.model` C++ pointer type; runtime instantiation
  and focused QML tests pass.
- Large fixture: 100,000 × 10,000 declared cells with three stored cells;
  `tile(0,100000,0,10000,64,64)` returned exactly 4096 bins and preserved
  known minimum, maximum and missing counts.
- Pixel fixture: overview tile split known extrema, missing pixels remained
  distinct, theme colors changed the pixels, and PNG/SVG were written and
  decoded/inspected by the C++ test.

`qtk-preview` dumps after compact dark and comfortable light grabs:

```text
Main  0,0 900x580
  Heatmap#heatmap  12,12 876x556
    HeatmapRenderer#heatmapPlot  0,0 876x556
Main  0,0 900x580
  Heatmap#heatmap  14,14 872x552
    HeatmapRenderer#heatmapPlot  0,0 872x552
```

Both captures were opened and inspected: the low-to-high ramp, extrema, axes,
unit legend and diagonally hatched missing cells are visible in both themes.
See [compact dark](../../../screenshots/heatmap-compact.png) and
[comfortable light](../../../screenshots/heatmap-comfortable.png).
