<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-031 polar, radar, and radial chart lane evidence

The isolated lane at `7795ff3` built with Qt 6.11.1 using plain `cmake
--build` and the configured system `MAKEOPTS` (`-j24 -l24`). Focused
`tst_polar`, `example_polar`, and `example_gallery` passed 3/3; focused QML
tests passed 3/3. The two captures were opened and inspected:
[compact](../../../screenshots/polar-compact.png) and
[comfortable](../../../screenshots/polar-comfortable.png).

`PolarDataModel` validates stable series/point IDs, finite angles/radii,
consistent series kinds, and bounded records. `PolarRenderer` shares one
coordinate transform among numeric polar points, category-ordered radar
polygons, radial bars, inverse projection, hit testing, selection, and image
export. Invalid domain or category inputs produce a visible error. The
`PolarView` data table and CSV use the complete source model.

The compact preview measured `PolarView#examplePolar 0,0 416x572`,
`PolarView#exampleRadar 424,0 416x572`, and
`PolarView#exampleRadial 848,0 416x572`. Comfortable density measured
413×561, 413×561, and 413×561 respectively. Shared integration and
dual-Qt package gates are recorded in [WP-031](WP-031.md).
