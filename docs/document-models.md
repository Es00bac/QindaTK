<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Domain document models, codecs and shared history

`DocumentSession`, `DocumentManager`, `UndoService`, recovery, workspace
restore and `FormatRegistry` work with any **domain model** through three
small C++ contracts. `PropertyDocument` (the reference property model) and
`CanvasDocument` (the spatial canvas) are the two built-in domains. A rich
text or workbook engine joins the same way; see
[adding a domain](#adding-a-domain). The public headers are
`<qindatk/document_model.h>`, `<qindatk/document_codec.h>` and
`<qindatk/recovery_journal.h>`. Decision:
[D-053](decisions-application.md#d-053--compose-domain-documents-through-a-model-a-codec-and-one-history-engine).

<a id="model"></a>
| Type | Responsibility | Thread |
| --- | --- | --- |
| `DocumentModel` | Identity, kind, `modelHeader()` (schema, document UUID, revision, reset generation), a change notification, and the prepare/publish/compose steps of its history | GUI |
| `DocumentCommand` | One recorded, immutable history step. Only the model that made it interprets it; `retainedBytes()` is charged to the undo budget | Owned by `UndoService` |
| `DocumentCodec` | Stable `schemaId()`, content recognition, bounded `inspect`/`decode`, `create`/`adopt` of a live model, and `capture` of an immutable `DocumentCapture` | `inspect`, `decode`, `DocumentCapture::encode`: any thread. `create`, `adopt`, `capture`: GUI |
| `DocumentCodecRegistry` | Thread-safe schema → codec map. `shared()` holds the built-in codecs; a `DocumentManager` can use an isolated registry | Any |
| `UndoService` | One history per document UUID behind the existing active-document facade | GUI |
| `DocumentSession` | A model plus its codec, store and recovery. `document` is the `PropertyDocument` or null; generic code uses `model`, `kind`, `schemaId`, `capture()` and `captureSnapshot()` | GUI |

## Built-in schemas

<a id="codecs"></a>
| `schemaId` | Kind | Model | File format | Suffix | Encoded bound |
| --- | --- | --- | --- | --- | --- |
| `qindatk.property-document.v1` | `property` | `PropertyDocument` | `qindatk.reference-property-document` JSON, version 2 (version 1 migrates) | `qtk` | 64 MiB |
| `qindatk.canvas-document.v1` | `canvas` | `CanvasDocument` | `qindatk.canvas` binary, version 1 ([canvas](canvas.md#native-format)) | `qtkc` | 320 MiB (`CanvasDocumentLimits`) |
| `qindatk.workbook.v1` | `workbook` | `Workbook` | `qindatk.workbook` JSON, version 2 with the session header (version 1 and header-less files open as revision 1; [workbook](workbook.md#native-file-format)) | `qtkwb` | 64 MiB (`Workbook::MaxFileBytes`) |
| `qindatk.scene3d.v1` | `scene3d` | `SceneModel3D` | `QindaTK.Scene3D` JSON, version 3 with session header; direct native version 1/2 files migrate on open ([scene](scene-3d.md#persistence-and-verification)) | `qtkscene` | 4 MiB |
| `qindatk.slides.v1` | `slides` | `SlideDeck` | `qindatk.slides` JSON, version 1 with session header ([slides](slides.md#persistence)) | `qtkslides` | 64 MiB |
| `qindatk.notebook.v1` | `notebook` | `NotebookDocument` | `QindaTK.Notebook` JSON, version 1 with the session header ([notebooks](notebooks.md#format)) | `qtknb` | 64 MiB (`Notebook::Limits::MaxFileBytes`) |

The reference format also stores a `QVariantList` property as an
`objectCollection`: at most 4,096 object rows with unique stable UUID `id`
fields, 100,000 JSON-compatible values, 24 nested levels and 16 MiB per
collection. Values are strings, booleans, finite numbers with magnitude at
most 2^53−1, maps or lists. Nulls, QObject pointers, binary assets and other
QVariant types are rejected at capture or decode. Existing format version 2
files remain readable.

`PropertyCollectionDropTarget` copies object transfer items into this native
collection with fresh destination row IDs in one `UndoService` transaction.
An explicit nested `{ "$ref": "<source-row-uuid>" }` points to another row in
the same transfer and is rewritten to its new ID, including inside maps and
lists. An external `$ref`, bare UUID string, implicit reference field or
resource field is rejected before mutation. Resource payloads need a domain
adapter with a resource store; the reference collection does not claim to
copy them. Save, reopen and recovery preserve the resulting structured rows.

`DocumentManager::createDocument(schemaId)` creates an empty session of any
registered kind; `documentTypes` lists them for QML. `openDocument(path,
schemaId)` recognizes the file **by content**, decodes it with full
validation, checks the decoded header against the adopted model and opens a
clean session. A nonempty `schemaId` must match. `openReferenceDocument`
is `openDocument` restricted to the property schema. `adoptBytes` turns
native bytes (a finished import job) into a new unsaved session.

## Commit, undo and redo

<a id="commands"></a>
A model changes content in two phases:

1. `prepareCommand(step, forward)` validates the whole step against the
   current state. It never mutates and never notifies.
2. `publishCommand(step, forward)` applies the whole step, increments the
   revision once and then notifies. It cannot fail after a successful prepare
   on the same thread.

`UndoService::commit(model, label, mergeKey, step)` prepares the step, checks
its `retainedBytes()` (and a macro's composition) against `memoryLimit`,
records it and only then publishes. An observer of the model's notification
therefore already sees the new cursor. A rejection is **returned, not
emitted**. Model, revision, damage, history cursor and notifications stay
unchanged. `undo(model)` and `redo(model)` prepare in the requested direction
and move the cursor only after that succeeds. The QML facade (`undo()`,
`redo()`, `canUndo`, `historyEntries`, macros) works on the active model,
whatever its kind. While a step publishes, another history operation on the
same document is refused ("A history operation is in progress").

`composeCommands(first, second)` returns one step equal to both. Macros
(`beginMacro`/`endMacro`/`cancelMacro`) and merge keys use it. A model that
returns null cannot group those steps: an edit inside a macro is then
rejected, and merging is skipped. A step whose `isNoOp()` is true, such as a
macro whose edits cancel out, is never recorded; cancelling such a macro just
closes it, and a merge that would net to nothing keeps both steps. A history entry keeps its `entryId` when a
merge extends it, so history navigation stays stable. A per-entry merge count
lets a pending save detect that the entry changed.

`PropertyDocument` records `PropertyCommand` steps (typed before/after values
by entity ID); `EditTransaction` keeps its preview and notification rules.
`CanvasDocument` records its `CanvasEdit`: touched-object deltas plus layer,
image and page deltas, with image pointers shared with the document. A step
never holds a whole-scene copy. Moving one object of a 100,000-object canvas
retains about 600 bytes
([evidence](universal-toolkit/implementation/evidence/WP-033-domain-sessions.md)).
`Workbook` records `WorkbookCommand`s: the mutations one operation or
transaction applied and their exact inverses. A step holds only the cells,
feature sets and names it changed ([workbook](workbook.md#transactions-history-and-revision)).
`SceneModel3D` records bounded before/after scene snapshots (at most 512
primitives) through the same service. A standalone scene owns a private
64-step service; a session adopts it. Gesture previews remain transient and
commit as one step. This snapshot representation is suitable for the bounded
primitive model, not a large imported mesh.

## One live history per model

`UndoService::addModel` (and `addDocument`) **refuses** a model whose live
history belongs to another service. Two services recording one model would
each replay inverse data the other had already changed. `adoptModel` moves
the history instead: its steps, cursor, clean position and step limit.
`DocumentSession` adopts its model. A standalone `CanvasDocument` keeps a
private service (256 steps, a budget large enough never to refuse an
ordinary edit). When a session adopts the canvas, that history moves into the
session's shared service and continues there under its byte budget.

A model calls `invalidateHistory()` after a reset or structural change, when
its recorded steps can no longer apply. It calls `invalidateCleanState()`
after unrecorded content changes, such as a fixture load or a direct insert,
because the saved state is then unreachable by undo/redo.

## Save capture, receipts and clean marks

<a id="saving"></a>
A save captures an immutable `DocumentCapture` on the GUI thread. For a canvas
this is O(1): hashes of value objects and immutable image tiles are shared.
The next edit detaches the document's own copy. A worker encodes the capture,
appends the recovery frame and commits the file through the store's
`QSaveFile` replacement. Only the receipt returns to the GUI thread. The
receipt must describe the captured header, or it is refused.

If the model's revision and reset generation still match the capture, the
current position becomes clean and recovery is discarded. Otherwise the
receipt is **stale**. The file changed, so the save baseline moves, but only
the captured history position (`UndoService::CleanMark`) becomes clean. The
session stays dirty and recovery is kept. Undoing back to exactly that
position makes it clean. A reset, unrecorded content, a merge into the marked
entry, eviction or a branch make the mark unmatchable, and the session then
stays dirty until the next save.

## Recovery frames

<a id="recovery-frames"></a>
`RecoveryJournal` is a thread-safe handle on the recovery directory. Workers
append through it. All copies share one lock and one discard epoch per
document. An append captured before a discard is therefore refused, and a
newer frame is never overtaken by an older one. New frames are version 2:

| Field | Bytes |
| --- | --- |
| magic `QTKR`, version 2, header length | 12 |
| schema ID length, schema ID (UTF-8, at most 255 bytes) | 2 + n |
| document UUID, revision, reset generation, payload length | 16 + 8 + 8 + 8 |
| payload SHA-256 | 32 |
| header SHA-256 (of everything above) | 32 |
| payload | length |

A scan accepts a frame only as a whole: both hashes, the expected document,
one schema per journal, no revision regress and, when a codec is registered,
`inspect(payload)` equal to the frame header. Version-1 reference frames from
older builds stay readable. A frame with no registered codec is still
hash-validated and offered (`codecAvailable: false`), but cannot be restored
by that build. Inspecting and restoring never modify recovery files. A decode
failure, such as an unavailable codec or an oversized decoded resource,
reports an error and keeps the files. When no valid frame remains, damaged
files are kept beside the journal as `.corrupt` instead of being deleted.

## Adding a domain

<a id="adding-a-domain"></a>
A domain engine with its own history (for example the rich text or workbook
lanes) adopts the boundary in five steps:

1. Implement `DocumentModel` on the model, or on a QObject wrapper that owns
   it. Return a stable `modelKind()` and a `modelHeader()` whose `schemaId`
   names the file schema. Connect `connectModelChanged` to the model's
   `changed()`.
2. Replace the local undo stack with `DocumentCommand` steps. Validate in
   `prepareCommand`, apply in `publishCommand` (one revision), and group in
   `composeCommands` if the domain supports macros. Keep a private
   `UndoService` for standalone use, as `CanvasDocument` does. A session then
   moves that history rather than duplicating it.
3. Provide a `DocumentCodec`: recognize the file by signature, bound every
   allocation in `decode` before making it, and check the decoded identity,
   references and counters. Only then return plain `DecodedDocument` data for
   `adopt`.
4. Register the codec in `DocumentCodecRegistry::shared()` at startup, or
   give the application's `DocumentManager` an isolated registry. Recovery,
   workspace restore and `adoptImport` then work unchanged.
5. Optionally publish a `FormatProvider` whose `schemas` metadata lists the
   schema, so export dialogs refuse other documents before any job runs.

The workbook (WP-055) follows these steps: `Workbook` is the model, its
`WorkbookCommand` steps hold the applied mutations and their exact inverses,
and `workbookDocumentCodec()` is a built-in codec
([workbook.md](workbook.md#sessions)). Rich text still owns a local history.

## Limits

- Autosave and save capture the whole document, debounced by the session's
  `autosaveDelayMs`. Recovery frames are full snapshots, not deltas;
  per-gesture history is delta-only.
- Canvas image tiles are stored uncompressed. `qUncompress` does not bound
  output to its declared size (a 36 KB blob declaring 1 KB inflated to 8 MB),
  so compressed tiles would need a bounded inflater first.
- `GestureRouter`'s Edit intent still targets `PropertyDocument`
  transactions; canvas gestures go through `ToolController`.
