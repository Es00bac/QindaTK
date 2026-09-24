<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-084 first slice: local geographic map

**State: active; package not accepted.** The `8d8caae` lane was cherry-picked
as `186b9d9`. `GeographicFeatureModel` loads and atomically saves local
GeoJSON features with stable IDs, attributes and explicit WGS84 coordinates.
`GeographicMapCanvas` projects them to Web Mercator, draws points, lines and
polygons across the antimeridian, and supports pan, zoom and picking.
`MapView` exposes the selected attributes, attribution and a visible offline
tile status. See [geographic-map.md](../../../geographic-map.md) for the API.

The lane's focused geographic model and example checks passed 2/2. After
integration, the configured `-j24 -l24` build passed and
`tst_geographic_map`, `tst_local_property_sync` and
`example_geographic-map` passed 3/3. The merged preview dump located
`MapView#fixtureMap` at 8,32 944×581,
`GeographicMapCanvas#mapCanvas` at 0,0 944×581, and
`Rectangle#mapInspector` at 620,4 320×68. The merged
[map screenshot](WP-084-map-merged.png) was opened: local vectors, selected
attributes, attribution and offline tile status are legible.

Raster/tile providers, spatial indexing and visible-region caching, mixed
layers, edits, projection switching and composed export remain open. A tile
provider lane is now in progress. This is a toolkit map surface and model,
not a complete GIS application.
