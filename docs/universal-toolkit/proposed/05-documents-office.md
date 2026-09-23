<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Documents, Office and productivity surfaces

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

[AppWindow, toolbars](../current/05-shell-navigation.md), [Viewport/Ruler](../current/08-media-canvas.md)
and template text inputs already exist. The following surfaces require a durable
application document model and explicit layout/format adapters. A text editor,
spreadsheet, slide deck, database and mailbox do not share one universal content
schema. They can share commands, selection, transactions, jobs and view shells.

<a id="off-01"></a>
## OFF-01 — RichTextEditor

**Priority:** P1. **Purpose:** Inline rich-text editing for notes, comments, captions and mail.

**Contract sketch:** Structured text/selection, character/paragraph attributes, links/lists/inline objects and composition state; formatting invokes commands.

**Dependencies and boundary:** Text document/shaping engine and clipboard adapter; TextArea supplies only part of the starting behavior.

**Completion check:** IME, grapheme selection, formatting and paste work with undo and accessible cursor/selection reporting.

<a id="off-02"></a>
## OFF-02 — RichDocumentView

**Priority:** P1. **Purpose:** Paginated editing surface for long structured documents.

**Contract sketch:** Document/layout adapter, visible pages, logical cursor/selection, anchored objects and navigation; viewport materializes visible pages.

**Dependencies and boundary:** Typesetting/pagination engine owns layout and text flow; DocumentSession owns persistence.

**Completion check:** Edit across a page boundary, paginate, undo and reopen while cursor mapping and anchored objects remain correct.

<a id="off-03"></a>
## OFF-03 — FormatBar

**Priority:** P1. **Purpose:** Contextual text-format commands and values.

**Contract sketch:** Bind font, size, style, paragraph, list and color properties through PropertyBinding and command IDs, including mixed states.

**Dependencies and boundary:** Active rich-text selection model; FontPicker and color controls.

**Completion check:** Toolbar state follows cursor/selection; a formatting change preserves focus and is one semantic edit.

<a id="off-04"></a>
## OFF-04 — StyleManager

**Priority:** P2. **Purpose:** Named character, paragraph, table and object styles.

**Contract sketch:** Style IDs, inheritance, preview and direct-format overrides; create/update/apply/reset through explicit commands.

**Dependencies and boundary:** Document style graph and layout engine; cycle/compatibility validation.

**Completion check:** Change a parent style and verify all dependent content updates, with direct overrides preserved according to policy.

<a id="off-05"></a>
## OFF-05 — PageNavigator

**Priority:** P1. **Purpose:** Virtual page/spread thumbnails and outline navigation.

**Contract sketch:** Stable page/section IDs, labels, thumbnails and current location; navigate/reorder/insert/delete requests where supported.

**Dependencies and boundary:** Page model and AssetProvider; document engine validates section constraints.

**Completion check:** Reorder or navigate a long document without loading every page image or losing logical text selection.

<a id="off-06"></a>
## OFF-06 — PageSetupDialog

**Priority:** P2. **Purpose:** Page size, margins, orientation, columns, bleed and sections.

**Contract sketch:** Display canonical physical dimensions and scope (document/section/page); validate printable/content bounds before commit.

**Dependencies and boundary:** Document layout and printer/output capability adapters.

**Completion check:** Apply a section margin change, inspect reflow, cancel/undo and preserve document units across locales.

<a id="off-07"></a>
## OFF-07 — TextFrameEditor

**Priority:** P2. **Purpose:** Create, connect and edit text frames in page-layout documents.

**Contract sketch:** Frame IDs, geometry, thread links, columns/insets and overset state; connect/disconnect/resize requests.

**Dependencies and boundary:** Typesetting engine maps logical stories to frames and pages.

**Completion check:** Thread a story through multiple frames, resize one, and preserve text/order and undo across reflow.

<a id="off-08"></a>
## OFF-08 — TableEditor

**Priority:** P2. **Purpose:** Structured tables inside text/page documents.

**Contract sketch:** Row/column/cell IDs, spans, styles, widths and text selection; insert/delete/merge/split commands.

**Dependencies and boundary:** Document table layout model and rich-text selection adapter.

**Completion check:** Merge/split cells across page boundaries and undo while preserving content, accessibility order and export structure.

<a id="off-09"></a>
## OFF-09 — EquationEditor

**Priority:** P2. **Purpose:** Structured mathematical expression editing.

**Contract sketch:** Expression tree, cursor/selection, symbol palette, source/visual views and validation; emit semantic edits.

**Dependencies and boundary:** Math parser/layout/interchange backend; no equation meaning inferred from plain text styling.

**Completion check:** Visual/source edits roundtrip on a documented expression subset and are accessible through a readable linear alternative.

<a id="off-10"></a>
## OFF-10 — ReferenceManager

**Priority:** P2. **Purpose:** Manage citations, footnotes, captions, fields and cross-references.

**Contract sketch:** Stable target/reference IDs, formatting descriptors, resolved/broken status and update commands.

**Dependencies and boundary:** Document field/bibliography engine and style system.

**Completion check:** Move/delete a target and verify references renumber or report broken links without silently pointing elsewhere.

<a id="off-11"></a>
## OFF-11 — ReviewPane

**Priority:** P2. **Purpose:** Comments and tracked-change review tied to document locations.

**Contract sketch:** Thread/change IDs, author/time, anchored ranges and resolved state; accept/reject/reply/navigation requests.

**Dependencies and boundary:** Revision/comment model; synchronization and permissions are adapter responsibilities.

**Completion check:** Edit preceding text and keep anchors correct; accept/reject one change with undo and preserve other reviewers' content.

<a id="off-12"></a>
## OFF-12 — PrintPreview

**Priority:** P1. **Purpose:** Page-accurate preview and print/export settings.

**Contract sketch:** Page renderer, paper/output profile, range/scale/duplex/copies and warnings; render preview asynchronously and submit explicit jobs.

**Dependencies and boundary:** Print/export adapter supplies final pagination and output capabilities.

**Completion check:** Preview page geometry matches exported/printed output; cancel and failed jobs retain settings and report the affected range.

<a id="off-13"></a>
## OFF-13 — PreflightPanel

**Priority:** P2. **Purpose:** Navigate production problems before print or export.

**Contract sketch:** ValidationIssue model grouped by fonts/assets/color/layout/output profile; jump-to-target and repair commands.

**Dependencies and boundary:** Domain preflight engine and ResourceResolver; checks have explicit scope/profile.

**Completion check:** Known missing-font, overset-text and asset fixtures produce actionable issues that clear after validated repair.

<a id="off-14"></a>
## OFF-14 — SheetView

**Priority:** P1. **Purpose:** Spreadsheet surface with sheet-specific selection and fill behavior.

**Contract sketch:** Use EditableGrid plus row/column headers, cell/range references, fill handle, freeze panes, merged cells and edit modes.

**Dependencies and boundary:** Workbook cell model and calculation engine own formulas, dependency graph and values.

**Completion check:** Enter/copy/fill formulas, switch sheets, undo and save/reopen with correct relative/absolute references and no delegate-owned state.

<a id="off-15"></a>
## OFF-15 — FormulaBar

**Priority:** P1. **Purpose:** Edit a cell expression and inspect referenced ranges.

**Contract sketch:** Active cell/name box, expression text, parsed reference spans, function hints and validation; commit/cancel through sheet editing.

**Dependencies and boundary:** Formula parser/evaluator and SheetView selection adapter.

**Completion check:** Select a referenced range while editing, cancel, and preserve prior formula; error spans identify invalid syntax.

<a id="off-16"></a>
## OFF-16 — WorkbookTabs

**Priority:** P1. **Purpose:** Worksheet navigation with naming, ordering and visibility.

**Contract sketch:** Stable sheet IDs, titles, color, protected/hidden state and rename/reorder/add/remove requests.

**Dependencies and boundary:** Workbook model and DocumentSession.

**Completion check:** Rename/reorder sheets without breaking references; removal prompts and undo restore formulas according to model policy.

<a id="off-17"></a>
## OFF-17 — ConditionalFormatEditor

**Priority:** P2. **Purpose:** Configure rule-based cell presentation.

**Contract sketch:** Rule IDs, target ranges, typed predicates, precedence, scales/icons and preview; validate and commit as one change.

**Dependencies and boundary:** Workbook rule evaluator; themes style the editor, document rules style cells.

**Completion check:** Overlapping rules apply in documented order and preview equals the saved/reopened sheet result.

<a id="off-18"></a>
## OFF-18 — DataValidationEditor

**Priority:** P2. **Purpose:** Configure allowed cell values and entry feedback.

**Contract sketch:** Range, type/list/formula constraints, blank policy, input message and invalid-value policy.

**Dependencies and boundary:** Workbook validation engine and optional data-source adapters.

**Completion check:** Pasting a range reports invalid cells accurately and applies the chosen all-or-nothing/partial policy with undo.

<a id="off-19"></a>
## OFF-19 — SlideCanvas

**Priority:** P1. **Purpose:** Editable slide surface using shared object tools.

**Contract sketch:** Slide/master IDs, object model, bounds, selection and guides; text/media/object insert and transform use shared commands.

**Dependencies and boundary:** Slide document/render engine, CanvasView and rich-text adapter.

**Completion check:** Edit text and shapes, apply a master, undo and export with consistent object geometry and inheritance.

<a id="off-20"></a>
## OFF-20 — SlideSorter

**Priority:** P1. **Purpose:** Virtual slide overview with reorder/group operations.

**Contract sketch:** Slide IDs/previews, sections, hidden/status state and multi-selection; drag/keyboard reorder and duplicate/delete requests.

**Dependencies and boundary:** Slide model, AssetGrid and transactions.

**Completion check:** Reorder many slides while preserving transition/section links and show current-slide identity after filtering.

<a id="off-21"></a>
## OFF-21 — SlideMasterEditor

**Priority:** P2. **Purpose:** Edit reusable master/layout relationships and placeholders.

**Contract sketch:** Master/layout IDs, placeholder roles, theme styles and inheritance/override visualization.

**Dependencies and boundary:** Slide style/layout model and CanvasView.

**Completion check:** Change a master placeholder and update dependent slides while preserving deliberate per-slide overrides.

<a id="off-22"></a>
## OFF-22 — AnimationPane

**Priority:** P2. **Purpose:** Order and edit object animations and transitions.

**Contract sketch:** Effect IDs, targets, timing/trigger/dependency and easing descriptors; preview, reorder and transaction-aware parameter editing.

**Dependencies and boundary:** Animation engine and shared CurveEditor/time model.

**Completion check:** Reorder effects and edit timings; preview and exported playback honor the same sequence and cancellation state.

<a id="off-23"></a>
## OFF-23 — PresenterView

**Priority:** P2. **Purpose:** Separate presenter controls and audience output.

**Contract sketch:** Current/next slide, notes, elapsed time, navigation, blanking and annotation commands; choose display by stable screen identity.

**Dependencies and boundary:** Presentation playback engine and WindowHost; audience surface has an independent view role.

**Completion check:** Disconnect/reconnect a display without exposing private notes on the audience output; timing and navigation remain controllable.

<a id="off-24"></a>
## OFF-24 — PdfView

**Priority:** P1. **Purpose:** PDF page, text and link navigation surface.

**Contract sketch:** Document handle, page/zoom/rotation, text search/selection, links and page thumbnails; explicit render-only versus edit capabilities.

**Dependencies and boundary:** PDF render/text-geometry backend with async page requests.

**Completion check:** Search/select text across rotated pages and inspect known PDFs; renderer failure is shown per page without losing navigation.

<a id="off-25"></a>
## OFF-25 — PdfAnnotationTools

**Priority:** P2. **Purpose:** Create/review annotations, form edits and proposed redactions.

**Contract sketch:** Annotation/form IDs, page coordinates, authors and capability flags; edits and final redaction are distinct commands/states.

**Dependencies and boundary:** PDF writer/form/redaction backend; OCR/signature operations are separate jobs.

**Completion check:** Save/reopen annotations; verify applied redaction removes underlying content through backend tests, not only an opaque overlay.

<a id="off-26"></a>
## OFF-26 — NotebookView

**Priority:** P2. **Purpose:** Notebook/section/page organization with mixed content.

**Contract sketch:** Stable hierarchy IDs, page metadata/tags, navigation/history and insert/move/search requests.

**Dependencies and boundary:** Notebook storage/index/sync model; BlockEditor or ink canvas renders the page.

**Completion check:** Move pages between sections, work offline, reopen and reconcile conflicts without dropping attachments.

<a id="off-27"></a>
## OFF-27 — BlockEditor

**Priority:** P2. **Purpose:** Composable text, image, code, table, embed and ink blocks.

**Contract sketch:** Stable block IDs/order, typed data/delegates, focus/caret transitions, insert/move/merge and rich clipboard operations.

**Dependencies and boundary:** Block document model, RichTextEditor and domain block adapters.

**Completion check:** Keyboard navigation and paste across block boundaries preserve structure and undo; unsupported blocks retain their data.

<a id="off-28"></a>
## OFF-28 — MailView

**Priority:** P2. **Purpose:** Virtual message/thread reading with attachments and actions.

**Contract sketch:** Conversation/message IDs, headers/body trust mode, attachment states and reply/archive/delete commands.

**Dependencies and boundary:** Mail store/protocol/sanitized-content renderer; no network/account logic is implied by the view.

**Completion check:** Open threads offline, load attachments lazily and update flags without displaying a late body for another message.

<a id="off-29"></a>
## OFF-29 — RichMailComposer

**Priority:** P2. **Purpose:** Message composition with recipients, formatting and attachments.

**Contract sketch:** Recipient tokens, subject, rich body, attachment jobs and draft state; explicit send request follows validation.

**Dependencies and boundary:** Mail account/draft service, TokenField, RichTextEditor and attachment provider.

**Completion check:** Draft recovery and failed sending preserve content/attachments; recipient edits validate without losing composition focus.

<a id="off-30"></a>
## OFF-30 — CalendarView

**Priority:** P2. **Purpose:** Agenda/day/week/month views over one calendar model.

**Contract sketch:** Event IDs, intervals, all-day state, calendars, zones and availability; create/move/resize/navigation requests.

**Dependencies and boundary:** Recurrence/time-zone/calendar engine and SelectionModel.

**Completion check:** Move recurring and all-day events across DST boundaries with explicit scope and undo, retaining their intended local times.

<a id="off-31"></a>
## OFF-31 — RecurrenceEditor

**Priority:** P2. **Purpose:** Human-readable configuration of repeating events and exceptions.

**Contract sketch:** Rule frequency/interval/weekdays/end conditions and occurrence exceptions; preview upcoming occurrences.

**Dependencies and boundary:** Calendar recurrence engine with explicit zone/calendar semantics.

**Completion check:** Preview leap-day/DST/end-date fixtures; editing one occurrence remains distinct from changing the entire series.

<a id="off-32"></a>
## OFF-32 — TaskBoard

**Priority:** P2. **Purpose:** Grouped task cards with list/board views.

**Contract sketch:** Task IDs, status, assignees, due dates and ordering; move/edit requests and filters use model/proxies.

**Dependencies and boundary:** Task/project model and transactions; remote updates require conflict policy.

**Completion check:** Move cards between columns by keyboard/pointer while remote changes preserve stable identity and report conflicts.

<a id="off-33"></a>
## OFF-33 — FormDesigner

**Priority:** P2. **Purpose:** Visual design of database/business forms.

**Contract sketch:** Field/control IDs, schema bindings, layout constraints, validation and preview mode; use shared canvas selection and inspector.

**Dependencies and boundary:** Form schema/runtime, data binding and database metadata adapters.

**Completion check:** Design a form, save/reopen, bind a record and validate edits without executing unintended data writes in design mode.

<a id="off-34"></a>
## OFF-34 — DatabaseSchemaView

**Priority:** P2. **Purpose:** Explore/edit tables and relationship diagrams.

**Contract sketch:** Table/field/key IDs, relationship/cardinality metadata and proposed schema changes; preview migration requests.

**Dependencies and boundary:** Database catalog and migration planner; UI never silently applies destructive schema edits.

**Completion check:** Relationship changes produce an inspectable migration plan and reflect backend failure without falsely updating the schema view.

<a id="off-35"></a>
## OFF-35 — ReportDesigner

**Priority:** P2. **Purpose:** Design paginated data reports with sections and grouping.

**Contract sketch:** Report object IDs, bound fields, groups, headers/footers, page rules and preview/export requests.

**Dependencies and boundary:** Query/data adapter and report pagination engine.

**Completion check:** Generate a report from known data and verify grouping/totals/page breaks match preview and saved design.
