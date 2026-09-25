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

Each edge records where it was written: `references(id)` returns
`{targetId, kind, line, reference, available}` per input, with `kind`
`import`, `reference` or `host`, a 1-based `line` (0 for host edges) and the
reference text as written. A missing diagnostic carries the same `line` and
`reference` and its message names the line, e.g.
`Main.qml line 10 needs missing images/lost.png`
([D-163](decisions-domains.md#d-163--edges-record-where-they-were-written-relinks-are-plans)).

## Ingestion

Set `projectRoot` to an existing directory (a path or a `file:` URL) before
adding assets. `scanProjectAsync()` scans every QML file under it on a
worker thread and returns a request ID; `scanning` is true until
`projectScanFinished(requestId, sourceCount, skippedCount)` or
`projectScanDiscarded(requestId)`. A newer scan or `cancelProjectScan()`
discards an older one. A source rescanned while the project scan ran keeps
its newer edges and counts as skipped. An indexed QML file that no longer
exists becomes unavailable and loses its outgoing edges, so what used it
reports a missing input. Projects with more than 4,096 QML files are
refused with `lastError`
([D-162](decisions-domains.md#d-162--project-dependency-scans-read-off-thread-and-yield-to-newer-answers)). Call
`scanQmlFile("path/from/root.qml")` for a local QML source. The adapter reads
at most 1 MiB, extracts quoted local directory/file imports and literal
`source`, `iconSource`, `url`, `file`, `fileName` and `texture` references,
registers referenced assets and replaces only that source's outgoing edges.
Commented references are ignored. A directory import lists its local QML
files; remote URLs, qrc URLs and dynamic JavaScript paths are outside this
adapter. Re-scan an edited file; a missing reference remains in the graph
with `available: false`.

For any domain, call `upsertAsset(id, label, uri, available, kind)`, then
`beginScan(id)` and `commitScan(id, token, dependencyIds)`, or
`commitScanEdges(id, token, [{targetId, kind, line, reference}])` to record
where each edge came from (the earliest line wins for a repeated target). A later
`beginScan` invalidates an older token, so an obsolete worker result cannot
replace newer edges. A source owns its outgoing edge set; `commitScan`
replaces it atomically. Call `setAvailable(id, false)` when an asset is
deleted. The index caps assets at 4,096, edges at 16,384 and each source at
128 inputs. `scanQmlFile` scans one file synchronously; both it and the
project scan reject a file that changes during the bounded read
(`scanRejected`). Hosts can run their own extraction on a worker and commit
its edges with the same generation token.

## Relinking

`relinkCandidates(id)` lists project files with the same file name as a
missing asset (a bounded walk: 20,000 entries, 32 results).
`relinkPlan(missingId, replacementId)` returns, per dependent,
`{sourceId, line, kind, reference, replacement, note}`. For a `reference`
edge `replacement` is the new path relative to the source's directory; a
`host` edge gets the replacement ID; a directory `import` gets an empty
replacement and a `note`. The index never writes files: the host applies the
plan in its own transaction and rescans the edited sources.

`Tk.AssetDependencyView` binds `index`, shows a virtualized asset list,
filter, input rows with line and kind, dependent/impact lists and
missing/cycle diagnostics. While a project scan runs it shows its progress
state and a Cancel button. For a missing asset that something still uses it
lists relink candidates; choosing one emits
`relinkChosen(assetId, replacementId, plan)`. Selection
uses the stable asset ID. Open, Relink and Rebuild emit requests; the host
decides how to navigate, calls `ResourceResolver` or a document transaction
for relinking, and schedules valid rebuilds. The index does not rewrite URIs,
compile outputs or store document history.

The fixture in `tests/fixtures/asset_dependencies` includes a real local
directory import, an existing image, a missing image, a commented reference
two files forming a dependency cycle, and `archive/lost.png` as a relink
candidate. The model tests scan it directly and as a private copy that they
edit and delete from; the QML interaction test exercises navigation, line
display, relink candidates and relink requests.
