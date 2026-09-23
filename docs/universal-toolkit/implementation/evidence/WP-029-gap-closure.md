<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-029 review gap closure

Isolated branch from WP-028 closure `7c2b00c`, 2026-09-23. Linux offscreen
software renderer, Qt 6.11.1, GCC 15.3.0, Debug/Ninja. `portageq envvar
MAKEOPTS` returned `-j24 -l24`; builds used plain `cmake --build` without
overrides. This is lane evidence, not integrated WP-029 acceptance.

## Behavior and known answers

- Version-1 `QuerySpec` accepts optional detail projection, up to four typed
  sort descriptors and one inner/left join. Existing aggregate JSON and
  aggregate results remain readable. The visual `QueryBuilder` exposes all
  three operations and roundtrips its supported draft. The local join scans
  two revision-guarded snapshots, checks exact key types, never matches null
  keys, and rejects an over-limit one-to-many expansion. Joined record IDs
  use length-prefixed source/row IDs; `sourceReferences` resolves provenance
  to the left and right source rows. An unmatched left row has one left
  reference and null right values.
- The five-row sales/two-row catalog fixture produces eight left-joined
  detail records: six A matches and two unmatched B records. Sorting by
  category places Supplies before Tools and nulls last. An inner join yields
  six records; a seven-record output cap rejects the full left join. Typed
  joined-field filters, capability rejection, revision mismatch, JSON
  roundtrip and stable IDs after right-source reorder are checked.
- Pivoting region then product creates prefix subtotal rows with stable IDs.
  East sum is **30**, East/A sum **30**, grand sum **50**; East provenance is
  `[r1, r2, r3]`. An interleaved source still publishes each parent directly
  before its descendants. `PivotView` can collapse a prefix while retaining
  its subtotal and exact source IDs. The visible cell page remains at most
  50 × 24.
- `saveQuery()` returns a versioned document for host storage. Reopening
  against a compatible new schema/source revision regenerates a request ID
  and refreshes; changing a required field type rejects and leaves no result.
  A joined-source mutation during pending refresh rejects publication.

## Focused checks

- Isolated full `cmake --build build/wp029-gap-closure`: exit 0.
- `ctest --test-dir build/wp029-gap-closure -R '^tst_query_core$'`: **1/1**
  passed after the hierarchy fixture correction and depth-first ordering.
- Focused `tests/qml/tst_query_controller.qml`: **7/7** passed; focused
  `tests/qml/tst_query_pivot.qml`: **9/9** passed.
- `qmllint` over `QueryBuilder`, `QueryTransformPanel`, `PivotView` and the
  new example with current module import/resource paths: exit 0. Existing
  PivotView delegate unqualified-access warnings remain; no new panel or
  controller API warnings were reported.
- `qtk-preview --check`, `--dump` and `--grab` completed for the joined
  detail and hierarchy examples at compact and comfortable density.

## Inspected pixels and geometry

The [joined compact](WP-029-join-compact.png),
[joined comfortable](WP-029-join-comfortable.png),
[hierarchy compact](WP-029-hierarchy-compact.png) and
[hierarchy comfortable](WP-029-hierarchy-comfortable.png) captures were
opened and inspected. The join panel shows the key pair, left-join option,
projection and sort; detail cells show Office, Supplies, Tools and a null
unmatched category in sorted order. The hierarchy displays contiguous
North/Q1/Q2, South/Q1/Q2 and West/Q1/Q2 beneath visible expand buttons;
comfortable density scrolls its taller rows within the bounded viewport.

Representative dump lines:

```text
compact join:       QueryBuilder#joinedQueryBuilder  8,44 1104x200
compact join:       PivotView#joinedDetailView  8,252 1104x280
compact join:       AbstractButton#pivotCell_5_2  365,0 96x24  "—"
comfortable join:   QueryBuilder#joinedQueryBuilder  10,48 1100x234
comfortable join:   PivotView#joinedDetailView  10,292 1100x238
comfortable join:   AbstractButton#pivotCell_5_2  437,0 115x29  "—"
compact hierarchy:  QueryBuilder#salesQueryBuilder  8,44 1104x299
compact hierarchy:  PivotView#salesPivotView  8,351 1104x291
comfortable hierarchy: QueryBuilder#salesQueryBuilder  10,48 1100x349
comfortable hierarchy: PivotView#salesPivotView  10,407 1100x233
```

The host still owns file storage and larger/remote query providers. This
lane does not add exact decimal aggregation, computed fields, column-axis
hierarchy expansion or partial drill-through pages. Sol owns shared dual-Qt
integration and WP-029 acceptance.

## Shared integration checkpoint

The Qt 6.11.1 shared CMake reconfigure and plain build passed after merging
this lane with WP-026 review fixes. Root captured and opened new
[compact dark](../../../screenshots/wp029-integration-compact.png) and
[comfortable light](../../../screenshots/wp029-integration-comfortable.png)
screenshots from the integrated tree. Their dumps show
`QueryBuilder#joinedQueryBuilder 8,44 1104x200` and
`PivotView#joinedDetailView 8,252 1104x280` in compact density; the
comfortable sizes are `10,48 1100x234` and `10,292 1100x238`. In both
captures, the unmatched category remains visibly null and joined row labels
are readable. Consolidated regression and Qt 6.8 gates are next.
