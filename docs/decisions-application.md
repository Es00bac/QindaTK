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

## D-042 — Bound event streams by count and payload

**Context.** A high-rate event source can outlive the visible window and can
submit unusually large text fields. Bounding only the number of rows leaves a
large per-row payload capable of exhausting memory. A paused view must also
remain an inspection view, not a reason to stop ingestion or grow history.

**Decision.** EventLogModel retains a finite row count and UTF-8 payload budget,
and stages each append batch under a separate byte ceiling. It rejects an
oversized or malformed record whole. Producers use one batch API call for a
burst. Filtering is synchronous over the bounded window. View pause records a
stable visible event ID and offset; it never pauses ingestion or retention.
CSV returns bounded data for the host to route through its own export job.

**Consequences.** Accepted records remain intact and source IDs remain stable
through filtering and eviction of other rows. Hosts can detect rejection from
the accepted count and choose a durable export path. This policy applies to
the initial EventLogModel; remote log storage, paging, and time-indexed query
engines need a separate provider contract.

## D-043 — Diff presentation uses immutable revision snapshots

**Context.** A comparison may contain a few edited lines or tens of thousands
of structured regions. The view must never commit a merge while rendering or
allow a queued decision to target a later document revision.

**Decision.** DiffModel uses bounded deterministic alignment for small local
text snapshots and an indexed DiffPageProvider for large or structured
comparisons. Both carry opaque source revisions and stable hunk/conflict IDs.
DiffView reads visible regions only. Accept/reject sends a typed DiffDecision
with expected revisions to a host-owned DiffTransactionAdapter; the host
performs the transaction and publishes a new snapshot.

**Consequences.** Large providers own their alignment and ID stability. A
destroyed provider or changed snapshot cancels pending requests. Hosts must
recheck revisions at commit time and keep cancellation reversible. The toolkit
never mutates the source documents.

## D-044 — Tile spatial documents in document space behind one render boundary

**Context.** A spatial editor must pan, zoom and rotate a large document in
several synchronized views, and pick and edit it in stable document
coordinates. It must render through both Qt's software adaptation (headless
CI, llvmpipe) and a GPU scene graph. Qt Quick's render thread may tear down
every GPU handle at any time: on a backend change, a window change or device
loss. Tool previews must never leak into document state or undo.

**Decision.** A view is a pose `{center, zoom, rotation}` held in a shareable
`CanvasViewState`. Views that share one state are synchronized, and
navigation never edits the document. The document is reached only through
`CanvasDocumentAdapter`. Its only mutation is an all-or-nothing `commit` of a
`CanvasEdit` that rejects stale `before` states and reports damage in
document coordinates. `ToolController` owns one gesture at a time. Previews
stay in the tool; `end` commits exactly one edit, and every interruption
cancels without touching the document. Rendering tiles the document **in
document space** at a power-of-two LOD (`ceil(log2(zoom·dpr))`), so
rotation and fractional zoom are one transform and tiles survive pans and
turns. `RenderSurfaceAdapter` owns planning, damage, a per-frame budget and a
byte-bounded LRU cache of CPU-side tile content (software images or
scene-graph vertex runs). It publishes an immutable frame. The render thread
reads only that frame. It keeps every GPU handle inside the item's node tree
and rebuilds the tree whenever the resource generation changes.

**Consequences.** No QML object ever owns a render-thread resource, so Qt's
teardown releases handles on the right thread, and context loss costs one
re-upload from the CPU cache. The software adaptation cannot draw custom
geometry, so the scene-graph provider falls back there and reports why.
Picking tolerance is in logical pixels (`px / zoom`). Degenerate objects are
kept but neither drawn nor picked. `CanvasDocument` records its edits in the
shared history engine and persists through the document session boundary of
[D-053](#d-053--compose-domain-documents-through-a-model-a-codec-and-one-history-engine).
Snapping, selection handles and guides (WP-041) and colour transforms
(WP-043) plug into the published contracts. See [canvas.md](canvas.md).

## D-045 — Keep timeline placement in an exact tick model and views delegate-free

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

**Dependency reconciliation.** WP-041's spatial selection geometry and
SnapModel are not consumed by this one-dimensional timeline. The exact-tick
`TimelineSnapper` already fulfills the timeline snap contract, so WP-041 is
not an implementation prerequisite for WP-048. The `TimelineMediaBackend`
contract deliberately leaves real audio/video decoding to hosts and the
later audio/video integration packages; the synthetic backend is identified
as a fixture. WP-048 acceptance checks request, cancellation, stale-result
and view behavior at that boundary, not a codec implementation.

## D-046 — Query spatial snaps from raw document coordinates

**Context.** Spatial gestures need guides, object geometry and grids to agree
across rotated and zoomed views. A snap chosen in document units alone changes
its apparent pointer tolerance under zoom. Reusing a previously snapped point
as the next drag input causes a jump when snapping is toggled off.

**Decision.** `SnapModel` takes the raw document-space pointer point on each
query, a shareable `CanvasViewState` for pixel tolerance, optional stable-ID
guide records, and a `CanvasDocumentAdapter` for indexed visible objects.
Visible guides outrank object geometry, which outranks grid lines; distance
and stable provenance break ties within a priority. The result is a point and
per-axis provenance, with no document mutation or retained gesture state.
Dragged object IDs are excluded by the host. The host chooses group anchors,
validates constraints and commits one edit through the document adapter.

**Consequences.** Disabling snapping immediately returns the raw pointer
position. Different views can share the same guide/document data while their
own zoom controls tolerance in logical pixels. Guide storage and transactions
remain a WP-041 domain responsibility; this decision does not make a
one-point snap query a complete transform controller.

## D-050 — Own the rich-text model and paginate per view

**Context.** WP-050 needs one authoritative rich-text document shown as a
continuous editor and as pages, local reflow after an edit on a 500-page
document, and stable page identities. `QTextDocument` has one document
layout, so two views of different widths cannot share it. A scratch probe
(Qt 6.11.1, 508 pages, 5,201 blocks) counted `QTextDocumentLayout` block
layouts after one edit, from the `qt.text.layout` category:

| Edit | `QTextDocumentLayout` | Per-view `RichTextLayout` |
| --- | --- | --- |
| One character, page 1 | 508 block layouts (181 ms; 601 ms under load 58) | 1 paragraph shaped, 1 page rebuilt |
| One character, page 250 | 256 block layouts (610 ms under load) | 1 paragraph shaped, 1 page rebuilt, 249 reused (12 ms under load) |
| New paragraph, page 1 | 1,017 block layouts (364 ms; 1,264 ms under load) | 2 paragraphs shaped; pages rebuilt from cached metrics up to the next section break |

The document layout re-lays out every block that crosses a later page
boundary. Re-shaping one paragraph with a standalone `QTextLayout` costs
0.31 ms (0.49 ms under load).

**Decision.** `RichTextDocument` is our own model: paragraphs, interned
runs, tables, lists and object anchors, with UUID-and-offset positions. Its
invertible steps form transactions and undo history. It has an
`externalUndo` bridge on the D-040 pattern, because `UndoService` records
`PropertyDocument` properties only. Each view owns a `RichTextLayout`:

- one `QTextLayout` per paragraph, which keeps Qt shaping, bidi, line
  breaking and font fallback;
- a paginator that restarts at the page before the first dirty block and
  stops when a page starts in its previous flow state;
- page keys derived from the section and the ordinal within it.

Fonts resolve against a 72-dpi device, so layout does not depend on the
screen. Persistence is the canonical `qindatk.richtext/1` codec, and a
`DocumentSession` extension adapter supplies save, recovery and stale-save
protection.

**Consequences.**

- Tables, floats, keep rules and repeated header rows are our code, not
  Qt's. The known-answer tests in `tst_rich_text_layout` and
  `tst_rich_text_scale` guard them.
- An inline object gets its width from percentage letter spacing. Qt 6.11
  scales absolute spacing by `int(fontDpi / screenDpi)`, which is 0 on the
  72-dpi device.
- HTML is an exchange format through `QTextDocument`, never the storage
  format.
- Named styles, table editing, merged cells and FormatRegistry providers
  belong to WP-051 and WP-019.
- Rich-text measurements need a quiet machine or a load note; the block and
  page counts are deterministic.
- See [rich-text.md](rich-text.md).

## D-051 — Convert composed sRGB canvas tiles for explicit RGB displays

**Context.** Canvas objects currently carry QColor sRGB values. Applying a
display transform to each object before alpha blending changes the result,
and the scene-graph geometry path has no profile-aware shader contract.

**Decision.** Composite in an sRGB QImage, then convert the complete tile with
Qt's QColorSpace API for a valid RGB display target. Use the converted raster
as the scene-graph texture as well as the software tile. Keep the geometry
path for identity sRGB display. Clear cached tiles on profile changes and
report unsupported working/display configurations instead of showing stale
or unconverted pixels.

**Consequences.** The supported managed path has CPU conversion and texture
upload cost, but both backends receive the same tile bytes. It does not
perform proofing, arbitrary working-space painting or gamut classification.
Those need separate renderer and ColorEngine integration. See
[canvas.md](canvas.md#rendering).

## D-052 — Keep detached panel placement in DockModel

**Context.** DockHost already displays panels docked or floating inside one
window. A detached panel needs a real native window, but its position still
belongs to the persisted workspace arrangement. Keeping a second placement
map in QML would allow restore and close behavior to diverge.

**Decision.** Add `detached` to DockModel's persisted mode. Its existing
`floatingRect` stores global logical geometry in that mode, and
`detachedScreenId` records the preferred display. `DetachedPanelHost` uses
WindowHost for native lifetime and moves the same declared content item into
the native window after DockHost reclaims its old frame. Native close returns
the item to its declaration and docks the panel in its remembered zone.

**Consequences.** In-host `floating` keeps its old meaning. Restoring a
detached panel with a missing screen uses an available screen and clamps the
logical rectangle to reachable bounds. Apps provide one DetachedPanelHost
and WindowHost alongside DockHost. Content stays model-bound and is not
duplicated when it crosses windows. See [docking.md](docking.md).

## D-053 — Compose domain documents through a model, a codec and one history engine

**Context.** Sessions, undo, atomic save, recovery, workspace restore and
format import/export were written around `PropertyDocument` and its JSON
codec. The spatial canvas kept a separate local history, and storing a canvas
as one PropertyDocument byte value would copy the whole scene into history on
every gesture and overrun the 8 MiB undo budget. Rich text and workbook
engines carry their own histories too.

**Decision.** A domain joins through `DocumentModel` (identity, kind, header,
change notification, and two-phase `prepareCommand`/`publishCommand` of
immutable `DocumentCommand` steps) and a `DocumentCodec` registered by stable
`schemaId`. The codec captures on the GUI thread, then encodes, inspects and
decodes (bounded) on any thread, and adopts on the GUI thread. `UndoService`
becomes the single engine: histories are keyed by document UUID, commits
record before they publish, rejections are returned without side effects, and
a model has exactly one live history (`addModel` refuses a second service;
`adoptModel` moves it). `DocumentSession` holds a model plus its codec and
keeps the PropertyDocument API. A save captures on the GUI thread; a worker
encodes, journals and replaces the file, and a stale receipt marks clean only
the history position it captured. Recovery frames carry the schema and the
complete header under a header hash, and version-1 frames stay readable. The
canvas records `CanvasEdit` deltas with shared image pointers, and page,
layer and image changes are recorded steps. Its binary native format keeps
exact 64-bit identities and counters and is validated before adoption.

**Consequences.** Existing PropertyDocument code and reference files are
unchanged. Generic code must use `model`/`kind`/`schemaId`/`capture`, because
`DocumentSession::document` is null for other kinds. Adding a domain takes a
model adapter, a codec and optional format providers declaring `schemas`.
Recovery frames written by this version cannot be read by older builds.
Full-document snapshots remain the unit of save and recovery. Canvas tiles
are stored uncompressed until a bounded inflater exists, because
`qUncompress` does not bound its output. See [document-models.md](document-models.md).
## D-054 — One tempo authority, a derived time map and engine snapshots

**Context.** Tempo edits must move beat-locked content exactly and
reversibly, previews should show their downstream effect, and an audio engine
must read tempo and automation without GUI-thread timing. TimeMap already
converted musical ticks, and musical markers already derived positions from
it.

**Decision.** `TempoMapModel` owns tempo/meter events and publishes the
merged segments to `TimeMap` (previews included; cancel republishes). A
beat-locked clip stores a PPQ beat anchor plus an exact tick offset, and its
start is re-derived on every time-map change, so an unchanged map round-trips
exactly and tempo undo restores clips bit-exactly without timeline history
steps. `AutomationModel` stores exact-tick points in canonical units and
reuses `CurveEvaluator` per segment. `EngineSchedule` publishes immutable
snapshots of committed data behind an atomic `shared_ptr`, with a TimeMap copy
detached from every thread.

**Consequences.** One place writes tempo segments, and views stay
presentation. Tempo is constant per segment (no ramps). Beat-locked clip
lengths and automation stay absolute. The three models keep separate
histories. Realtime reclamation and a real engine are WP-060. See
[tempo-automation.md](tempo-automation.md).

## D-076 — Bounded scene hierarchy and optional Qt Quick 3D view

**Context.** WP-076 needs real rendered objects and reversible manipulation, while
keeping scene state independent of camera and renderer lifetimes.

**Decision.** SceneModel3D owns stable primitive IDs, hierarchy, uniform positive
scale, quaternion transforms, appearance, transactions and native persistence.
SceneView3D resolves world snapshots through an optional Qt Quick 3D renderer.
Parent-local transforms remain authoritative. Camera-plane drag previews carry
world deltas from the initial position and become one history step on release.

**Consequences.** Uniform scales allow exact keep-world reparenting without shear
or fragile matrix decomposition. Qt owns mesh picking and graphics resources.
The software adaptation reports unavailable. Mesh providers, nonuniform scale,
materials, shared document history and full gizmos remain separate WP-076 scope.
See [scene-3d.md](scene-3d.md).

## D-081 — Script capabilities are copied values and semantic commands

**Context.** A developer console needs useful automation without handing a
script every live QML delegate, QObject and document implementation detail.

**Decision.** `ScriptRuntime` evaluates JavaScript off the UI thread with only
JSON document and command-metadata copies. The host explicitly allowlists
command IDs. Scripts queue actions; the runtime rechecks them and invokes
`CommandRegistry` on its owner thread. A multi-action replay requires an active
`UndoService` model and uses a semantic macro that rolls back on a handler
failure. Evaluation has source, output, command-count, argument and time limits.

**Consequences.** Scripts cannot directly traverse application objects, and a
host defines the API through snapshots and command handlers. The runtime is
local automation, not an untrusted-code sandbox; process and memory isolation,
extension packaging and durable macro storage remain separate work. See
[script-runtime.md](script-runtime.md).

## D-082 — Synchronize reference property values as independent operations

**Context.** `UndoService` records a linear local before/after chain, while
two clients can edit the same value offline and reconnect in either order.

**Decision.** The first collaboration slice stores bounded, stable operation
packets in a local service. Per-property maximum `(logical time, actor UUID,
operation UUID)` wins independent of arrival order; losing operations remain
in the log and generate a conflict signal. The service persists accepted
operations atomically before delivery. Presence stays ephemeral. A winning
value applies through `PropertyDocument` without entering `UndoService`, and
invalidates stale local history.

**Consequences.** Two property replicas converge from the same baseline and
recover the service log on restart. The revision of each replica remains a
local counter. A local socket bridge uses the same packet codec across
processes; each actor's local persistent log also serves as an outgoing queue.
The server checks a host-provided credential at connection and editor/viewer
permission per operation. Live presence travels in separate socket frames and
is replayed only while the origin remains connected; disconnect emits leave.
Named local checkpoints keep immutable typed reference snapshots, author and
revision provenance. Restore creates a safety checkpoint and sends one bounded
snapshot operation that updates winning property values in a single document
revision. Collaborative reversal requires a fresh operation;
generic domain merge, version restore, credential lifecycle and remote-network
transport remain WP-082 work. See [collaboration.md](collaboration.md).
