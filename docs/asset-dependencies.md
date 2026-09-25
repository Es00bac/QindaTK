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
adding assets. Changing it cancels a running project scan, whose result is
then discarded. `scanProjectAsync()` scans every QML, SVG, HTML and CSS file
under it on a worker thread and returns a request ID; `scanning` is true until
`projectScanFinished(requestId, sourceCount, skippedCount)` or
`projectScanDiscarded(requestId)`. A newer scan or `cancelProjectScan()`
discards an older one. A source rescanned while the project scan ran keeps
its newer edges and counts as skipped. An indexed source that no longer
exists becomes unavailable and loses its outgoing edges, so what used it
reports a missing input; once nothing uses it, it is removed. The
placeholder of a reference that was removed from its source goes the same
way. Only entries a scan created are pruned like this; host-registered
assets go through `removeAsset(id)` (a used asset stays as a missing
placeholder) or `pruneMissing()` (every missing asset nothing uses)
([D-205](decisions-domains.md#d-205--scans-prune-what-they-created-rebuilds-need-a-valid-closure)).
Projects with more than 4,096 source files are
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

Documents and media reference each other through markup, so
`scanFile(path)` (and the project scan) also read `.svg`, `.html`/`.htm` and
`.css` sources: `href`, `xlink:href`, `src` and `poster` attributes, CSS
`url(...)` (kind `reference`) and CSS `@import` (kind `import`), with the
same local-path rules, 1 MiB bound and line numbers. `#fragment` and
`?query` are dropped from the target ID but kept in `reference`; `<!-- -->`
and `/* */` comments are ignored. Sources get kind `qml`, `svg`, `html` or
`css`.

Generated outputs come from the render queue: `indexRenderJobs(jobs)` takes
`RenderQueueModel.jobs` rows and makes each `outputPath` (project-relative
when inside the root) an asset of kind `output` that depends on its
`sourceSnapshot` (or `sourceId`) and on the outputs of the jobs in its
`dependencies`. An output is available when its job succeeded and the file
exists.

Documents that link media through `Tk.DocumentResourceLinks` (office
documents, `.sloom` projects and any other `PropertyDocument` format) need no
format parser: `indexDocumentResources(documentId, links)` makes the document
depend on each logical `resourceId` in the links' manifest, with the
resolver's URI as `reference` and kind `resource`. A resource is missing when
the resolver reports `missing` or does not know it, so a rebuild of the
document is blocked until it is relinked. Re-index after the manifest
changes; an unlinked resource's edge goes and, if nothing else uses it, so
does its entry.

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
`relinkCandidatesAsync(id)` does the same walk on a worker and answers with
`relinkCandidatesReady(requestId, id, candidates)`; views use it.
`relinkPlan(missingId, replacementId)` returns, per dependent,
`{sourceId, line, kind, reference, replacement, note}`. For a `reference`
edge `replacement` is the new path relative to the source's directory; a
`host` edge gets the replacement ID; a directory `import` gets an empty
replacement and a `note`. The index never writes files: the host applies the
plan in its own transaction and rescans the edited sources.

## Rebuilds

`rebuildBlockers(id)` lists what makes rebuilding `id` invalid: a missing
leaf (a missing asset with no inputs of its own) or a cycle anywhere in its
dependency closure. A missing asset that has inputs is a generated output
the rebuild recreates, so it does not block. `rebuildPlan(changedId)` answers
"what does replacing this input rebuild": `{valid, blocked}` where `valid`
lists the affected outputs with a valid closure, inputs before outputs, and
`blocked` gives `{id, reason}` for the rest.

## View

`Tk.AssetDependencyView` binds `index`, shows a virtualized asset list,
filter, input rows with line and kind, dependent/impact lists and
missing/cycle diagnostics. While a project scan runs it shows its progress
state and a Cancel button. For a missing asset that something still uses it
lists relink candidates (asked from the worker); choosing one emits
`relinkChosen(assetId, replacementId, plan)`. Rebuild is available only when
`rebuildBlockers` is empty, and a caption names the first blocker; Rebuild
affected emits `rebuildAffectedRequested(assetId, validIds)` from
`rebuildPlan`. A Details/Graph switch shows the selected asset's
neighbourhood in `Tk.AssetDependencyGraph`, the shared `Tk.NodeGraph` with
inputs on the left and dependents on the right (12 per side, the rest
counted); selecting a node re-centres on it. Selection
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
