<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Forms, schemas and reports

WP-059 gives an office app data-entry forms and printable reports over a
local SQLite database. `Tk.LocalDatabaseProvider` reads and changes the
database ([controls](controls.md#local-database-forms-and-reports)). A
**design** says what a form and a report show, and it is a session
document. A **report run** goes through the WP-029
[query engine](query-pivot.md) into a paginated rich-text document, which the
[print path](printing.md) previews, prints and writes as PDF. Decisions:
[D-182](decisions-domains.md#d-182--reports-evaluate-in-the-query-engine-over-a-bounded-snapshot),
[D-183](decisions-domains.md#d-183--a-report-is-a-rich-text-document-with-band-paragraphs-and-tables),
[D-184](decisions-domains.md#d-184--a-design-is-a-session-document-of-whole-design-steps)
and [D-185](decisions-domains.md#d-185--saved-designs-are-qualified-against-the-live-schema).

| Part | Files |
| --- | --- |
| SQLite provider | `src/core/local_database_provider.{h,cpp}` |
| Table snapshot as a `QuerySource` | `src/core/sqlite_query_source.{h,cpp}` |
| Report definition, evaluation, layout | `src/core/local_report_spec.{h,cpp}`, `local_report_engine.cpp` |
| Report output model | `src/core/local_report_document_model.{h,cpp}` |
| Design document, edits, codec | `src/core/local_data_design_document.{h,cpp}`, `local_data_design_edits.cpp`, `local_data_design_codec.cpp` |
| Design file checks | `src/core/local_data_design_store.{h,cpp}` |
| Controls | `FormDesigner`, `DatabaseSchemaView`, `ReportDesigner` |
| Fixture | [examples/forms-schema-report](../examples/forms-schema-report/Main.qml) |

## Design format

<a id="design-format"></a>
A design is a JSON map with `format: "qindatk.local-data-design"` and
`version` 2 (version 1 files from the first cut still open):

| Member | Contents |
| --- | --- |
| `designId` | A stable name for the design |
| `binding` | `{provider: "sqlite", schema: "main", table, fields: [...], types: {field: "integer" \| "number" \| "text" \| "boolean"}}`. `types` records the column types when the design was bound. A new document is unbound, with an empty `table` and no fields |
| `form` | `{title, tableName, fields: [{fieldName, label, hidden}]}`, listed in form order |
| `report` | The report definition below |

Every field the form or report names must be a bound field. The document
refuses an edit that breaks this rule, and `LocalDataDesignStore` refuses to
open such a file.

## Report definition

<a id="report-definition"></a>
| Key | Meaning |
| --- | --- |
| `tableName` | The bound table. `LocalDataDesignDocument.report` fills it in |
| `headerText`, `footerText` | The report header (a level 1 heading) and the closing paragraph |
| `pageHeaderText`, `pageFooterText` | The page header and page footer bands, one line in every page's top and bottom margin. `{page}` and `{pages}` expand to the page number and count. Without `pageFooterText` a report prints "Page {page} of {pages}"; `""` turns a band off. At most 500 characters |
| `columns` | The detail band: field names or `{field, label}`. When the list is empty, every column is shown (every bound field, through the document) |
| `groups` | Up to 3 levels, outermost first: `{field, label, pageBreakBefore, totals: [{field, function}]}` |
| `totals` | Report-footer totals, `[{field, function}]` |
| `detail` | `false` prints only the group headers and totals (a summary) |
| `query.filter` | A WP-029 predicate, in the JSON that `FilterBar` emits (`compare`, `and`, `or`, `not`, `isNull`, `isNotNull`) |
| `query.parameters` | Typed parameter values, `{id: {type, value}}` (`value` holds integer digits as a string) |
| `query.sort` | `[{field, ascending, nullsLast}]`, applied after the group keys. A report sorts on at most 4 keys, groups included |
| `page` | `{paperId: a4 \| a5 \| letter \| legal, orientation: portrait \| landscape}` |

The `function` names are the WP-029 aggregate names: `countRows`,
`countValues`, `sum`, `average`, `minimum` and `maximum`. Every group footer
and the report footer print the row count first. `sum` and `average` need a
numeric column.

The first-cut keys `groupField`, `sumField`, `filterField` and `filterValue`
still work. They read as one group level with a SUM, a summary report and an
equality filter. The first edit of such a design rewrites it into the
current keys.

## Evaluation

<a id="evaluation"></a>
`LocalReportDocumentModel.build(provider, report, parameters)`:

1. **Snapshot.** `SqliteTableSource` reads the table's reportable columns in
   one bound read, at most 4,000 rows. It maps each declared type to a
   `QueryType` using SQLite's affinity rules. `BOOL` becomes boolean,
   `DATE`/`TIME` become text, and BLOB or untyped columns are left out. Every
   stored value takes its column's type. A value of another kind, such as
   text stored in a REAL column, becomes an error value.
2. **Parse.** `LocalReport::parse` checks the definition against the live
   schema. `QueryEngine::fromJson` parses the filter, parameters and sort, and
   `QueryEngine::validate` checks them. A value passed in `parameters` must
   name a declared parameter. It is converted to the type of the column it is
   compared with. For an integer column, `"3"` and `3` both work, and
   `"many"` is refused. Parameters are only ever values: they are never
   pasted into SQL.
3. **Run.** Two `QueryEngine::execute` calls run on the same snapshot:
   - A **detail** run applies the projection and filter, then sorts by the
     group keys (ascending, nulls last) and the report's sort keys. Ties keep
     table order.
   - A **grouped** run uses the group fields as its row dimensions, with the
     row count and every total as measures.

   The engine's null rules hold. Comparisons with null never match. Sums and
   averages skip nulls, and a group whose values are all null totals to null
   (printed "—"), not zero. The report is limited to 2,000 matched rows.
4. **Bands.** The ordered rows are walked into bands: report header, report
   info (source, row count, parameter values), group headers, detail, group
   footers, the report footer's totals and the footer text. Each group footer
   looks up its totals by the engine's group ID, so every printed total is
   the engine's own value.

## Output

<a id="output"></a>
`LocalReport::documentJson` lays the bands out as a `qindatk.richtext`
document:

| Band | Layout |
| --- | --- |
| Report header | Heading 1 |
| Group header | Heading 2–4, indented by level, kept with the next block. With `pageBreakBefore`, every group of that level except the first starts a new page, but only when it is the outermost group that changed, so a header is never left behind on the previous page |
| Page header and page footer | <a id="page-bands"></a>The document's running page regions (`page.headerText`, `page.footerText`, [rich text](rich-text.md#running-page-regions)), painted in the 48pt margins of every page, so they never move the body |
| Detail | One table per run of the innermost group. Its first row holds the column labels, and the paginator repeats that row at the top of every continuation page, above that page's first detail row |
| Group and report totals | Bold paragraphs |

The document is decoded into the model's single `document`, so a
`Tk.PrintAdapter`, a `RichDocumentView` or a `PrintPreview` bound to it
follows every rebuild. `exportPdf(path)` is the document's default output
ticket. `PrintAdapter.print()` sends the same pages to PDF files or printers
(docs/printing.md).

A build that fails leaves the document, `rowCount`, `groupCount`, `columns`
and `bands` from the last good build unchanged. It sets `errorText` and one
of these `errorStage` values:

| Stage | Cause |
| --- | --- |
| `definition` | An unknown or unbound column, a SUM over text, too many groups or sort keys |
| `parameters` | An unknown parameter, or a value that is not of its type |
| `query` | A closed database, an unknown table, the row limits, or stored data of the wrong type. The error names the column, for example "Column amount holds text in a number column" |
| `layout` | The rich-text decoder refused the result |
| `output` | A PDF path problem |

Page numbers come from the page footer band
([D-183](decisions-domains.md#d-183--a-report-is-a-rich-text-document-with-band-paragraphs-and-tables)).
`tst_local_report_design` checks "Page *n* of *N*" on every page of a
multi-page report, the inked margins of printed sheets, and that the header
row repeats with no detail row lost.

## Design document

<a id="design-document"></a>
`Tk.LocalDataDesignDocument` is a `DocumentModel` with the schema
`qindatk.local-data-design.v1` (kind `data-design`, suffix `qtkdata`). Its
codec is built in, so `DocumentManager.createDocument(schemaId)`,
`openDocument`, save, autosave, recovery frames and `restoreRecovered` all
work as they do for the other documents ([document models](document-models.md)).
A document created on its own keeps a private 128-step history. A session
adopts that history into its shared `UndoService`.

Each edit records one whole-design step. If the edit would break the design
rules, it returns false, sets `lastError` and changes nothing:

| Method | Undo label |
| --- | --- |
| `bindTable(provider, table, fields)` | Bind to *table*: binds the table and records the live column types. Form fields, columns, groups, totals and a filter that name an unbound field are dropped |
| `setFormTitle`, `setFormFieldLabel`, `setFormFieldHidden`, `moveFormField` | Form layout |
| `setReportText(headerText \| footerText \| pageHeaderText \| pageFooterText)`, `setReportColumns`, `setReportDetail` | Report and page bands |
| `setReportGroups`, `setReportTotal(groupField, field, function, enabled)`, `setGroupPageBreak` | Groups and totals (`groupField` empty: the report footer) |
| `setReportFilter(predicate, parameters)` | The filter. `QueryEngine::fromJson` checks it first |
| `setReportPage(paperId, orientation)` | Page rules |
| `setDesign(map)` | Replaces the whole design, for example after reopening a file |

`UndoService.beginMacro`/`endMacro` group several edits into one step. The
file is the design map plus a `session` member that holds the document ID,
revision, reset generation and extensions. `LocalDataDesignStore.loadFromFile`
reads it too. A version 1 file opens with an identity derived from its
`designId`, so opening it twice gives the same document.

`validate(provider)` (or `LocalDataDesignStore.validate`) checks a saved
design against today's schema. It returns `fields: [{name, status, savedType,
liveType}]`, where `status` is `ok`, `missing`, `typeChanged` or
`unsupported`, and `issues`. A missing column and a report that no longer
parses (for example an `average` over a column that became text) are fatal.
A changed type on its own is a warning.

## Designers

<a id="designers"></a>
With `designDocument` set, `FormDesigner` shows the document's form in form
order. In design mode it can edit the title, labels, "Shown" and field order
(up/down), with undo and redo in its header. `ReportDesigner` edits the
header, footer, page header and page footer, first group level, group total and function, "New page per
group", "Detail rows" and the equality filter, and its export request
carries `designDocument.report`. Every change is one document command. The
panel shows a refused edit in its error notice. Nested groups and other
report shapes are set through the document API. Without `designDocument`,
both controls keep their host-snapshot behaviour and request signals.

`FormDesigner` changes record values and emits `recordSaveRequested` only in
edit mode. In design or preview mode `setFieldValue` and `requestRecordSave`
return false and set `requestState` to `refused`, whoever calls them. A blank
entry is null, never 0, and a number column refuses text or, for an INTEGER
column, a fraction (`requestState` `invalid`). A required (NOT NULL,
non-key) field left blank fails the save with "*label* is required".

`DatabaseSchemaView` never patches `schemaSnapshot` from a plan. It re-reads
the provider when the provider changes and when the host acknowledges a
commit, so a failed commit (`acknowledge(id, "failed", error)`) leaves the
fields and relationships the database really has and shows the error. A
previewed plan lists the foreign keys it adds and removes
(`planRelationships`, from the plan's `relationshipChanges`). A migration
plan may copy rows with `INSERT INTO t SELECT ...` only into a table the same
plan creates, the copy step of SQLite's table rebuild, so relationship
changes on a table with rows can be planned.

## Verification

- `tst_local_report_design`: nested groups with null keys and values, every
  band text and detail cell, summary and first-cut definitions, typed
  parameters (overrides, a refused value, a declared type mismatch, a
  hostile string), failures at each stage that keep the last document, a
  group page break (1 page without it, 2 with it), printing through a
  `PrintAdapter` backend whose sheets equal the preview, and PDF page counts
  through the adapter and `exportPdf`. A 300-row table paginates with the
  header row repeated at the top of every later page and no detail row lost
  or repeated, and every page prints "Page *n* of *N*" and its page header
  in the margins, on printed sheets equal to the preview.
- `tst_local_data_design_document`: each command, refused edits, undo/redo
  and a macro; `DocumentManager` create, save, reopen with the same identity,
  and crash recovery; a version 1 file opened with a stable identity; and a
  fixture-only reversible schema change (a rename and back, and a column
  retyped by a table rebuild and back). The design is qualified at each step,
  a failed build keeps the old document, and the report comes back
  identical.
- `tst_local_database_provider`: relationship changes through table
  rebuilds preview with their added or removed foreign keys and change
  nothing; a stale plan fails whole at commit, leaving schema and rows.
- `tst_wp059_database_workflows.qml`: the designers driving a document,
  page band edits, form validation (a non-number, a fraction, a blank
  required field) and no record write in design or preview mode, and
  `DatabaseSchemaView` through a relationship plan, a failed commit and a
  successful one.
- `tst_local_data_design_output`.

Performance, other platforms, screen readers and output-fidelity comparison
belong to the qualification packages WP-088 to WP-094.
