<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Review batch acceptance — 2026-09-23

This record closes WP-019, WP-021, WP-023 through WP-030, WP-032 and WP-047
against integrated source revision `720b732`. WP-020 and WP-022 were accepted
earlier. WP-031 remains active. WP-040 and later application packages are
outside this acceptance batch.

## Integrated gates

- `cmake --build build/integration` on Qt 6.11.1 and `cmake --build build/qt68`
  on Qt 6.8.3 completed with the machine's configured build parallelism
  unchanged (`portageq envvar MAKEOPTS` reported `-j24 -l24`).
- The full shared `ctest` suite passed **89/89 on Qt 6.11.1** and **89/89 on
  Qt 6.8.3**, using offscreen Qt Quick software rendering. Qt 6.11's
  `all_qmllint` target exited zero. The repository still emits nonfatal
  unqualified-access lint warnings.
- Both installed stages were refreshed from these builds. The external
  `platform_services`, `chart`, `data_views` and `wp032` consumers compiled
  where source changed and ran with exit zero against the installed headers,
  library and QML module on **both** Qt versions. The platform consumer also
  calls the newly exported font coverage API and instantiates the font
  features/date controls; the chart consumer checks linked axes and annotation
  CSV; the data consumer saves/reopens a typed query.
- `docs/catalog.json` was regenerated after the public control/API changes.
  Compact and comfortable screenshots were opened for the changed Navigation,
  Preferences, typography/date, asset-grid, editable-grid, query/pivot and
  chart views. Their package records link the images and geometry dumps.

An initial Qt 6.8 repository run loaded the *previous installed stage* because
its command placed `build/qt68-stage/lib64` ahead of the current build library
in `LD_LIBRARY_PATH`. The 100,000-asset fixture then reproduced the old
offscreen subscription leak. `ldd` confirmed that library path. The invalid
run was stopped; with only the Qt 6.8 SDK library path, `ldd` resolved
`build/qt68/src/libqindatk.so.0`, the focused scale test passed, and the full
**89/89** run above passed. Installed-consumer checks intentionally use the
refreshed stage.

## Package disposition

| Package | Final evidence and boundary |
| --- | --- |
| [WP-019](WP-019.md) | Real import/export job, preflight, guarded write and dismissal paths passed. A `.qtk` suffix nominates a provider; byte validation during preflight determines importability. Vendor formats remain host adapters. |
| [WP-021](WP-021.md) | Route confirmation/reconciliation and asynchronous sibling results passed focused **9/9** QML checks. Compact/comfortable route captures show the provider chevron. Host owns persisted route state. |
| [WP-023](WP-023.md) | Bounded expression and path/token provider/cancellation checks plus installed consumer passed. `recentPaths` records accepted locations; a dedicated recent-path chooser remains a later UI extension. |
| [WP-024](WP-024.md) | Font catalog C++ **1/1** and typography/date QML **14/14** focused checks passed. Face cmap checks report missing sample code points; shaping, fallback and feature eligibility remain outside this service. |
| [WP-025](WP-025.md) | Existing **11/11** and new lifecycle **5/5** QML checks passed without QML runtime warnings. Escape/close discard staging; Return validates; a hidden invalid editor is revealed, focused and visibly sized. |
| [WP-026](WP-026.md) | Real concurrent JobModel lifecycle, authoritative committed result and accessible Toast announcement passed. Retry revalidation and execution lineage remain host responsibilities; TaskCenter does not own jobs. |
| [WP-027](WP-027.md) | The 100,000-asset fixture passed on both Qt versions. Offscreen retained GridView delegates release subscriptions; rapid-scroll throughput remains unmeasured. |
| [WP-028](WP-028.md) | Sparse 100,000 × 100 grid, visual navigation/editing and real desktop AT-SPI cell action passed; compact/comfortable captures were inspected. |
| [WP-029](WP-029.md) | Typed joins, projection/sort, pivot hierarchy, host save/reopen and staged data consumer passed; integrated query/pivot captures were inspected. |
| [WP-030](WP-030.md) | Linked axes, annotation model/CSV, histogram source-domain selection, installed chart consumer and dual-Qt captures passed. |
| [WP-032](WP-032.md) | Event, diff and resource providers/views plus the dedicated installed consumer passed on both Qt versions. |
| [WP-047](WP-047.md) | Transport state, exact sample positions, cadence and installed TransportBar passed. A physical audio engine is a later package. |

These boundaries limit the claims for this batch. The complete application
proofs and remaining work packages in the [acceptance plan](../acceptance.md)
are still required before QindaTK can claim the full universal scope.
