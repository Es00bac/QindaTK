<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 02 — Common controls and scalable data

**Execution underway.** WP-020 through WP-032 are accepted; WP-033 is active
as recorded in the ledger.

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-020"></a>
## WP-020 — Build command controls and adaptive toolbars

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-011](phase-01-services.md#wp-011), [WP-017](phase-01-services.md#wp-017)

**Proposal scope:** [CTL-01 SplitButton](../proposed/02-controls-navigation.md#ctl-01), [CTL-02 MenuButton](../proposed/02-controls-navigation.md#ctl-02), [CTL-03 ToolShelf](../proposed/02-controls-navigation.md#ctl-03), [CTL-05 CommandBar](../proposed/02-controls-navigation.md#ctl-05), [CTL-06 OverflowToolBar](../proposed/02-controls-navigation.md#ctl-06)

**Change area:** SplitButton, MenuButton, ToolShelf, CommandBar and OverflowToolBar.

1. Specify command binding, primary/secondary actions, menu ownership, checked state and tooltip/accessible labels.
2. Implement template-based controls and tool grouping with Theme roles and stable objectNames.
3. Implement overflow measurement and keyboard order without losing command state when items move between toolbar and menu.
4. Wire a shared fixture to the command registry and exercise disabled/context-changing commands, menu cancellation and focus return.
5. Document properties/signals, density sizes and parts; add gallery scenes with inspected compact/comfortable captures.

**Accept when:** All five controls invoke registry commands consistently and remain usable when the toolbar cannot fit.

**Delegate:** Sol supplies the command/focus contract; Luna implements controls in independent files and submits shared catalog/docs patches.

<a id="wp-021"></a>
## WP-021 — Build document and application navigation

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-014](phase-01-services.md#wp-014), [WP-020](phase-02-controls-data.md#wp-020)

**Proposal scope:** [CTL-04 Ribbon](../proposed/02-controls-navigation.md#ctl-04), [CTL-07 DocumentTabs](../proposed/02-controls-navigation.md#ctl-07), [CTL-08 NavigationRail](../proposed/02-controls-navigation.md#ctl-08), [CTL-09 BreadcrumbBar](../proposed/02-controls-navigation.md#ctl-09)

**Change area:** Ribbon, DocumentTabs, NavigationRail and BreadcrumbBar.

1. Define navigation models, stable IDs, contextual ribbon groups and document close/reorder requests.
2. Implement adaptable ribbon groups and rail/breadcrumb overflow with keyboard navigation and visible selection.
3. Connect DocumentTabs to DocumentManager dirty/close state; a close affordance must wait for the document lifecycle result.
4. Persist applicable navigation/workspace state without storing domain content in delegates.
5. Verify long labels, many tabs, failed close/save, RTL, narrow windows and focus after reorder/removal.

**Accept when:** Navigation reflects authoritative document state and stays reachable at constrained widths and translated text sizes.

**Delegate:** Luna owns QML implementations; Sol reviews close/save and workspace integration.

<a id="wp-022"></a>
## WP-022 — Build reusable typed inspectors

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-012](phase-01-services.md#wp-012), [WP-017](phase-01-services.md#wp-017)

**Proposal scope:** [CTL-12 Inspector](../proposed/02-controls-navigation.md#ctl-12), [CTL-13 MixedValueField](../proposed/02-controls-navigation.md#ctl-13), [CTL-14 UnitField](../proposed/02-controls-navigation.md#ctl-14), [CTL-16 VectorField](../proposed/02-controls-navigation.md#ctl-16), [CTL-17 TransformEditor](../proposed/02-controls-navigation.md#ctl-17), [CTL-24 FormGroup](../proposed/02-controls-navigation.md#ctl-24)

**Change area:** Inspector, MixedValueField, UnitField, VectorField, TransformEditor and FormGroup.

1. Map PropertyDescriptor types and validation states to editor delegates, including reset/default, mixed and read-only values.
2. Implement unit conversion at the presentation boundary with declared precision, bounds and keyboard stepping.
3. Implement vector/transform field transactions, axis linking and multi-selection edits without overwriting untouched components.
4. Implement form labeling, help/error associations, focus order and expandable groups using existing layout primitives.
5. Verify live model changes, one-gesture undo, invalid input, cancellation, mixed values and large translated labels.

**Accept when:** All six controls edit through PropertyBinding and share transaction/validation behavior with later spatial and media tools.

**Delegate:** Luna implements the QML editors; Sol supplies unit/transform math and reviews multi-target edit semantics.

<a id="wp-023"></a>
## WP-023 — Add expression, path and token editors

**Implementation lead:** Sol. **Status:** accepted. **Depends on:** [WP-016](phase-01-services.md#wp-016), [WP-019](phase-01-services.md#wp-019), [WP-022](phase-02-controls-data.md#wp-022)

**Proposal scope:** [CTL-15 ExpressionField](../proposed/02-controls-navigation.md#ctl-15), [CTL-20 FilePathField](../proposed/02-controls-navigation.md#ctl-20), [CTL-21 TokenField](../proposed/02-controls-navigation.md#ctl-21)

**Change area:** ExpressionField, FilePathField and TokenField with bounded evaluation/provider interfaces.

1. Define expression grammar, allowed functions/units, evaluation limits and error locations; keep arbitrary code execution out of a numeric expression parser.
2. Implement an actual parser/evaluator or integrate a proven restricted evaluator behind the descriptor contract.
3. Implement path browsing/resolution and multi-token identity, completion, removal and duplicate policies.
4. Connect async completion/path validation with stale-result rejection and transactions for committed values.
5. Verify malformed/expensive expressions, locale numbers, missing paths, keyboard token editing and one-step undo.

**Accept when:** Expression evaluation is bounded and unit-aware; path/token editors handle real provider results and cancellation.

**Delegate:** Luna owns path/token presentation and expression fixture coverage after Sol settles the evaluator.

<a id="wp-024"></a>
## WP-024 — Add typography and calendar value pickers

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-017](phase-01-services.md#wp-017), [WP-022](phase-02-controls-data.md#wp-022)

**Proposal scope:** [CTL-18 FontPicker](../proposed/02-controls-navigation.md#ctl-18), [CTL-19 FontFeaturesEditor](../proposed/02-controls-navigation.md#ctl-19), [CTL-22 DateTimeField](../proposed/02-controls-navigation.md#ctl-22), [CTL-23 CalendarPicker](../proposed/02-controls-navigation.md#ctl-23)

**Change area:** FontPicker, FontFeaturesEditor, DateTimeField and CalendarPicker.

1. Define font family/style/feature availability and date/time zone/calendar values independently of display strings.
2. Implement searchable font enumeration, fallback display and feature controls that reflect actual font capabilities.
3. Implement date/time entry and calendar keyboard navigation with locale-aware parsing and explicit invalid/ambiguous time behavior.
4. Connect all committed edits to property transactions, including feature reset and multi-target mixed values.
5. Verify unavailable fonts, variable/OpenType feature metadata where supported, leap dates, DST boundaries, RTL and IME.

**Accept when:** Pickers expose supported font/date capabilities accurately and preserve typed values across locale changes.

**Delegate:** Luna implements controls against Qt/provider metadata; Sol reviews time-zone and font capability adapters.

<a id="wp-025"></a>
## WP-025 — Add preferences, shortcut editing and find/replace

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-014](phase-01-services.md#wp-014), [WP-022](phase-02-controls-data.md#wp-022), [WP-023](phase-02-controls-data.md#wp-023)

**Proposal scope:** [CTL-10 ShortcutEditor](../proposed/02-controls-navigation.md#ctl-10), [CTL-11 PreferencesDialog](../proposed/02-controls-navigation.md#ctl-11), [CTL-25 FindReplaceBar](../proposed/02-controls-navigation.md#ctl-25)

**Change area:** ShortcutEditor, PreferencesDialog and FindReplaceBar.

1. Define settings category models, search scope, shortcut conflict resolution and replace transaction boundaries.
2. Implement preference editors with validation, reset and explicit immediate-versus-apply behavior.
3. Implement shortcut capture/display, conflict explanation, restore defaults and persisted remapping.
4. Implement find navigation and replace adapters with cancellable search, revision checks and read-only handling.
5. Verify text composition, scoped shortcuts, search completion after document edits, invalid preferences and atomic replace-all undo.

**Accept when:** Settings and shortcuts persist correctly; find/replace works against a real editable model with stale-result protection.

**Delegate:** Luna owns presentation and fixtures; Sol reviews command conflict and replace-all semantics.

<a id="wp-026"></a>
## WP-026 — Add history, notifications and task feedback

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-012](phase-01-services.md#wp-012), [WP-015](phase-01-services.md#wp-015), [WP-020](phase-02-controls-data.md#wp-020)

**Proposal scope:** [APP-17 HistoryView](../proposed/01-application-services.md#app-17), [CTL-26 NotificationCenter](../proposed/02-controls-navigation.md#ctl-26), [CTL-27 Toast](../proposed/02-controls-navigation.md#ctl-27), [CTL-28 TaskCenter](../proposed/02-controls-navigation.md#ctl-28)

**Change area:** HistoryView, NotificationCenter, Toast and TaskCenter.

1. Define stable history/job/notification IDs, severity, actions, retention, progress and dismiss behavior.
2. Implement history navigation against UndoService and preserve the difference between current undo history and persisted document versions.
3. Implement accessible notifications and toast timing/persistence using Theme roles and explicit actionable focus behavior.
4. Implement task progress, cancellation, retry and detail views over JobModel without giving delegates job ownership.
5. Verify concurrent jobs, failed retries, document closure, keyboard announcement/action paths and bounded history/notification growth.

**Accept when:** Feedback reflects actual service state and gives useful recovery actions; dismissing a view does not cancel unrelated work.

**Delegate:** Luna owns these views once service roles are stable; Sol reviews retry/cancel actions.

<a id="wp-027"></a>
## WP-027 — Implement virtual lists, trees and asset grids

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-015](phase-01-services.md#wp-015), [WP-016](phase-01-services.md#wp-016), [WP-017](phase-01-services.md#wp-017)

**Proposal scope:** [DAT-01 VirtualList](../proposed/03-data-views.md#dat-01), [DAT-02 VirtualTree](../proposed/03-data-views.md#dat-02), [DAT-03 AssetGrid](../proposed/03-data-views.md#dat-03)

**Change area:** VirtualList, VirtualTree, AssetGrid and standard C++ model/provider adapters.

1. Define stable identity, role access, lazy children, expansion, sorting/filtering, keyboard navigation and selection adapters.
2. Implement bounded visible delegates using supported Qt primitives or an equivalent measured recycler; avoid full data copies into JavaScript arrays.
3. Implement lazy tree loading and thumbnail requests with cancellation on recycling and view destruction.
4. Implement reorder/drop operations through domain commands and maintain selection/current identity across model resets.
5. Run 100,000-node/asset fixtures; measure delegate counts, cache/decoder concurrency, navigation latency and stale-result rejection.

**Accept when:** Collections remain bounded and preserve ID-based selection through lazy loading, filtering and recycling.

**Delegate:** Luna implements delegates and generated datasets; Sol owns model/recycling semantics.

<a id="wp-028"></a>
## WP-028 — Implement editable two-dimensional grids

**Implementation lead:** Sol. **Status:** accepted. **Depends on:** [WP-016](phase-01-services.md#wp-016), [WP-022](phase-02-controls-data.md#wp-022), [WP-027](phase-02-controls-data.md#wp-027)

**Proposal scope:** [DAT-04 EditableGrid](../proposed/03-data-views.md#dat-04), [DAT-05 HeaderView](../proposed/03-data-views.md#dat-05), [DAT-06 RangeSelectionOverlay](../proposed/03-data-views.md#dat-06)

**Change area:** EditableGrid, HeaderView, RangeSelectionOverlay and cell/range edit adapters.

1. Define row/column identity, sparse/lazy cells, editor delegates, frozen regions, headers, range selection and reset/reindex behavior.
2. Implement both-axis virtualization and synchronized headers without creating every offscreen column in a visible row.
3. Implement cell commit/cancel, keyboard movement, range paste/fill, resize/reorder and validation through one transaction.
4. Expose selection/value/action semantics and a usable accessible alternative for large grids.
5. Exercise 100,000 rows by 100 columns, variable dimensions, model insertion/removal, invalid paste and undo; record delegate, memory and latency evidence.

**Accept when:** A real lazy model supports reversible cell/range editing with bounded delegates and synchronized headers.

**Delegate:** Luna implements cell/header visuals and focused fixture adapters; Sol owns virtualization and range algorithms.

**Astra trigger:** Astra may resolve a measured two-axis virtualization or range-identity failure after the smallest reproducer and alternatives are documented.

<a id="wp-029"></a>
## WP-029 — Implement filters, query building and pivots

**Implementation lead:** Sol. **Status:** accepted. **Depends on:** [WP-027](phase-02-controls-data.md#wp-027), [WP-028](phase-02-controls-data.md#wp-028)

**Proposal scope:** [DAT-07 FilterBar](../proposed/03-data-views.md#dat-07), [DAT-08 QueryBuilder](../proposed/03-data-views.md#dat-08), [DAT-09 PivotView](../proposed/03-data-views.md#dat-09)

**Change area:** FilterBar, QueryBuilder, PivotView and typed query/aggregation adapters.

1. Define a typed query AST, field metadata, parameter binding, null/error semantics and backend capability negotiation.
2. Implement visual filter/query editing and serialization without concatenating executable queries from display strings.
3. Implement actual local aggregation/pivot behavior and a provider interface for remote or larger datasets.
4. Integrate grouping, drill-through, refresh/cancel and stable row/column identities with the editable grid.
5. Verify filters against a known dataset, empty/null results, changed schemas, stale queries and numerical aggregation fixtures.

**Accept when:** Query and pivot output is computed correctly and retains provenance to source rows and declared aggregation semantics.

**Delegate:** Luna owns AST editor controls and known-answer fixtures; Sol owns execution, query adapters and aggregation.

<a id="wp-030"></a>
## WP-030 — Build the general chart framework

**Implementation lead:** Sol. **Status:** accepted. **Depends on:** [WP-015](phase-01-services.md#wp-015), [WP-017](phase-01-services.md#wp-017), [WP-027](phase-02-controls-data.md#wp-027)

**Proposal scope:** [DAT-10 ChartView](../proposed/03-data-views.md#dat-10), [DAT-11 ChartAxis](../proposed/03-data-views.md#dat-11), [DAT-12 ChartSeries](../proposed/03-data-views.md#dat-12), [DAT-13 ChartLegend](../proposed/03-data-views.md#dat-13), [DAT-15 Histogram](../proposed/03-data-views.md#dat-15)

**Change area:** ChartView, ChartAxis, ChartSeries, ChartLegend, Histogram and plot/export adapters.

**Additional obligations:** [VIS-01](supporting-scope.md#vis-01), [VIS-02](supporting-scope.md#vis-02), [VIS-03](supporting-scope.md#vis-03).

1. Define explicit coordinates, categories, units, gaps, series IDs, selection, ranges and linear/log/date axis semantics.
2. Implement line/step/area/stacked area, grouped/stacked bar/waterfall and scatter/bubble/vector series with truthful baseline and missing-value behavior.
3. Implement histogram bin contracts, axis ticks, legend interaction, hit testing, decimation and bounded data updates.
4. Add accessible tabular equivalents and image/vector export through the format/job boundary where supported.
5. Verify known-coordinate fixtures, empty/non-finite/log-invalid values, stacking totals, hit identity and GPU/software rendering.

**Accept when:** The framework renders actual arbitrary datasets with defined axes/selection/export; current telemetry Graph remains compatible.

**Delegate:** Luna owns legends, chart controls and fixture datasets; Sol owns axis/series/render algorithms.

<a id="wp-031"></a>
## WP-031 — Complete analytical chart and scheduling families

**Implementation lead:** Sol. **Status:** accepted; [integrated evidence](evidence/WP-031.md). **Depends on:** [WP-024](phase-02-controls-data.md#wp-024), [WP-028](phase-02-controls-data.md#wp-028), [WP-029](phase-02-controls-data.md#wp-029), [WP-030](phase-02-controls-data.md#wp-030)

**Proposal scope:** [DAT-14 Heatmap](../proposed/03-data-views.md#dat-14), [DAT-16 DistributionPlot](../proposed/03-data-views.md#dat-16), [DAT-17 GanttView](../proposed/03-data-views.md#dat-17)

**Change area:** Heatmap, DistributionPlot, GanttView and additional chart series/layout adapters.

**Additional obligations:** [ENG-23](supporting-scope.md#eng-23), [VIS-04](supporting-scope.md#vis-04), [VIS-05](supporting-scope.md#vis-05), [VIS-06](supporting-scope.md#vis-06), [VIS-07](supporting-scope.md#vis-07), [VIS-08](supporting-scope.md#vis-08), [VIS-09](supporting-scope.md#vis-09), [VIS-10](supporting-scope.md#vis-10).

1. Implement heatmaps/matrices and density/box/violin displays with declared estimators, binning, interpolation and missing-data behavior.
2. Add pie/donut, treemap/sunburst, polar/radar, OHLC/candlestick and bullet/gauge/target displays with correct coordinate and total semantics.
3. Implement error bars/bands and preserve whether intervals are confidence, credible or another domain-defined quantity.
4. Implement Gantt intervals, dependency/working-calendar adapters, allocation overlays and reversible rescheduling; share DateTime and grid contracts.
5. Validate statistical known answers, hierarchy totals, axis edge cases, calendar/DST boundaries and accessible alternatives. Connect network, map and 3D families in their later owning packages.

**Accept when:** All assigned visualization families have renderer/provider fixtures and the three named views have real data behavior.

**Delegate:** Luna builds small series delegates and fixtures from fixed math contracts; Sol owns statistics, coordinates and scheduling.

**Astra trigger:** Escalate only an unresolved numerical estimator or large hierarchical layout problem with accuracy/performance acceptance data.

<a id="wp-032"></a>
## WP-032 — Build event, diff and resource views

**Implementation lead:** Luna. **Status:** accepted. **Depends on:** [WP-015](phase-01-services.md#wp-015), [WP-024](phase-02-controls-data.md#wp-024), [WP-027](phase-02-controls-data.md#wp-027), [WP-028](phase-02-controls-data.md#wp-028)

**Proposal scope:** [DAT-18 EventLogView](../proposed/03-data-views.md#dat-18), [DAT-19 DiffView](../proposed/03-data-views.md#dat-19), [DAT-20 ResourceTable](../proposed/03-data-views.md#dat-20)

**Change area:** EventLogView, DiffView and ResourceTable.

1. Define event sequence/timestamps, stable diff regions and resource identity/availability/allocation roles.
2. Implement bounded event ingestion, filters, follow-tail/pause and export without keeping unbounded GUI history.
3. Implement side-by-side/inline diff presentation through a real diff provider, navigation and explicit conflict/merge actions.
4. Implement resource grids with allocation summaries and editable properties backed by model commands.
5. Verify burst ingestion, repeated text/empty diffs, changed resources, scroll/focus preservation and accessible row/region navigation.

**Accept when:** The three displays consume real models, stay bounded and correctly route any edit or merge requests.

**Delegate:** Luna owns views; Sol selects/reviews diff and resource scheduling providers before assigning adapters.

<a id="wp-033"></a>
## WP-033 — Prove the shared application framework

**Implementation lead:** Sol. **Status:** active; workbench lifecycle flows are integrated, while the general object collection adapter and native accessibility-client qualification remain open ([integration evidence](evidence/WP-033.md)). **Depends on:** [WP-013](phase-01-services.md#wp-013), [WP-014](phase-01-services.md#wp-014), [WP-019](phase-01-services.md#wp-019), [WP-020](phase-02-controls-data.md#wp-020), [WP-022](phase-02-controls-data.md#wp-022), [WP-026](phase-02-controls-data.md#wp-026), [WP-028](phase-02-controls-data.md#wp-028)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** A new reference workbench example, service/model fixtures and integration evidence.

1. Compose two documents, shared command chrome, properties, collections, an editable grid and actual native-format persistence.
2. Exercise the same edit through inspector, keyboard, context menu and accessible actions; inspect one-transaction undo and cancellation.
3. Copy/paste between documents, filter/sort while selecting, run async jobs and close a document before a provider returns.
4. Inject a failed save, restore from recovery, switch workspace and reopen the saved document with equivalent content.
5. Fix integration defects before multiplying domain surfaces; publish a runnable example and exact reproduction instructions.

**Accept when:** The common infrastructure works together in an actual app with error/recovery paths, not only isolated component tests.

**Delegate:** Luna authors repeatable scenarios and documentation while Sol integrates and diagnoses cross-service defects.
