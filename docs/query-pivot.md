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

## Identity and drill-through

Dimension IDs hash the ordered typed dimension values; they stay stable when
source order changes. Every measure column has its own stable ID. Row and
column grand totals use reserved IDs and appear last. `QueryResult::labels`
provides display labels separately from identity. `sourceRows(rowId, columnId)`
returns the stable source row IDs that contributed to that aggregate. A blank
cross-product cell has an empty provenance list and null result (or zero for
counts). This is a bounded snapshot, not a persistent selection model.

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
No joins, sorting, computed fields, incremental refresh, partial drill-through
pages, hierarchy expansion or persistence layer are included in this core
package. Those operations require explicit provider contracts and UI work.

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

The controls currently support flat AND/OR clauses and one level of row/column
field assignment. The core accepts recursively nested predicate trees; a
recursive group editor, nested field hierarchies, incremental refresh and
remote-provider progress UI remain later work.
