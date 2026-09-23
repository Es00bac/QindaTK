<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-029 Query visual layer evidence

The QML visual layer adds typed filter composition, saved-query editing, and a
read-only paged pivot view over `Tk.QueryController`. Controls are theme-driven,
accessible, and expose stable object names. Filter clauses and grouping,
column, and measure choices bind to the versioned query specification; they do
not construct SQL.

## Verification

- `tst_query_pivot.qml`: 7/7 cases pass, including typed integer filters,
  invalid-value retention, zero-literal round trip, unsupported nested-AST
  rejection without changing the draft, unique measure IDs after remove/add,
  real controller completion, second-page values and provenance, and stale
  request rejection.
- The pivot page size is capped at 50 rows × 24 columns (1,200 cells). The
  current controller provenance API returns source IDs as a list; the view
  requests them only for visible cells. Core query limits cap total provenance
  IDs at 100,000.
- `qtk-preview examples/query-pivot/Main.qml --check` succeeds.
- Compact and comfortable `--dump`/`--grab` previews were inspected. Compact:
  `QueryBuilder#salesQueryBuilder 8,44 1104x250`,
  `PivotView#salesPivotView 8,302 1104x450`, cells `96x24`. Comfortable:
  `QueryBuilder#salesQueryBuilder 10,48 1100x293`,
  `PivotView#salesPivotView 10,351 1100x399`, cells `115x29`.
- Captures: `WP-029-compact.png` and `WP-029-comfortable.png` in this folder.

The fixture and example use a small in-memory typed grid. They demonstrate the
current QML integration contract; they do not claim an external database or
streaming provider implementation.
