<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Polar, radar and radial plots

`PolarView` renders actual source coordinates. It supports numeric polar
points/lines/polygons, categorical radar polygons and categorical radial
bars. `PolarDataModel` holds the full-resolution records for painting, stable
selection, the accessible `DataTable` and CSV. The display is bounded to
10,000 records and 32 series, and exported PNG/SVG dimensions are 1–8192 px.
The separate Cartesian `ChartView` and `ChartAxis` APIs are unchanged.

## Coordinates and order

Set `angleMode` to `Tk.PolarRenderer.Numeric` with `angleUnit` `Degrees` or
`Radians`, or to `Category` with a unique, nonempty ordered `categories`
list (at most 64 names). Each record supplies exactly one of `angle` or
`category`, plus `radius`, stable `seriesId` and `pointId`, optional
`seriesName`, and `kind`. A series keeps one kind, name and angular mode.
Numeric line/polygon vertices are sorted by normalized angle. Categorical
vertices follow the declared category list, regardless of source row order.
A categorical polygon must cover every category exactly once and have at
least three points. Radial bars require categories; their sector width is
75% of one category interval. `Points` and `Polyline` work in either mode.

Zero points north. Angles grow clockwise by default; `clockwise: false`
reverses that direction. `startAngleDegrees` rotates the zero direction in
degrees even when input angles are radians. The plotted radius is
`(value - radialMinimum) / (radialMaximum - radialMinimum)`, with a finite
nonnegative increasing domain. A source radius outside the declared domain,
unknown category, or incompatible angular mode makes the renderer invalid
and displays its error rather than clipping or silently moving data.
`project`, `projectCategory`, `unproject` and hit testing all use the same
transform. `unproject` returns angle in the declared numeric unit and the
nearest category when applicable. Radial-bar hit testing accepts the wedge
interior, while point/line/polygon selection uses the actual projected
sample positions. The origin has no defined bearing, so `unproject` returns
an empty result there. A visible series key names each colour; the data table
also carries the names and exact values.

## Source and nonvisual access

`replacePoints(rows)` validates the whole batch before changing the model.
It rejects duplicate `(seriesId, pointId)`, nonfinite or negative radii,
nonfinite or excessively large angles, inconsistent series metadata and
malformed records. `appendPoint` rejects a full model instead of evicting
source data. The `get(index)` records back `DataTable`, including stable IDs.
Set `showDataTable` for a full-resolution accessible table; arrow keys move
selection through source rows in their original order. The plot emits
`pointSelected(seriesId, pointId)` for pointer and keyboard selection.

`csvData()` includes source angles/categories, declared units, category
indices, radial domain, zero rotation and direction; it quotes fields using
RFC 4180 rules. `renderImage`, `exportPng` and `exportSvg` use the same
renderer as the screen; image export also signals `exportRequested` for a
host format/job service. No radial interpolation or statistics are inferred.

See [the example](../examples/polar/Main.qml) for radians, a two-series radar
comparison and directional radial bars. Stable object names are in
[controls.md](controls.md).
