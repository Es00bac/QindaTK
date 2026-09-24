<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Clipboard and drag/drop transfers

`TransferPayloadCodec` defines the version-1 internal MIME format
`application/x-qindatk-transfer+json`. The envelope carries a source document
UUID, exact decimal revision, one requested operation (`copy`, `move` or
`link`), and bounded items with stable entity UUIDs. Supported item kinds are
text, PNG image, range and object. The codec checks field types, canonical
IDs, version, duplicates, nesting and size before returning a payload.
Text/plain UTF-8 and image/png alternatives are advertised only for a single
validated text or PNG item; collection, range and object payloads remain in
the internal format. A transfer intent does not authorize model mutation.

`ClipboardService` uses an injectable `ClipboardAdapter`; its default adapter
talks to the Qt system clipboard. `availableFormats` and `canPaste` describe
the current clipboard. `inspectPaste(document,target,property)` validates a
destination without editing it and reports supported format, conversion
warnings or a specific malformed, missing or unsupported state. The reference
adapter accepts text into a writable `QString` property, PNG bytes into a
writable `QByteArray` property, and typed objects into a writable `QVariantList`
collection. Object copy generates fresh destination IDs. Resource-bearing or
unresolved-reference objects are rejected until an application supplies a
domain adapter that can resolve them. A supported paste changes one property
through `EditTransaction` and records one `UndoService` step.

For an object collection, `cutObjects` places a pending move on the clipboard.
Its source stays committed until paste into the same document and collection
passes validation; that paste reorders the list in one transaction. A source
revision change invalidates the pending cut. Cross-document and
cross-collection moves require a domain adapter that can commit both sides
atomically and are reported as unsupported by the reference path.

`DragDropService` coordinates a typed drag session independently of DockModel
placement gestures. A source supplies its payload and allowed operations;
an application supplies a stable `DropTarget` endpoint. The target returns a
side-effect-free preview with an opaque token and must commit its domain edit
atomically. The service checks source revision, target identity/view generation
and target document snapshot before commit, consumes each token once, and
clears transient state on Escape, rejection, view recycling or target loss.
Autoscroll is an explicit request/stop signal to the viewport adapter. A
cross-document Move is rejected unless the target advertises and previews an
atomic source-and-destination operation. The shipped
`PropertyCollectionDropTarget` copies typed objects into a `QVariantList`
property, remaps row IDs and commits one undoable transaction. Applications
implement their own `DropTarget` for richer models and OS drag adapters.

All transfers are GUI-thread operations. Applications must preserve exact
source revisions as decimal strings when they cross QML, revalidate any
external content at the destination, and pass only stable IDs rather than
delegate pointers. An unsupported format stays visible as an error; it never
falls through to an unrelated MIME representation of an internal payload.

## Public service surface

| Type | Application-facing contract |
| --- | --- |
| `TransferPayloadCodec` | `encode`, `decode` and `mimeAlternatives` enforce the version and byte/item limits; exact revisions are decimal strings on the wire. |
| `ClipboardService` | `copyText`, `copyImage`, `copyRange`, `copyObjectIds`, `cutObjectIds`, `inspectPaste`, `paste` and `rangePasteData`; `availableFormats`, `canPaste`, `cutPending`, `lastError` and `lossWarnings`; `changed` and `rejected`. C++ callers may inject a `ClipboardAdapter`. |
| `DragDropService` | `beginEncodedDrag`, `previewEncodedDrop`, `commit`, `cancel`, `escape`, `invalidateView` and `requestAutoscroll`; phase/session/source/preview/error state and committed, cancelled, rejected and autoscroll signals. |
| `DropTarget` | A domain endpoint supplies stable target/view IDs, generation, document snapshot, side-effect-free preview token, atomic commit and cancellation. It owns the actual model operation. |
| `PropertyCollectionDropTarget` | C++ reference endpoint for a writable `QVariantList` of maps with canonical UUID `id` fields; Copy only. |

`inspectPaste` returns a state of `ready`, `unsupported`, `missing`, `stale`
or `malformed`, with a message and conversion warnings. `paste` returns a
Boolean in QML and emits `rejected(message, code)` on failure; C++ callers
receive `OperationError`. A successful reference edit uses one undo entry.

<a id="ranges"></a>
## Ranges

`copyRange(documentId, revision, range, text)` places one `range` item on
the clipboard: the internal MIME format carries the domain's `range` map
(for a spreadsheet, `Workbook.rangeTransfer`'s versioned payload), and
`text/plain` carries `text`, the projection other applications paste (at
most 4 MiB of UTF-8). A range has no entity of its own, so each copy gets a
fresh item ID. The document ID must be canonical and the revision an exact
decimal. `rangePasteData()` tells a range-aware paste what it can use now:
`{kind: "range", range, sourceDocumentId, text}` for a single copied range
item, `{kind: "text", text}` for any plain text (including text copied from
another application), or `{kind: ""}`. It only reads; the domain validates
and commits the paste itself. `SheetView` uses both
([workbook.md](workbook.md#clipboard)): a range pastes formulas, formats and
merged areas into the same or another workbook as one undo step, and text
pastes tab-separated values under the view's paste policy.
