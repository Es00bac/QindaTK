<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Resource planning table

`Tk.ResourcePlanModel` and `Tk.ResourceTable` implement a bounded allocation
matrix for people, machines, budgets, or other capacity-bearing resources.
This page defines their units, edit behavior, identity, and conflict semantics.

## Model input

The model accepts two ordered lists. Each resource record is
`{ id, name, unit, capacity }`. Each bucket is `{ id, name }`. IDs are unique,
nonempty strings of at most 256 characters and may not contain U+001F. Names
and units are nonempty. Capacity is a finite nonnegative number in the
resource's declared unit. Values are not implicitly converted or rounded.
Bucket IDs identify discrete time periods; the application supplies labels
such as weeks or days in `name`.

The matrix is capped at 100,000 resources, 256 buckets, and 1,000,000
resource/bucket intersections. Allocations are sparse: unset cells read as
zero. Updating the ordered resource or bucket records preserves allocation
values whose two stable IDs still exist. Any structural update clears local
undo history so commands cannot target a changed capacity or removed identity.

## Totals and conflicts

Every allocation and capacity value is in that resource's declared unit. A
cell compares one period's allocation with that resource's per-bucket
capacity. `overAllocated` is true when allocation is strictly greater than
capacity; equality is within capacity. The model accepts over-capacity values
so an application can represent and resolve a planning conflict. Negative,
non-finite, unknown-ID, and read-only requests are rejected without changing
the plan.

`resourceUsage` sums a resource's allocations across all buckets;
`resourceCapacity` multiplies its per-bucket capacity by the bucket count.
`resourceConflict` is true if any period is over capacity. `bucketSummary(id)`
returns one `{ usage, capacity, overAllocated }` record per resource unit in
that bucket. The model never adds unlike units (for example, hours and GPU
hours). `bucketUsage`, `bucketCapacity`, and `bucketConflict` table roles are
the summary for the current row's unit.

## Editing and notifications

`requestAllocation(resourceId, bucketId, value)` is the edit command used by
the QML table and by hosts. `setData()` routes through the same request path.
One accepted edit updates its allocation, resource total, bucket/unit total,
and all affected conflict state before publishing one new revision. A single
transaction ID is emitted by `allocationCommitted`. Local history retains at
most 256 operations; Ctrl+Z/Ctrl+Y call `undo()`/`redo()`. Undo and redo check
the expected current cell value and stable IDs before changing any state.
Programmatic structure updates are silent with respect to allocation command
signals.

## View and stable interaction

`Tk.ResourceTable` virtualizes both axes. It creates only visible row headers,
bucket headers, and cell delegates, with a 4,096 visible-cell ceiling. Its
`activeResourceId` and `activeBucketId` are stable identities; reordering or
inserting source records keeps the selection on the same identities and clamps
the pixel scroll positions. If an active identity is removed, selection moves
to the first valid cell.

The table exposes a Table accessible role and describes the focused cell with
resource, bucket, allocation, capacity, unit, and conflict status. Arrow keys
move by cell; Home and End move to the first or last bucket in the current
resource row; Enter/F2 starts an edit; Escape cancels it. The editor submits
the original resource/bucket IDs even if the view selection changes while the
edit is open. Cells and row headers have stable `objectName`s for tests and
preview inspection.

## Verification fixture

`tests/cpp/tst_resource_plan_model.cpp` checks mixed-unit bucket totals,
over-allocation, one-revision updates, undo/redo, stable IDs, and rejected
requests. `tests/qml/tst_resource_table.qml` checks bounded visible delegates,
keyboard editing, conflict and total updates, undo, accessibility, and stable
selection on source reorder. The runnable example is
[`examples/resource-table/Main.qml`](../examples/resource-table/Main.qml).
