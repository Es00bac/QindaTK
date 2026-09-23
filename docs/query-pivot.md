<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Typed query and pivot core

`query_core.h` defines the WP-029 data contract behind FilterBar,
QueryBuilder and PivotView. This package supplies C++ execution and the grid
adapter; QML controls are separate. The host owns data storage, source
revision, schema revision, editing commands and remote services.

## Query contract

`QuerySpec` names an immutable schema ID/revision, source revision and unique
query request ID. It holds a typed predicate tree, named typed parameters, row/column dimensions and
measures. `QueryEngine::toJson` and `fromJson` use a versioned JSON structure;
integer values serialize as decimal strings to preserve full signed 64-bit
precision. A visual editor changes this structure and never constructs raw
SQL or a code expression. `validate` rejects missing fields, invalid
parameter types, unsupported operators/aggregates and missing capabilities.
Each source field has a stable ID, declared type and nullability.

Comparisons involving null return **unknown**; only true rows pass a filter.
`isNull` and `isNotNull` are explicit. `AND`, `OR` and `NOT` use three-valued
logic. A source error is distinct from null and rejects the whole refresh;
it does not silently omit a row. Text comparison is case-sensitive and
lexicographic. Numeric comparisons use numeric values, not display strings.
Types are strict: a text cell is not parsed as a number by the grid adapter.

`CountRows` includes rows whose measure field is null. `CountValues` skips
nulls. `Sum`, `Average`, `Minimum` and `Maximum` skip nulls and return null
when no non-null values exist. Average divides by non-null count. Numeric sums
use an extended precision accumulator and return a `double`; a host requiring
exact currency/decimal arithmetic must provide its own backend. A non-finite
number or an invalid source type rejects the refresh.

Detail mode uses a nonempty `projection` of schema field IDs, no axes and no
measures. Up to four typed `sort` descriptors specify field ID, ascending
direction and whether nulls come last. Ties resolve by stable source row ID.
The result has one row per matched record, columns `field:<fieldId>`, and
one source-row provenance ID per cell. Invalid values or output beyond the
cell/provenance limits reject the whole refresh.

One optional `join` descriptor names a second schema/source revision, left
and right key fields, and inner versus left-outer behavior. Key types must
match exactly; null keys never match. A left join retains an unmatched left
row and supplies nulls for right fields. Joined fields use
`<sourceId>.<fieldId>` in filter, projection, sort and aggregation descriptors.
One-to-many matches produce one joined record per pair. Composite row IDs
use length-prefixed source/row IDs and a distinct unmatched tag, so pairs
cannot alias. `sourceRows` returns joined record IDs;
`sourceReferences(rowId, columnId)` resolves them to `{sourceId,rowId}`
references for host drill-through. The local caps are 100,000 rows per input,
100,000 joined rows and 500,000 materialized field values, plus the
controller's `maxSnapshotValues`. Capability or revision mismatches reject.

## Identity and drill-through

Dimension IDs hash the ordered typed dimension values; they stay stable when
source order changes. Every measure column has its own stable ID. Row and
column grand totals use reserved IDs and appear last. `QueryResult::labels`
provides display labels separately from identity. `sourceRows(rowId, columnId)`
returns the stable source row IDs that contributed to that aggregate. A blank
cross-product cell has an empty provenance list and null result (or zero for
counts). This is a bounded snapshot, not a persistent selection model.
With two or more row dimensions, each proper prefix is a subtotal row with
its own stable typed ID and exact contributing source IDs. `rowParents` maps
each child to its prefix. The view can hide descendants while retaining the
subtotal and its provenance; grand totals remain separate and last.

`GridQuerySource` projects requested fields from `EditableGridModel` through
its stable row/column IDs and sparse `cellValue` API. The host supplies the
typed schema and increments its schema revision when metadata changes. The
adapter reads on the GUI thread and never clones the entire grid. A completed
result can be passed to `queryResultAsGrid` for the grid's visible-window and
geometry APIs. This creates a **read-only presentation snapshot**. The model
rejects data edits, range paste/fill and history replay, and advertises
non-editable item flags. Pivot edits should go through an explicit domain
drill-through operation against source IDs.

## QML controller

`Tk.QueryController` connects a `Tk.EditableGridModel` source to this core.
Set `source`, `schema` and `query`, then call `refresh()`. The schema map is
`{id, revision, fields: [{id, type, nullable}]}` where `type` is `boolean`,
`integer`, `number` or `text`. `query` is the version-1 JSON shape from
`QueryEngine::toJson`; include a unique `id` and the grid's current
`revision` as `sourceRevision`. `refresh()` returns false for an immediately
invalid request and otherwise schedules work. `status` is `idle`, `running`,
`complete`, `rejected` or `cancelled`; `diagnostic` explains rejection.
`completed(requestId)` and `rejected(requestId, reason)` identify results.

On completion, `resultModel` is a read-only `EditableGridModel`. Use
`rowIdAt`/`columnIdAt` on it, then `rowLabels[id]`, `columnLabels[id]`,
`cellValue(rowId, columnId)` and `sourceRows(rowId, columnId)` on the
controller. `resultChanged` fires after the complete snapshot and labels are
ready. A new query, source or schema invalidates the previous result.
`cancel()` discards the pending refresh and prevents late publication.
For a join, set `joinSource` and `joinSchema` before refresh. The controller
collects both grids on the GUI thread in bounded chunks, checks both
revisions through publication, and evaluates immutable snapshots on a worker.

`saveQuery()` returns a versioned JSON document containing the typed query
and field schemas; the host owns file I/O. After recreating sources and
current schemas, call `reopenQuery(document)`. It checks required field IDs
and types against saved metadata, rebases compatible schema/source revisions,
assigns a new request ID and refreshes. Missing or changed required fields
reject reopening without publishing a result. A host can call
`QueryBuilder.loadSpec(controller.query)` to restore the visual draft. The
[joined detail example](../examples/query-transform.qml) exercises this flow.

The controller reads at most 128 source rows per event-loop turn on the
source's GUI thread, then evaluates an immutable snapshot on a worker. The
default `maxSnapshotValues` is 500,000 projected field values (configurable
from 1 to 1,000,000); `QueryLimits` also caps rows, groups, output cells and
provenance. It checks revision and request generation at collection and
publication. This path is for bounded local data; hosts with larger datasets
should provide a remote `QuerySource` and publish through their own adapter.

## Bounds, refresh and providers

The default local scan cap is 100,000 source rows, 10,000 row/column groups,
100,000 output cells, 100,000 stored provenance IDs, 24 predicate levels and
256 predicate nodes. A breach rejects the complete refresh. The executor
checks an atomic cancellation token before and during scanning and discards
partial results. It checks source and schema revisions before, periodically
during and after the scan. Consumers must compare the returned query request
ID before publishing results so an older query cannot overwrite a newer one.

`QuerySource` exposes schema, revision, projected row reads and capabilities.
A provider that supports remote execution advertises `remoteExecution` and
implements `executeRemote`; the engine negotiates before dispatch and checks
revision and output bounds on return. The provider owns its remote query plan,
threading, server cancellation and durable data. GUI-thread sources such as
`GridQuerySource` must be used on their owner thread. Local execution is
synchronous; schedule it on a worker only with a thread-safe immutable source.
Computed fields, incremental refresh, partial drill-through pages, column
hierarchy expansion and host file persistence remain separate provider work.

## QML editor and result view

`FilterBar` edits field/operator/value clauses and emits a typed predicate
object. Invalid drafts are retained and reported beside the controls; callers
must check `valid` before submitting. Integer literals remain strings in the
JSON object, preserving values outside JavaScript's exact-number range. Null
checks have dedicated `isNull` and `isNotNull` operators. Clause groups use
explicit AND or OR nodes.

`QueryBuilder` uses the schema and source revision supplied by
`QueryController`. Its field-axis selector assigns each field to no axis, the
row axis or the column axis. Measure rows pair a field with a supported
aggregate. The generated JSON follows the version-1 `QueryEngine` shape and
contains request ID, schema ID/revision, source revision, predicate,
parameters, row and column dimensions, and measures. `loadSpec()` restores
the editor from a saved JSON spec. Preview requests pass that object to
`QueryController`; it executes asynchronously and only publishes a result
for its active request ID. Hosts can persist the emitted spec separately.

`PivotView` reads stable row and column IDs, labels and aggregate values from
the controller's read-only result model. It displays at most 50 by 24 cells
at once (`maxRows`/`maxColumns` can lower those limits) and provides row and
column paging, so all bounded result
IDs stay reachable without instantiating every cell. Activating a cell emits
the exact row ID, column ID and source row IDs from aggregate provenance.
Editing and clipboard operations are not offered by this view; the host can
map provenance to a separate source-data or detail view.

The controls support flat AND/OR clauses, typed join/projection/sort editing,
row/column field assignment and row-prefix expand/collapse. The core accepts
recursively nested predicate trees; a recursive group editor and remote
provider progress UI remain later work.
