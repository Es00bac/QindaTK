<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local geographic mapping

`MapView` displays local GeoJSON vector features using `GeographicFeatureModel` and
`GeographicMapCanvas`. The example in `examples/geographic-map/Main.qml` loads a
five-feature fixture and selects Denver. No network request or tile fetch occurs.
The [live fixture screenshot](screenshots/geographic-map.png) shows the vector features,
selected attributes, tile state and attribution. The `tileStatus` value is `unsupported-offline-vector-only`; a blank base map is
therefore expected, and source attribution remains visible in the bottom corner.

## Coordinates and source contract

Input GeoJSON coordinates are WGS84 longitude, latitude in degrees, in that order
(`EPSG:4326`). Longitude must be in `[-180, 180]` and latitude in `[-90, 90]`.
The displayed Web Mercator projection (`EPSG:3857`) uses normalized world X east
and Y south, both 0–1 for the primary world. Its latitude is clamped to
±85.0511287798066°. `project()` reports `latitudeClamped`; invalid numeric or
out-of-range coordinates return `{valid: false}`. `unproject()` wraps longitude
into `[-180, 180)` and clamps normalized Y to `[0,1]`.

`MapView.coordinateAt(screenX, screenY)` returns WGS84 longitude/latitude with a
`crs` field. `screenPosition(lon, lat)` returns pixels in the view. Zoom is a
continuous Web Mercator scale, 256 × 2^zoom pixels per world, limited to 0–18.
Mouse drag pans, wheel zooms about the pointer, arrow keys pan, and +/- zoom.
Longitude wraps. Each line segment takes its shortest crossing of the
antimeridian; geometry is painted in adjacent world copies for seam continuity.
This is appropriate for local features, not routes intentionally spanning over
180° in longitude.

The model accepts GeoJSON FeatureCollection entries with Point, MultiPoint,
LineString, MultiLineString, Polygon and MultiPolygon geometry and object
properties. Explicit feature IDs are retained; omitted IDs become a stable
SHA-256-derived `geo-` ID from geometry and properties. Duplicate IDs and
unsupported or invalid geometry reject the whole load, leaving prior data intact.
`loadGeoJson()` and `saveGeoJson()` accept only local paths or `file:` URLs;
saving is atomic and keeps IDs, attributes and the collection `attribution`
string. Selecting a feature exposes its ID, type and properties in
`selectedFeature`, and the overlay inspector prints those attributes.

## Scope of this slice

The renderer scans local features for painting and picking. Large spatial data
needs a visible-region cache and spatial index. Raster and tile providers,
source freshness, mixed layers, projection switching, geometry edits and
annotations, and export of a composed map remain for WP-084. The current
GeoJSON reader handles the six listed geometry types and assumes GeoJSON's
WGS84 CRS; arbitrary CRS declarations are unsupported.
