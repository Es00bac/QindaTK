<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-087 asset dependency index slice

Branch `work/wp087-asset-index`, from integration `bce1e0f`.

Implemented:

- `AssetDependencyIndex` stores up to 4,096 stable-ID assets and 16,384
  directed edges, with at most 128 inputs per source. Host ingestion uses
  `beginScan`/`commitScan` generation tokens; an obsolete result cannot
  replace newer edges. Missing targets remain nodes. Queries return direct
  inputs/dependents, transitive reverse impact, missing-edge diagnostics and
  strongly connected cycles.
- `scanQmlFile` reads one project-relative QML file up to 1 MiB, extracts
  local quoted imports and literal file/source references, excludes comments,
  registers discovered files and replaces that source's edges. A file changed
  during the bounded read is rejected.
- `Tk.AssetDependencyView` has a virtualized asset list, search, selected
  inputs/dependents/impact and missing/cycle diagnostics. Open, Relink and
  Rebuild are host requests; Rebuild is disabled on directly diagnosed or
  unavailable selections.

Focused proof:

- Built `tst_asset_dependency_index`, `tst_qml` and `qtk-preview` with the
  configured direct-build limits `-- -j24 -l24`.
- `ctest --test-dir build/wp087 -R '^tst_asset_dependency_index$'` passed.
  Its real fixture project has a directory import, image reference, missing
  file, commented reference and two QML files in a cycle. It also checks
  reverse impact and stale scan rejection.
- Targeted `tst_qml -input tests/qml/tst_asset_dependency_view.qml` passed
  3/3 without QML warnings, including input navigation, impact, missing
  diagnostics, relink request and search.
- Gallery `Main.qml --check` passed. Direct section dumps show compact
  `AssetDependencyView#assetDependencyView 0,0 882x360` and comfortable
  `0,0 878x360`. Both PNGs were opened and inspected:
  [compact](WP-087-asset-dependencies-compact.png),
  [comfortable](WP-087-asset-dependencies-comfortable.png).

Remaining WP-087: dynamic QML expressions and non-QML source formats need
their own extraction providers. Build scheduling/relink transactions belong
to hosts and `ResourceResolver`. Trace ingestion, clock-domain conversion,
`Tk.TraceView`, large-trace cancellation/export and cross-thread span tests
are not in this first slice.
