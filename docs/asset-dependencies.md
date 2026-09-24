<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Asset dependency index

`AssetDependencyIndex` tracks directed edges from an asset to the inputs it
needs. IDs are stable host strings; the bundled QML file adapter uses paths
relative to `projectRoot`. An edge remains when its target is missing, so
the index can explain and repair broken references. `assets`, `asset(id)`,
`dependencies(id)`, `dependents(id)`, `affectedBy(id)` and `diagnostics`
expose the graph to QML. `affectedBy` follows reverse edges transitively:
replacing an input lists every output that may need rebuilding. Missing edges
and strongly connected cycles have explicit diagnostics.

## Ingestion

Set `projectRoot` to an existing directory before adding assets. Call
`scanQmlFile("path/from/root.qml")` for a local QML source. The adapter reads
at most 1 MiB, extracts quoted local directory/file imports and literal
`source`, `iconSource`, `url`, `file`, `fileName` and `texture` references,
registers referenced assets and replaces only that source's outgoing edges.
Commented references are ignored. A directory import lists its local QML
files; remote URLs, qrc URLs and dynamic JavaScript paths are outside this
adapter. Re-scan an edited file; a missing reference remains in the graph
with `available: false`.

For any domain, call `upsertAsset(id, label, uri, available, kind)`, then
`beginScan(id)` and `commitScan(id, token, dependencyIds)`. A later
`beginScan` invalidates an older token, so an obsolete worker result cannot
replace newer edges. A source owns its outgoing edge set; `commitScan`
replaces it atomically. Call `setAvailable(id, false)` when an asset is
deleted. The index caps assets at 4,096, edges at 16,384 and each source at
128 inputs. This first adapter scans one file synchronously and rejects a
file that changes during the bounded read. Hosts can run extraction on a
worker and commit its edges with the same generation token.

`Tk.AssetDependencyView` binds `index`, shows a virtualized asset list,
filter, input/dependent/impact lists and missing/cycle diagnostics. Selection
uses the stable asset ID. Open, Relink and Rebuild emit requests; the host
decides how to navigate, calls `ResourceResolver` or a document transaction
for relinking, and schedules valid rebuilds. The index does not rewrite URIs,
compile outputs or store document history.

The fixture in `tests/fixtures/asset_dependencies` includes a real local
directory import, an existing image, a missing image, a commented reference
and two files forming a dependency cycle. The focused model test scans it;
the QML interaction test exercises navigation and relink requests.
