<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Reversible editing, property state and selection

WP-012 supplies a reference editing path above the existing input controls.
`PropertyDocument` is a small authoritative, typed model for examples and
adapters. An application domain may keep its own model, but it must preserve
the same ID, revision, validation, atomic commit and inverse-data rules when
connecting to the controls. The public headers are
`<qindatk/property_document.h>`, `<qindatk/edit_transaction.h>`,
`<qindatk/undo_service.h>`, `<qindatk/property_binding.h>`,
`<qindatk/selection_model.h>` and `<qindatk/validation_model.h>`.

## Services and ownership

| Type | Owns and exposes | Important boundary |
| --- | --- | --- |
| `PropertyDocument` | Document UUID, monotonic revision/reset generation, typed property descriptors, entity values and one transient preview | Stores by `EntityId`, not row or delegate; committed values change only atomically. Set descriptors before adding entities. |
| `EditTransaction` | One begin → preview updates → validate → commit/cancel operation | Controllers own it across a gesture. Preview does not increment revision or history; Escape or lost grab calls `cancel()`. |
| `UndoService` | Independent histories keyed by document ID for any [domain model](document-models.md), active-document facade, clean cursor/revision, merge keys, macro groups and byte budget | The application registers documents, sets the active document and marks clean only after a successful save. A model has one live history: a second service cannot record it. |
| `PropertyBinding` | Inspector projection for one descriptor and stable target IDs: value, mixed/default, writable, animated, error and edit methods | Owner refresh and document synchronization emit `changed`, never `userEditAccepted`. User requests go through `EditTransaction`. |
| `SelectionModel` | Selected/current/anchor entity IDs and reset generation | View order is supplied only for a range operation; sorting and filtering never rewrite IDs. |
| `ValidationModel` | Snapshot-tagged pending/error/warning/info issues and first relevant target/path | Results from an old run, revision or reset generation cannot replace current feedback. |

All six are GUI-thread QObjects. Asynchronous validators receive copied
`SnapshotHeader`/values and return ID-tagged results to the GUI thread. No
worker retains a model, control, delegate or transaction pointer. Entity and
document IDs are UUID strings in QML. Revision, reset generation and run ID
are decimal strings, so JavaScript `Number` never rounds them.

## Reference document and transaction

Define a `PropertyDescriptor` with `id`, exact `QMetaType`, typed default,
optional unit ID, writable flag and animatable flag. `addEntity` accepts only
known properties with the declared type and fills unspecified properties from
their defaults. `setTargetWritable` and `setTargetAnimated` apply per-entity
state. A multi-target edit preflights every target before it changes any of
them; one read-only, deleted or incompatible target rejects the whole edit.
`value()` includes the active preview; `committedValue()` never does.

```cpp
QindaTK::PropertyDocument document;
document.defineProperty({"gain", QMetaType::fromType<double>(),
                         0.0, "dB", true, true});
const auto track = QindaTK::EntityId::create();
document.addEntity(track, {{"gain", 0.0}});

QindaTK::UndoService undo;
undo.addDocument(&document);
undo.setActiveDocument(&document);
undo.markClean(); // call after a successful save, not after every edit

QindaTK::EditTransaction drag;
drag.setDocument(&document);
drag.setUndoService(&undo);
drag.begin({track}, "gain", "Adjust gain");
drag.updateValue(3.0); // repeat as the pointer moves; transient preview
drag.updateValue(4.0);
drag.commitEdit();      // one revision and one undo entry
```

`begin` captures original values and the document snapshot. `updateValue`
validates type and writability, then publishes a transient preview. A domain
can set a validator callback to check constraints spanning all targets on
commit. Commit checks that revision and reset generation still match, runs
validation, checks inverse-data capacity, discards preview, applies every
value and records one inverse step. A rejected commit or `cancel()` discards
the preview and preserves the committed values. A changed document revision
returns `StaleRevision`; a model reset returns `StaleGeneration`. The
controller should retire that transaction and resolve its target IDs again.

Call `cancel()` on Escape, pointer-grab loss, an abandoned popup or controller
destruction. Do not create a transaction for each pointer movement. A field,
handle and command can each call the same `begin`/`update`/`commit` methods;
they differ only in their interaction event source. The reference document
allows one preview at a time. Applications needing overlapping independent
edits must provide a domain adapter with conflict handling.

The QML `EditTransaction` properties are `document`, `undoService`,
`targetIds`, `propertyId`, `label`, `mergeKey`, `active`, `transactionId`,
`errorMessage` and `errorCode`. Its invokables are `begin()`, `update(value)`,
`commit()` and `cancel()`, with `committed`, `canceled` and `rejected` signals.
The C++ overload accepts typed `EntityId` targets and returns `OperationError`
from update/commit. A default `OperationError` means success.

## Inspector binding

`PropertyBinding` takes `document`, `undoService`, `propertyId` and
`targetIds`. It exposes `descriptor` (`id`, `typeName`, `typeId`, `unitId`,
`defaultValue`, `writable`, `animatable`), `value`, `defaultValue`, `mixed`,
`writable`, `animated`, `validationError`, `availabilityError`, `editing`,
`requiresExactString` and `exactIntegerValue`. A mixed selection has no
ordinary aggregate `value`; it stays mixed until an accepted user edit.
`begin()`, `update(value)`, `commit()`, `cancel()` and `reset()` all use one
transaction. `reset()` submits the typed descriptor default as an edit.
`userEditAccepted(propertyId, targetIds, value)` is separate from owner
`changed` notifications; `userEditRejected(message, code)` carries failures.

For 64-bit integer descriptors, QML must display `exactIntegerValue` and call
`updateExactInteger(decimalString)`. Passing such a value through the ordinary
QML numeric `value` path can lose precision above 2^53. For other types,
controls should bind their owner value to the projected state and call the
binding's edit methods only from user interaction signals. `NumberField`
exposes `valueModified`; `Slider` exposes `valueModified`; a command handler
can use the same transaction service from C++. Continuous controls need a
controller that calls `begin()` on gesture start, `update()` for previews,
`commit()` at gesture end and `cancel()` on grab loss. The controls themselves
do not own document undo history.

## Undo, clean state and structural changes

`UndoService` keeps one history per registered document, keyed by its
document UUID. The model is a `PropertyDocument`, a `CanvasDocument` or any
[domain model](document-models.md#commands). It exposes `canUndo`, `canRedo`,
`undoText`, `redoText`, `clean`, `cleanRevision`, `memoryLimit` and
`lastError` for the active model: `activeDocument` when that is a
`PropertyDocument`, and `activeModel` for any kind. `undo()` and `redo()`
validate the recorded step against the model before anything changes, so a
refused step leaves values, revision and cursor untouched. `markClean()`
stores the current history cursor and revision after a successful save.
Returning to that cursor through undo makes the document clean, even though
its monotonic revision has advanced. Branching after undo discards redo
steps; a discarded saved cursor stays dirty. A save that finishes after newer
edits marks clean only the history position it captured.

`addDocument`/`addModel` refuse a document whose live history already
belongs to another `UndoService`; `adoptModel` moves it instead. A
`DocumentSession` does this. Two services recording one document would each
replay inverse data the other had already changed.

A nonempty `mergeKey` coalesces adjacent edits with the same key when they do
not cross the clean cursor. The merged entry keeps its `entryId`. Macros
(`beginMacro(label)`/`endMacro()`) group consecutive edits into one step;
`cancelMacro()` rolls them back. The byte budget defaults
to 8 MiB and can be set with an exact decimal `memoryLimit` string or C++
`setMemoryLimit(bytes)`. Oversized edits are rejected before commit so an edit
is never silently nonundoable. Old entries are evicted first when the budget
is reduced or a new step is recorded; evicting the saved cursor invalidates
clean tracking. The byte count is an estimate of what each step keeps alive
beyond the live document (inverse values, removed resources), not a
serialized size guarantee.

Adding/removing entities, resetting the model and changing target writable or
animation metadata invalidate this reference history and its clean cursor.
This conservative rule prevents undo from targeting a deleted/recreated ID or
replaying inverse data against a different schema. A richer domain model may
record structural edits as first-class inverse steps instead; the canvas
records layer, image and page changes that way. `removeDocument`
and QObject destruction retire a history; the active document must be set to
another live session by the application.

<a id="history-participants"></a>
## History participants

A domain model that keeps its own exact history, such as `Tk.Workbook`, does
not register values with `UndoService`. It implements `UndoParticipant`
(`<qindatk/application_contracts.h>`): `canUndo`, `canRedo`, `undoText`,
`redoText`, `isModified`, `undo`, `redo` and `markSaved`, and its QObject
emits `historyChanged()` when any of them changes. Setting
`UndoService.activeParticipant` to that object routes the facade to it:
`canUndo`, `canRedo`, `undoText`, `redoText`, `clean` (`!isModified()`),
`undo()`, `redo()` and `markClean()`, with `changed` forwarded from
`historyChanged()`. A QObject that is not a participant is refused (“Not an
undo participant”) and the previous participant stays. While a participant
is active, the property-document history is not shown (`historyEntries` is
empty) and macros are refused; the participant groups its own steps (the
Workbook's transactions). Set `activeParticipant` to null to return to
`activeDocument`. The host switches it with the active view, so menu,
shortcut and toolbar commands reach the document the user is working in.

## Selection and validation

`SelectionModel` stores UUID-backed `selectedIds`, `currentId`, `anchorId`
and exact decimal `generation`. `select`, `toggle`, `extendTo`, and `clear`
are user operations; they emit `selectionModified` only when the state changes.
`extendTo(targetId, orderedVisibleIds)` uses the current visible order to
choose an inclusive range, then stores the resulting IDs. `reconcileLiveIds`
removes deleted IDs using the complete authoritative ID set, preserving
survivors through sort and filter changes; never pass only currently visible
rows if hidden objects remain alive. `resetModel(allIds, nextGeneration)`
requires a strictly newer decimal generation, resolves surviving IDs against
the replacement model and drops deleted IDs. Reconciliation/reset emit state
notifications without implying a user selection operation.

Set `ValidationModel`'s snapshot before a run, then call `beginRun()` and
retain its decimal token. `completeRun()` must return that token plus the
document ID, revision and reset generation actually checked. A typed C++
completion also carries the schema ID in `SnapshotHeader`. Only the current
pending run for the current snapshot can publish issues. Each issue has a
stable code, `error`/`warning`/`info` severity, optional target ID, field
path, message and remedy. `pending` and `invalid` are distinct; `firstIssue`
prefers an error and exposes `firstIssueTargetId`/`firstIssuePath` for focus
navigation. `clear()` discards feedback and makes late completions inert.

The reference model validates type and writability. Application validators
must add domain constraints such as numeric ranges, linked-resource checks,
cross-property rules and localized messages. A color, audio or spreadsheet
engine keeps its own precision, storage and domain undo data behind this
service contract.
