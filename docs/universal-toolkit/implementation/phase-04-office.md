<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 04 — Documents and Office workflows

**Execution underway in isolated lanes.** WP-050 and WP-055 have unintegrated
Opus worktrees; their dependencies and acceptance gates remain open. Other
packages are planned.

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-050"></a>
## WP-050 — Implement rich text and paginated document editing

**Implementation lead:** Sol. **Status:** active (isolated Opus lane; not integrated). **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-024](phase-02-controls-data.md#wp-024), [WP-025](phase-02-controls-data.md#wp-025), [WP-033](phase-02-controls-data.md#wp-033), [WP-040](phase-03-authoring.md#wp-040)

**Proposal scope:** [OFF-01 RichTextEditor](../proposed/05-documents-office.md#off-01), [OFF-02 RichDocumentView](../proposed/05-documents-office.md#off-02), [OFF-03 FormatBar](../proposed/05-documents-office.md#off-03), [OFF-05 PageNavigator](../proposed/05-documents-office.md#off-05)

**Change area:** RichTextEditor, RichDocumentView, FormatBar, PageNavigator and structured text/layout backend.

**Additional obligations:** [ENG-04](supporting-scope.md#eng-04).

1. Define paragraphs/runs/objects, stable positions, IME preedit, selection, styles, pagination and text-to-page coordinate mapping.
2. Prototype the hardest fixture: mixed scripts, tables and anchored content crossing page breaks. Choose the actual layout/serialization backend from that evidence.
3. Implement editing, pagination and page navigation with incremental invalidation and actual text shaping/selection behavior.
4. Implement formatting through transactions and synchronize editable and paginated views; persist a native structured document.
5. Verify 500-page fixtures, bidi/IME, selection across pages, anchored objects, cancel/undo and save/reopen equivalence; retain measurable pagination locality.

**Accept when:** The document slice edits real styled multi-page content with valid cursor mapping and persistence.

**Delegate:** Luna owns format/page controls and fixture documents after the backend contract stabilizes; Sol owns document/layout integration.

**Astra trigger:** Use Astra for a hard pagination, text-position mapping or shaping/anchoring problem demonstrated by a minimized fixture.

<a id="wp-051"></a>
## WP-051 — Add publishing structure, styles and tables

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-042](phase-03-authoring.md#wp-042), [WP-050](phase-04-office.md#wp-050)

**Proposal scope:** [OFF-04 StyleManager](../proposed/05-documents-office.md#off-04), [OFF-06 PageSetupDialog](../proposed/05-documents-office.md#off-06), [OFF-07 TextFrameEditor](../proposed/05-documents-office.md#off-07), [OFF-08 TableEditor](../proposed/05-documents-office.md#off-08)

**Change area:** StyleManager, PageSetupDialog, TextFrameEditor and TableEditor.

1. Define style inheritance/overrides, page geometry, linked frames and structured table/merged-cell constraints.
2. Implement style application/update and page setup as reversible document operations with incremental repagination.
3. Implement text-frame geometry/flow and actual table insertion, row/column edits, merges and resizing.
4. Connect inspector, canvas and text selection without ambiguous edit ownership; expose structural navigation to keyboard/assistive users.
5. Verify style cycles/overrides, overflow frames, split tables across pages, merged-cell edits and reopen/print-layout consistency.

**Accept when:** Publishing operations modify the structured document and preserve flow/style/table semantics through undo and persistence.

**Delegate:** Luna builds style/page/table panels; Sol owns structured text and pagination operations.

<a id="wp-052"></a>
## WP-052 — Add equations, references and document review

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-025](phase-02-controls-data.md#wp-025), [WP-051](phase-04-office.md#wp-051)

**Proposal scope:** [OFF-09 EquationEditor](../proposed/05-documents-office.md#off-09), [OFF-10 ReferenceManager](../proposed/05-documents-office.md#off-10), [OFF-11 ReviewPane](../proposed/05-documents-office.md#off-11)

**Change area:** EquationEditor, ReferenceManager and ReviewPane with structured semantic adapters.

1. Define a supported equation AST, citation/reference IDs, generated references and tracked edit/comment ranges.
2. Integrate real equation layout/editing and reference formatting/renumbering with typed errors for unsupported constructs.
3. Implement comment/review navigation and accept/reject operations that update persistent tracked changes.
4. Handle edits that move/delete referenced or annotated content while preserving recoverable identity.
5. Verify known equation output, reference renumbering, overlapping review ranges, accept/reject undo and document roundtrip.

**Accept when:** Equation/reference/review content participates in actual document structure, layout and reversible edits.

**Delegate:** Luna owns review/reference presentation and semantic fixture documents; Sol owns range reconciliation and layout providers.

<a id="wp-053"></a>
## WP-053 — Implement printing and publishing preflight

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-051](phase-04-office.md#wp-051)

**Proposal scope:** [OFF-12 PrintPreview](../proposed/05-documents-office.md#off-12), [OFF-13 PreflightPanel](../proposed/05-documents-office.md#off-13), [PLT-04 PrintAdapter](../proposed/08-platform-integrations.md#plt-04)

**Change area:** PrintPreview, PreflightPanel, PrintAdapter and page-output providers.

1. Define page ranges, paper/orientation/scaling, printer capabilities, output color and structured preflight issues.
2. Implement preview using the same page layout/output description as export/printing, with bounded page caches.
3. Implement actual printer/file-output adapters for the selected platform and expose unsupported device options explicitly.
4. Implement checks for missing fonts/assets, overflow, resolution and document-specific constraints with actionable links.
5. Compare preview/export pages against fixtures and exercise printer cancellation/failure, unavailable fonts and out-of-range page selections.

**Accept when:** Print preview, preflight and actual output share tested pagination/geometry and recover from device failures.

**Delegate:** Luna implements preview/preflight controls and capture comparisons; Sol owns native print/output integration.

<a id="wp-054"></a>
## WP-054 — Implement PDF viewing and annotation

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-040](phase-03-authoring.md#wp-040)

**Proposal scope:** [OFF-24 PdfView](../proposed/05-documents-office.md#off-24), [OFF-25 PdfAnnotationTools](../proposed/05-documents-office.md#off-25)

**Change area:** PdfView, PdfAnnotationTools and page/text/annotation backend.

**Additional obligations:** [ENG-07](supporting-scope.md#eng-07).

1. Choose a backend using fixtures for page rendering, text selection/search, annotation editing and annotation export/reopen.
2. Implement bounded page tiles, navigation, coordinate mapping and text/link accessibility.
3. Implement supported annotation types as semantic commands with persistent IDs and real serialization.
4. Publish operation-specific PDF capabilities; distinguish viewing/annotation from text reflow, OCR, signatures or destructive redaction when those are not implemented.
5. Verify rotated/mixed-size pages, multilingual text, malformed/encrypted inputs where supported, annotation undo and output roundtrip.

**Accept when:** The two proposed PDF controls work against a real document backend with explicit, tested annotation capabilities.

**Delegate:** Luna owns toolbars, page fixtures and annotation visuals; Sol owns backend and format fidelity.

<a id="wp-055"></a>
## WP-055 — Implement a workbook and spreadsheet editing stack

**Implementation lead:** Sol. **Status:** active (isolated Opus lane; full scope unfinished and not integrated). **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-028](phase-02-controls-data.md#wp-028)

**Proposal scope:** [OFF-14 SheetView](../proposed/05-documents-office.md#off-14), [OFF-15 FormulaBar](../proposed/05-documents-office.md#off-15), [OFF-16 WorkbookTabs](../proposed/05-documents-office.md#off-16), [OFF-17 ConditionalFormatEditor](../proposed/05-documents-office.md#off-17), [OFF-18 DataValidationEditor](../proposed/05-documents-office.md#off-18)

**Change area:** SheetView, FormulaBar, WorkbookTabs, ConditionalFormatEditor, DataValidationEditor and workbook/calc engine.

**Additional obligations:** [ENG-05](supporting-scope.md#eng-05).

1. Define cells/ranges/sheets, typed values/errors, relative/absolute references, dependency graph and formula grammar/function capability list.
2. Implement or integrate actual parsing/recalculation, cycle/error propagation, reference adjustment and native workbook persistence.
3. Implement formula entry, fill/paste, sheet operations, conditional formatting and validation through grid/transaction contracts.
4. Connect lazy evaluation/render updates and named/imported data providers; large workbooks must not become QML object graphs.
5. Verify known formula results, cross-sheet references, row/column insertion, circular dependencies, invalid paste, undo and reopen equivalence; test declared interchange operations separately.

**Accept when:** The spreadsheet slice computes formulas and preserves reference/range semantics through structural edits and persistence.

**Delegate:** Luna owns sheet/formula/formatting UI and workbook fixtures; Sol owns calculation, dependency and reference algorithms.

**Astra trigger:** Escalate a specific incremental-recalculation, reference-rewrite or formula precision defect after known-answer fixtures isolate it.

<a id="wp-056"></a>
## WP-056 — Implement slide authoring and presentation

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-018](phase-01-services.md#wp-018), [WP-042](phase-03-authoring.md#wp-042), [WP-043](phase-03-authoring.md#wp-043), [WP-049](phase-03-authoring.md#wp-049), [WP-050](phase-04-office.md#wp-050)

**Proposal scope:** [OFF-19 SlideCanvas](../proposed/05-documents-office.md#off-19), [OFF-20 SlideSorter](../proposed/05-documents-office.md#off-20), [OFF-21 SlideMasterEditor](../proposed/05-documents-office.md#off-21), [OFF-22 AnimationPane](../proposed/05-documents-office.md#off-22), [OFF-23 PresenterView](../proposed/05-documents-office.md#off-23)

**Change area:** SlideCanvas, SlideSorter, SlideMasterEditor, AnimationPane, PresenterView and presentation backend.

**Additional obligations:** [ENG-06](supporting-scope.md#eng-06).

1. Define slides, inherited masters/layouts, notes, transitions/animations and timeline evaluation in a persistent object model.
2. Implement object editing, slide reorder/duplicate and master inheritance/override behavior using the canvas/command system.
3. Implement animation editing/evaluation and deterministic presentation playback using exact time and curve contracts.
4. Implement audience and presenter windows with notes, navigation, clock and reconnect/monitor-removal behavior.
5. Verify master changes, reordered/duplicated slides, sampled animation frames, separate audience/notes output, undo and save/reopen/export.

**Accept when:** The slide slice authors persistent content and presents deterministic output in real separate views.

**Delegate:** Luna owns sorter/master/animation/presenter controls; Sol owns inheritance, evaluator and multi-window playback.

<a id="wp-057"></a>
## WP-057 — Implement notebooks and structured blocks

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-016](phase-01-services.md#wp-016), [WP-027](phase-02-controls-data.md#wp-027), [WP-050](phase-04-office.md#wp-050)

**Proposal scope:** [OFF-26 NotebookView](../proposed/05-documents-office.md#off-26), [OFF-27 BlockEditor](../proposed/05-documents-office.md#off-27)

**Change area:** NotebookView, BlockEditor and hierarchical block/notebook document adapter.

1. Define notebooks/pages/blocks, stable identity, supported embedded block types and cross-block cursor/selection rules.
2. Implement actual block insertion, reorder, nesting, split/merge and rich-text/media embedding with serialization.
3. Implement lazy page/block presentation and keyboard navigation without losing edit state on delegate reuse.
4. Integrate search, clipboard, links and recovery using common services.
5. Verify cross-block copy/paste, deletion/reorder undo, embedded missing assets and reopen consistency for large notebooks.

**Accept when:** Notes/blocks are an editable persistent document model with reliable text and structural operations.

**Delegate:** Luna owns block/page delegates and fixtures; Sol owns document structure and cursor reconciliation.

<a id="wp-058"></a>
## WP-058 — Implement mail, calendars and task workflows

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-024](phase-02-controls-data.md#wp-024), [WP-027](phase-02-controls-data.md#wp-027), [WP-050](phase-04-office.md#wp-050)

**Proposal scope:** [OFF-28 MailView](../proposed/05-documents-office.md#off-28), [OFF-29 RichMailComposer](../proposed/05-documents-office.md#off-29), [OFF-30 CalendarView](../proposed/05-documents-office.md#off-30), [OFF-31 RecurrenceEditor](../proposed/05-documents-office.md#off-31), [OFF-32 TaskBoard](../proposed/05-documents-office.md#off-32)

**Change area:** MailView, RichMailComposer, CalendarView, RecurrenceEditor, TaskBoard and local/provider models.

**Additional obligations:** [ENG-21](supporting-scope.md#eng-21).

1. Define message/thread identities, drafts/attachments, calendar events/zones/recurrence exceptions and task status/order.
2. Implement a functional local message/calendar/task store and import/export fixtures before any remote service adapter.
3. Implement reading/threading, rich draft composition, event recurrence editing and board transactions with actual persisted models.
4. Define explicit provider operations, offline/error states and acknowledgments; test sending/sync against a local test endpoint, never a real recipient as an automated test.
5. Verify recurring-event exceptions/DST, draft recovery, attachments, task reorder/undo and offline conflict behavior.

**Accept when:** All five productivity views operate on real local data with documented provider boundaries and reproducible failure behavior.

**Delegate:** Luna implements message/calendar/board presentation and recurrence fixtures; Sol owns protocols, recurrence and persistence.

**Astra trigger:** Astra may help with an unresolved recurrence/time-zone or sync conflict algorithm, with exact event fixtures.

<a id="wp-059"></a>
## WP-059 — Implement forms, schemas and report design

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-022](phase-02-controls-data.md#wp-022), [WP-029](phase-02-controls-data.md#wp-029), [WP-051](phase-04-office.md#wp-051), [WP-053](phase-04-office.md#wp-053)

**Proposal scope:** [OFF-33 FormDesigner](../proposed/05-documents-office.md#off-33), [OFF-34 DatabaseSchemaView](../proposed/05-documents-office.md#off-34), [OFF-35 ReportDesigner](../proposed/05-documents-office.md#off-35)

**Change area:** FormDesigner, DatabaseSchemaView, ReportDesigner and local relational/report providers.

**Additional obligations:** [ENG-22](supporting-scope.md#eng-22).

1. Define schema introspection, relationships, typed fields, form bindings, report bands/groups and parameterized query contracts.
2. Implement a real local database provider and schema inspection; separate schema previews from explicitly executed migrations.
3. Implement form design/editing and report layout with persistent bindings, validation and command/undo support.
4. Render reports using the paginated output path and expose data/query failures without corrupting layouts.
5. Verify parameter binding, null/type changes, grouping/totals, pagination, saved designs and a fixture-only reversible schema-change scenario.

**Accept when:** Users can design and run real data-backed forms/reports with correct bindings and printed output.

**Delegate:** Luna owns designer property panels and fixtures; Sol owns database/provider and report evaluation semantics.
