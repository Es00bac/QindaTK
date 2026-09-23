<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Application decisions

Continuation of [decisions.md](decisions.md); [service decisions](decisions-services.md) contain D-030 through D-039.

## D-026 — Keep application contracts typed and optional engines explicit

**Context.** Later document, creative, Office and media packages need stable
identity, exact time/revision values and operation availability across QML and
C++ threads. A QML `Number` cannot represent every 64-bit tick or revision,
and linking every optional engine into the core module would make simple
control consumers pay for unused backends.

**Decision.** Publish distinct C++ entity/document IDs, decimal-string QML
codecs for exact integers, versioned immutable snapshot headers, cancellation
tokens and reset generations in the core library. QObject models and the
capability registry stay on their owning GUI thread; workers receive immutable
snapshots and check revision/generation before results are applied.
`CapabilityRegistry` accepts explicit lightweight operation manifests, never
loads a backend during query, and chooses a deterministic available provider
with a specific disabled reason and provenance. Optional domain packs are
separate selected targets and register themselves with an application-owned
registry. The base `QindaTK` import and C++ library link no such engine.

**Consequences.** Existing controls remain usable without domain packages.
QML exchanges large revisions and ticks as strings, and applications must
resolve IDs again after model resets. The first optional pack owns its
plugin/discovery and installed-package policy, with a core-only consumer as
a regression gate. See [application contracts](application-contracts.md).

## D-027 — Route semantic commands through scoped handlers

**Context.** Menus, toolbars and the palette can present the same semantic
operation, but independent callbacks can target the wrong document after a
focus or document switch. Handlers owned by a closed view must not stay live.

**Decision.** Give each operation a stable ID and descriptor. An
application-owned `CommandContext` stores scope instances with opaque tokens,
parent links, explicit activation, typed snapshot values and weak QObject
owners. `CommandRegistry` resolves a handler from the active path, most
specific first; a successful invocation captures a `CommandInvocationContext`
and calls one handler. Removing a scope subtree or its owner invalidates the
corresponding handlers. Keep descriptors global to the registry and register
handlers against live scope tokens. State callbacks report presentation
state; callers invalidate the registry when their inputs change.

Use `CommandBinding` and the current `MenuItem`, `CommandButton` and
`CommandPalette` adapters to connect presentations. `ToolBar` remains a
container; direct `Button`, unbound `MenuItem` and caller-fed palette behavior
remain available. `ShortcutMap` routes portable key sequences through a
window event filter, ranks active scope bindings above global bindings,
reports same-rank ambiguities, supports timed chords and leaves ordinary
text-editing input and IME composition to the editor by default. `FocusManager`
records weak item/window references for transient surfaces and restores only
when focus remains in the closing surface tree or has been cleared.

**Consequences.** A descriptor alone is not invokable: the active path needs a
live handler whose state is enabled. Applications own scope activation,
window attachment, shortcut persistence and conflict handling. Current focus
restoration is item-based and guarded against focus theft; it does not yet
store a stable logical focus identifier or automatically choose a document
editor target. Text input that opts into a shortcut can intentionally consume
typing keys, and system/third-party shortcut conflicts are outside the map.
See [commands.md](commands.md) for APIs and remaining integration limits.

## D-028 — Keep edit previews transient and history keyed by document identity

**Context.** Numeric fields, drag handles and commands can all change the same
model property. Per-widget history would make a drag produce many undo steps,
could commit a partly updated multi-selection and might replay inverse data
against a recycled delegate after sorting or model reset.

**Decision.** `PropertyDocument` is a small authoritative reference model with
typed descriptors, stable entity IDs, revision and reset generation.
`EditTransaction` captures original values, previews without advancing the
revision, validates every target and commits an atomic multi-target change.
Cancel or rejection removes the preview. `UndoService` keeps separate histories
per document, records one step per committed transaction, merges only across
matching keys without crossing a saved clean cursor, and applies a bounded
inverse-data budget. Structural or metadata mutations outside this value-edit
history invalidate that document's history rather than risk stale replay.
`SelectionModel` stores IDs rather than row positions and resolves survivors
after reset. `PropertyBinding` separates owner synchronization from accepted
user edits; `ValidationModel` accepts only results tagged with the current
run and snapshot.

**Consequences.** A controller must keep one transaction alive for an entire
gesture and explicitly cancel it on Escape or lost grab. A domain wanting
undoable structure changes or concurrent overlapping previews must implement
the corresponding model adapter; the reference model conservatively clears
history on such changes. QML exact 64-bit values use decimal strings. See
[editing.md](editing.md) for APIs and behavior.

## D-029 — Capture immutable save revisions and keep recovery separate

**Context.** A file write can outlive its initiating edit. Later edits,
external replacement and process interruption must not imply a current save.

**Decision.** `DocumentSession` captures immutable bytes on the GUI thread.
`DocumentStore` writes on a worker; the local adapter uses `QSaveFile` and a
last-known file hash. Only a receipt matching the live revision and extension
generation marks undo clean or discards recovery. `RecoveryManager` validates
hashed frames in a separate journal/checkpoint pair keyed by document UUID.
Restored content stays dirty. `DocumentManager` keeps one session per document
ID and canonical path while multiple views share its undo history.

**Consequences.** Save As can require explicit overwrite. Close with Save
waits for completion and remains blocked by newer edits. Recovery never
silently replaces the original file. Hash checks are not an interprocess lock.
The reference codec's supported types and limits are in
[persistence.md](persistence.md); richer formats need their own adapter.

## D-040 — Keep sparse grid edits authoritative and bridge shared undo explicitly

**Context.** A two-axis grid can have millions of addressable cells. Storing a
`PropertyDocument` entity per cell or `SelectionModel` ID per selected cell
would materialize data that a virtual view never shows. `UndoService` records
`PropertyDocument` property changes, not arbitrary grid ranges. A second
implicit history under a shared Undo command could replay one edit twice.

**Decision.** `EditableGridModel` is a `QAbstractTableModel` with stable row
and column IDs, a lazy baseline source, sparse committed overrides, compact
range endpoints, and bounded visible cell queries. It validates a whole range
before atomic commit and records a single local inverse step by default.
An explicit `externalUndo` mode disables local navigation. The host records
the emitted transaction UUID and full change set in its document history and
replays it through a guarded grid history method. Reorder and insertion keep
ID-based history; removal invalidates history to avoid stale targets.

**Consequences.** Hosts that use shared undo must choose external mode and
release retired transaction IDs. The grid bridge checks transaction
provenance, current revision, identity generation and cell values before
applying a whole step. `PropertyDocument` and `UndoService` remain unchanged.
The view and domain must supply accessible virtual cells, labels, persistence
and formula evaluation. See [editable-grid.md](editable-grid.md).

## D-041 — Carry typed query plans and source provenance across pivot refreshes

**Context.** Filters and pivots must distinguish null from errors, preserve
selection identities when source order changes, and avoid stale results after
edits or schema changes. A large or remote dataset cannot be copied into a
QML array for client-side aggregation.

**Decision.** A versioned `QuerySpec` stores typed predicates, parameters,
field IDs, schema/source revisions and aggregate descriptors. `QuerySource`
negotiates capabilities and supplies projected rows or remote results. The
bounded local engine uses three-valued filter logic, hashes typed dimension
values for stable row/column IDs, and stores contributing source row IDs per
aggregate cell. Refresh rejection is atomic on error, cancellation, revision
change or limit breach. A completed pivot is a read-only presentation snapshot
when adapted to `EditableGridModel`. The QML controller gathers a bounded
snapshot in GUI-thread chunks, executes against that immutable copy on a
worker, and publishes only if source revision and request generation match.

**Consequences.** Hosts own source snapshots and remote execution, must
increment schema revisions on metadata changes, and must check query request
identity before publishing results. Exact decimal finance, joins, sorting and
incremental aggregation need later provider implementations. See
[query-pivot.md](query-pivot.md).

## D-044 — Keep timeline placement in an exact tick model and views delegate-free

**Context.** An edit timeline must move, trim, split and ripple thousands of
clips with sample- and frame-exact results, undo each gesture exactly, and
stay responsive with 10,000 clips. Floating-point pixel positions drift under
zoom; QML Numbers round above 2⁵³; delegates recycled while scrolling cannot
own a gesture or authoritative state. `UndoService` records `PropertyDocument`
property changes and invalidates history on structural edits such as a split.

**Decision.** `TimelineModel` stores tracks, clips and markers by UUID with
signed 64-bit ticks in a declared rational timebase (705,600,000 ticks per
second by default, which divides common sample rates and frame durations).
A pure planner resolves typed intents under an explicit `TimelineEditPolicy`
into complete change sets; one session previews at a time without touching
committed state, and a revision-guarded commit applies one change set as one
history step with precondition checks on undo and replay. The model owns a
local history by default and offers the D-040 external-history bridge.
`TimelineViewport` maps exact ticks to pixels from integer differences and
diffs bounded delegate rows by key; views route gestures through its hit
tests from one pointer controller per surface (clip lanes, header column,
marker lane), never from a recycled delegate, and every exit path of a drag
ends its session exactly once. Media previews use a cancellable,
stale-checked provider seam, and transfers use the WP-016 payload and
drop-target contracts.

**Consequences.** Hosts with their own session model either project into
`TimelineModel` or take its history through `externalUndo`; domain edit rules
(overwrite, crossfades, beat-locked clips) are policy subclasses, not view
code. Snapping is a time-axis provider until WP-041's spatial SnapModel
exists. See [edit-timeline.md](edit-timeline.md).
