<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Printing and publishing preflight

WP-053 turns a [rich-text document](rich-text.md) into printed or PDF
output and checks it first. `Tk.PrintAdapter` owns everything: the
destinations and what they can do, one print ticket, the output sheets, a
bounded preview cache, print jobs and preflight. `Tk.PrintPreview`,
`Tk.PrintSheetView` and `Tk.PreflightPanel` only draw the adapter's
properties and send requests back. [examples/print-preflight](../examples/print-preflight/Main.qml)
binds all three to one document. Decisions:
[D-130](decisions-domains.md#d-130--one-output-description-for-preview-files-and-printers),
[D-131](decisions-domains.md#d-131--print-jobs-print-a-snapshot-one-sheet-per-turn),
[D-132](decisions-domains.md#d-132--preflight-issues-have-derived-ids-and-validated-repairs)
and [D-133](decisions-domains.md#d-133--bleed-is-page-setup-page-geometry-stays-document-wide).

| Part | Files |
| --- | --- |
| Tickets, papers, capabilities | `src/core/print_ticket.{h,cpp}` |
| Sheet geometry, painting, devices, PDF files | `src/core/print_output.{h,cpp}`, `rich_text_pdf_export.cpp` |
| Preflight checks | `src/core/print_preflight.{h,cpp}` |
| Adapter, jobs, repairs | `src/core/print_adapter{.h,_p.h,.cpp,_jobs.cpp,_repair.cpp}` |
| Sheet preview item | `src/core/print_sheet_view.{h,cpp}` |
| Native printers (optional) | `src/platform_widgets/native_print_backend.{h,cpp}` in `qindatk_widgets` |

## Tickets

A ticket is the adapter's `settings` map, the same keys `PrintPreview`
edits. `applySettings(patch)` merges a patch. A malformed value (an unknown
colour mode, 1.5 copies, `pages: "x"`) is refused with `lastError`, and the
ticket stays as it was. An option the destination lacks is accepted: it is
reported by preflight and refused by `print()`.

| Key | Values |
| --- | --- |
| `printerId` | a destination ID: `pdf`, `print-to-file`, `printer:<name>` |
| `outputPath` | file destinations: a local path or `file:` URL |
| `pages` | ranges like `1-3, 5, 8-`; empty prints every page. `pageRange` `{startPage, endPage}` is the one-range form `PrintPreview` sends |
| `paperId` | `document` (the page's own size), `a3`, `a4`, `a5`, `letter`, `legal`, `tabloid`, or a printer's `native:<key>` |
| `orientation` | `auto` (follow the page), `portrait`, `landscape` |
| `scaleMode`, `scalePercent` | `fit`, `actual`, or `percent` with 1–1000 |
| `copies`, `collate` | copies from 1; collate when the device can |
| `duplex` | `simplex`, `long-edge`, `short-edge` |
| `colorMode` | `color`, `grayscale` |
| `includeBleed`, `cropMarks` | extend each sheet by the page's bleed; draw trim marks outside it |

Page ranges are checked against the laid-out page count when they are used,
so a selection can outlive an edit that shortens the document. `print()`
refuses out-of-range pages (`invalidArgument`, field `pages`).

## Destinations and capabilities

`destinations` lists every destination as a capabilities map;
`capabilities` is the selected one. The map has `printerId`, `label`, `kind`
(`printer` or `file`), `available`, `status`, option lists in
`PrintPreview`'s shape (`paperOptions`, `orientationOptions`,
`scaleOptions`, `duplexOptions`, `colorOptions`, each `{id, label}`),
`maxCopies`, `collate`, `bleed`, `printableMarginsPt` and
`unavailableOptions`: the user-facing names of what the device lacks, which
`PrintPreview` shows.

| Destination | Backend | Notes |
| --- | --- | --- |
| `pdf` "Save as PDF" | `Print::PdfFileBackend` (base library) | `QPdfWriter` through `QSaveFile`: the file appears only when the job completes. Colour or grayscale PDF; one copy; no duplex |
| `print-to-file` | `NativePrintBackend` | The system print engine (`QPrinter`, PDF format) writing `<path>.part`, renamed on success |
| `printer:<name>` | `NativePrintBackend` | CUPS printers from `QPrinterInfo`: papers, duplex and colour modes, copies and the unprintable edge as the printer reports them. The job reaches CUPS when it completes |

The native backend lives in `qindatk_widgets` (built when Qt PrintSupport
is found) so the base library stays QtQuick-only. An application registers
it once, after creating its application object:

```cpp
QindaTK::PrintAdapter::registerDefaultBackend(std::make_shared<QindaTK::NativePrintBackend>());
```

An unknown or vanished destination stays selected and reports
`available: false`; a job never goes somewhere else.

## One output description

A sheet is where one document page lands on the paper fed to a device:
`sheetFor(page, sourcePage, ticket, paper)` gives the sheet size, the trim
box on it, the scale, the bleed and the crop-mark length. `sheets` lists
the sheets of the selection (`{sourcePage, pageNumber, widthPt, heightPt,
trim, scale, bleedPt, markPt, clipped}`), and `sheetForPage(i)` gives one
page's sheet with `selected`.

- `document` paper: the sheet is the page plus bleed and marks, at 100 %.
- Standard paper: `auto` orientation follows the page; `fit` scales the
  page with its bleed and marks to the sheet, `actual` keeps 100 %, and the
  result is centred. `clipped` flags a page that overruns the sheet.
- Every device paints through `Print::paintSheet` with a painter in
  1/96-inch units, the layout's own unit: preview images, `QPdfWriter`, the
  system print engine and test devices alike. `RichTextDocument.exportPdf`
  is this path with the default file ticket (document paper, every page).

Output paints black text with no selection, caret or frame guides. Tracked
insertions print underlined and deletions struck through (preflight warns
about them); comments are not printed.

## Preview

`Tk.PrintSheetView { adapter; pageIndex }` shows one page's sheet as the
job will print it, grayscale included. It fetches images in `updatePolish`
from the adapter's cache, never while painting, at a zoom snapped to 1/64
steps. `previewImage(page, zoom)` (C++) renders on demand; the cache holds at
most `previewCacheLimit` images (default 8) and 64 MiB, evicting the least
recently used. `previewCacheCount`, `previewCacheBytes` and
`previewRenderCount` report it. Any document or ticket change empties the
cache and bumps `previewGeneration`.

## Jobs

`print(patch)` applies the patch, validates the ticket against the
destination and the page count, and returns a job ID, or "" with
`lastError`/`lastErrorCode` (`invalidArgument`, `unsupported`,
`unavailable`, `conflict` while another job runs). The job:

1. copies the document (`toJson`/`loadJson`) and lays the copy out, so edits
   made while it prints do not move its pages;
2. opens a device and paints one sheet per event-loop turn (`job.status`
   `pending`, `running`, `progress`, `sheetsDone`);
3. finishes the device: `completed`, or `failed` with `error`.

`cancel(jobId)` sets `cancelling`; the next turn aborts the device and the
status becomes `cancelled`. Nothing is written or spooled: the PDF file is
never created, and the print engine drops its spool file. A failed or
cancelled job reports `affectedPages` (with `affectedStart`, `affectedEnd`)
— the pages that did not reach paper or disk — and `failedPage`. The ticket
is kept, so the same print can be retried; `jobFinished(jobId, status)`
fires at the end. A device that delivers sheets as it goes reports them
through `Device::deliveredSheets()`, and only the rest are affected. For CUPS
the adapter reports `completed` when the job is handed to the spooler; it
does not follow the job on the printer.

## Preflight

`runPreflight()` checks the document and the ticket for the selected
`profileId` and returns `issues` in `PreflightPanel`'s shape, plus
`paragraphId`, `offset`, `pageIndex` and `fieldPath` for navigation.
`preflightStale` turns true after any change; `blockingIssueCount` counts
blockers and errors. Profiles: `print` (office printer, 150 dpi) and `press`
(commercial print, 300 dpi, strict).

| Issue ID | Severity | Found when | Repair action |
| --- | --- | --- | --- |
| `output-<field>` | blocker (unavailable destination), else error | the destination refuses a ticket option, pages are out of range, no file is chosen | `use-supported`, `print-all-pages`, `choose-output-file` (argument: path) |
| `output-clipped` | warning | a scaled page overruns the sheet | `fit-to-paper` |
| `output-unprintable-edge` | warning | the text box reaches the printer's unprintable edge | — |
| `output-bleed-missing` | warning | bleed is requested but the page has none | `disable-bleed` |
| `font-missing:<family>` | warning; error for `press` | a family used by text, a style or the defaults is not installed | `replace-font`: the document family, else `sans-serif` |
| `image-missing:<objectId>` | error | an image source cannot be read | `relink-image` (argument: a readable image path or URL) |
| `image-unresolved:<objectId>` | error | an `asset:` source: output reads files directly | `relink-image` |
| `image-resolution:<objectId>` | warning; error for `press` | image pixels over printed inches (at the sheet's scale) fall below the profile | — |
| `text-overset:<frameId>` | error | story text is left after the last frame | — (resize or add frames) |
| `page-overflow:<blockId>` | warning | an item that cannot break is taller than the page | — |
| `field-broken:<fieldId>` | error | a reference's target or source is gone (WP-052) | — |
| `fields-stale` | warning | field results differ from their numbering | `update-fields` |
| `equation-invalid:<objectId>` | error | an equation's source cannot be typeset | — |
| `review-markup` | warning; error for `press` | open tracked changes or comments | — (accept, reject or resolve) |

`repair(issueId, actionId, argument)` applies one repair as one undo step
(document repairs) or one ticket change, then runs preflight again. The
repair counts only if the issue ID is gone; otherwise the change is undone,
the ticket restored, and `repair` returns false with `lastError`. Issues
without a repair navigate: the host moves the view to `paragraphId`/`offset`
or `pageIndex`, or opens the frame or setting named by `targetId`.

## Bleed and page scope

`PageSetup.bleedPt` (0–72 pt) is saved as `page.bleedPt` in
`qindatk.richtext` version 2, reported as `pageSetupMm.bleedMm` and set by
`applyPageSetupMm` (0–25.4 mm) as one undo step. A ticket includes it with
`includeBleed`. Page geometry is document-wide: `applyPageSetupMm` still
refuses `scope: "section"` and `"page"` (`unsupported`), because
`RichTextLayout`, the views and the page model assume one page size (D-133).

## Verification

`tst_print_output` covers page-range syntax and validation, sheet geometry
against hand-computed fixtures (document, Letter fit and actual, A4
landscape, 200 %, bleed with marks), PDF page count and media boxes against
the plan, preview pixels equal to a raster device's job output, snapshot
isolation, the bounded cache, grayscale preview, cancellation, device
failure with affected pages and retry, refused tickets and bleed
persistence. `tst_print_preflight` drives each check and repair with known
fixtures. `tst_native_print` (with PrintSupport) lists system destinations,
prints a range through the system engine, cancels without a file and
refuses a vanished printer. `tst_print_preflight.qml` binds the controls to
an adapter. Runs on real printers, output-fidelity comparison and other
platforms are qualification work (WP-088, WP-089).
