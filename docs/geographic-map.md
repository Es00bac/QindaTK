<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local geographic mapping

`MapView` displays local GeoJSON vector features using `GeographicFeatureModel` and
`GeographicMapCanvas`. The example in `examples/geographic-map/Main.qml` loads a
five-feature fixture, selects Denver and opens a local 25-tile illustrated XYZ
basemap. No network request occurs. The [live fixture screenshot](screenshots/geographic-map.png)
shows the composed raster/vector map, selected attributes, tile state and both
source attributions. A [comfortable-density capture](screenshots/geographic-map-comfortable.png)
shows the same scene with the larger inspector and spacing.

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

## Offline XYZ raster source

`MapView.loadTileManifest(path)` reads a local JSON manifest into its built-in
`GeographicTileSource`; apps may also assign a source to `MapView.tileSource`.
The manifest uses the standard 256-unit XYZ world grid, with `z`, `x`, `y` (Y
increases southward), `minZoom`, `maxZoom`, decoded image `tileSize` (64–1024
square pixels), `attribution`, and explicit local
tile entries:

```json
{
  "tileSize": 256, "minZoom": 9, "maxZoom": 9,
  "attribution": "Example source",
  "tiles": [{"z": 9, "x": 106, "y": 194, "file": "tiles/106-194.png"}]
}
```

Tile paths must be relative and cannot contain `..`. The source rejects remote
manifests and invalid coordinates; a failed replacement leaves the prior source
intact. Decoding happens off the UI thread. A viewport change drops pending
requests for tiles no longer visible; late results are ignored. The decoded
cache defaults to 16 MiB and is bounded by `cacheLimitBytes` (1–256 MiB).
`tileStatus` reports ready, missing and failed visible tiles. Missing tiles
show the themed vector canvas beneath them. Attribution stays on screen even
when tiles are missing. The local fixture can be regenerated with
`examples/geographic-map/generate_tiles.py`.

The canvas draws tiles at the integer zoom level below the continuous camera
zoom and scales them to match vector geometry. Longitude wraps and local tiles
repeat across world copies. The manifest is deliberately sparse and offline;
the API can be adapted to a network provider later without changing MapView's
camera or vector contract.

## Remaining WP-084 scope

The vector renderer still scans local features for painting and picking. Large
spatial data needs a visible-region cache and spatial index. Network providers,
source freshness, layer ordering, projection switching, geometry edits,
annotations and export of a composed map remain. The GeoJSON reader assumes
WGS84; arbitrary CRS declarations are unsupported.
