<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Format registry and import/export jobs

`Tk.FormatRegistry` is the operation-specific format broker for reference
documents. It ships two providers: `qindatk.reference.v2` and `text.utf8`.
Neither provider recognizes or edits Adobe, Office, audio, video or other
vendor formats. A file extension alone never establishes support.

## Public workflow

| Step | Import | Export |
| --- | --- | --- |
| Discover | `formatsForOperation("import")` or `detectImport(source)` | `formatsForOperation("export")` |
| Inspect | `preflightImport(source, formatId, options)` | `preflightExport(document, destination, formatId, options)` or `preflightExportSession(session, ...)` |
| Start | `startImport(source, formatId, plan.options)` | `startExport(document, destination, formatId, plan.options)` or `startExportSession(session, ...)` |
| Observe | `jobs.job(jobId)` and `jobs.jobFinished` | Same; inspect the result receipt before declaring success |
| Adopt | `adoptImport(manager, jobId)` after success | The committed output file is the result; exporting does not make the document clean |

The `formats` property lists provider metadata, including `formatId`,
extensions, supported operations, editable import, render-only and roundtrip
claims. `formatsForOperation` filters by the named operation; a provider's
ability to recognize a file does not imply editable import, rendering or
roundtrip support. A successful preflight returns `ok`, `operation`,
`formatId`, `options`, `warnings`, `losses`, `requiresAcknowledgement` and a
bounded `preview`. An unsuccessful plan includes an error. Show warnings and
losses before starting the job. Explicit `allowLossy: true` is required for a
lossy export; do not infer consent from the selected format.

Pass the returned `plan.options` unchanged to the corresponding start call.
Preflight records an `expectedSourceSha256` for imports, an
`expectedSourceRevision` for property-document exports, an
`expectedSnapshotSha256` for session exports, and `expectedSha256` when
`conflictPolicy: "replaceIfHash"` is selected. Start rejects a source,
document or destination changed since approval. The default output conflict
policy refuses an existing file. A replace request without the matching hash
does not overwrite it. Local file URLs are supported; remote URLs are rejected.

`startImport` returns a job ID; the worker produces validated reference bytes
without mutating any open document. Call `adoptImport` only after the job state
is `succeeded` and the user has chosen to open it. That creates a
`DocumentSession` in the chosen `DocumentManager`. The job can be cancelled
through `registry.jobs.cancelJob(jobId)`. A completed job exposes `result`,
`warnings`, `losses` and errors through `jobs.job(jobId)`; its status key is
`state` (`queued`, `running`, `succeeded`, `failed`, or `cancelled`). A
`committed` result says the output replacement completed, including when a
cancellation request arrived after the commit boundary. Failed results retain
structured diagnostics. Do not treat a returned job ID as a successful import
or file write.

## Provider limits

| Provider | Supported behavior | Deliberate boundary |
| --- | --- | --- |
| `qindatk.reference.v2` | Validated import and export of the version 2 `PropertyDocument` reference schema; typed values and `extensions` roundtrip | This is a reference model, not a generic creative or office document interchange format |
| `text.utf8` | Strict UTF-8 import with optional BOM; export a single text value with selected BOM and line-ending policy | Properties or entities outside the representable text body are reported as losses and need explicit acknowledgement |

Malformed or oversized source files fail preflight. A file with an unrelated
signature is not imported because its name happens to use a known extension.
Text previews are bounded to 500 characters. Native previews report bounded
metadata such as entity count. Missing linked local resources are warnings,
not automatically loaded assets. Providers receive immutable bytes or
snapshots; they cannot read or mutate live QObjects from worker threads.

<a id="snapshot-sources"></a>
## Snapshot sources

A document model that is not a `PropertyDocument` exports through the same
jobs by implementing `SnapshotSource` (`<qindatk/application_contracts.h>`):
`exportSnapshot()` returns an immutable `SnapshotPtr` of its committed state
on the GUI thread. `preflightExportSource(source, destination, formatId,
options)` and `startExportSource(…)` accept any QObject that is one (QML
passes the model itself, for example a `Tk.Workbook`). C++ callers may pass
a snapshot directly to `preflightSnapshot`/`startExportSnapshot`. The plan
records `expectedSnapshotSha256`, so a start with a model that changed since
preflight is refused, like a session export. A QObject that is not a
snapshot source gets “Not a snapshot source”. The provider must accept the
snapshot's `schemaId`: the workbook providers (`qindatk.workbook.v1`,
`text.csv`, `text.tsv`, added by `Workbook.registerFormat`) refuse other
snapshots, and the reference providers refuse workbooks. Imports from those
providers are adopted by the model (`Workbook.adoptImport(registry, jobId)`)
rather than as a `DocumentSession`; see
[workbook.md](workbook.md#interchange).

The reference file's schema and save/recovery lifecycle are documented in
[persistence](persistence.md). `FormatRegistry` export is a separate operation
from `DocumentSession.requestSave`: it does not alter the session URI, undo
clean state, autosave journal or save conflict baseline.
