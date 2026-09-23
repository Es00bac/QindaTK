<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Application services

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

Current starting points: [AppWindow, menus and palette](../current/05-shell-navigation.md),
[DockModel](../current/09-docking.md), template input behavior and QSettings-backed
layout storage. The services below would unify application behavior. C++/Qt may
already offer underlying primitives; these proposals concern QindaTK contracts
and QML integration. They are not claims that such algorithms must be invented.

<a id="app-01"></a>
## APP-01 — CommandRegistry

**Priority:** P0. **Purpose:** One catalog of semantic actions shared by menus, tools, palette, scripts and accessibility.

**Contract sketch:** Register descriptors with stable id, label, icon, capability predicate, checked state and invoke(arguments, context); expose changes and failures to every presentation.

**Dependencies and boundary:** CommandContext; domain handlers supply the operation, not a widget callback stored in a document.

**Completion check:** Disable an action in the active document and verify every presentation updates; one invocation creates exactly one operation.

<a id="app-02"></a>
## APP-02 — CommandContext

**Priority:** P0. **Purpose:** Resolve actions against the focused document, selection and tool.

**Contract sketch:** Expose activeDocument, focusedView, selection and capability scopes; resolve handlers from most specific to application scope with an explicit precedence rule.

**Dependencies and boundary:** DocumentManager and SelectionModel; text editing reserves its local commands during composition.

**Completion check:** Focus a second document and ensure undo/delete target it; typing into an inspector never deletes canvas objects.

<a id="app-03"></a>
## APP-03 — ShortcutMap

**Priority:** P0. **Purpose:** Configurable keyboard dispatch with conflict and context handling.

**Contract sketch:** Map command IDs to portable sequences/chords, contexts and alternatives; emit conflicts and support import/reset; resolve locale/platform presentation separately.

**Dependencies and boundary:** CommandRegistry and platform key/IME information; KeyCap remains display-only.

**Completion check:** Conflicting bindings are explained; canceled chords and IME input do not invoke unintended commands.

<a id="app-04"></a>
## APP-04 — UndoService

**Priority:** P0. **Purpose:** Document-aware reversible history with clean-state tracking.

**Contract sketch:** Expose canUndo/canRedo, undoText/redoText, cleanRevision, undo/redo, macro groups and merge keys; select the active document's stack.

**Dependencies and boundary:** Wrap an undo backend such as Qt primitives; domain commands own inverse data and memory policy.

**Completion check:** A continuous drag is one undo step; undo to the saved revision clears dirty state; branching history behaves deterministically.

<a id="app-05"></a>
## APP-05 — EditTransaction

**Priority:** P0. **Purpose:** Shared begin/preview/commit/cancel lifecycle for a semantic edit.

**Contract sketch:** Hold context, original values, target IDs and base revision; preview proposals, validate on commit, emit committed/canceled/rejected with an undo label.

**Dependencies and boundary:** Domain model and UndoService; controllers own transactions rather than ephemeral delegates.

**Completion check:** Escape, lost grab or rejected validation restores the original values; only a successful commit enters history.

<a id="app-06"></a>
## APP-06 — SelectionModel

**Priority:** P0. **Purpose:** Stable selection shared by views of the same objects or ranges.

**Contract sketch:** Expose currentId, anchorId, selected IDs/ranges and selection mode; select/toggle/extend/clear reconcile model insert/remove/reset events.

**Dependencies and boundary:** Domain identity/index adapter; sparse cell ranges and clip selections use specialized representations.

**Completion check:** Sort/filter a view and verify the same objects remain selected; deleted IDs are removed without selecting unrelated replacements.

<a id="app-07"></a>
## APP-07 — DocumentSession

**Priority:** P0. **Purpose:** Lifecycle facade for one open document.

**Contract sketch:** Expose id, URI, title, revision, dirty, loading/saving/readOnly and undo service; requestSave/SaveAs/close and report operation results.

**Dependencies and boundary:** Document format/store adapter performs atomic persistence and validates capabilities.

**Completion check:** Failed save preserves the document and dirty state; successful save/reopen reproduces committed edits.

<a id="app-08"></a>
## APP-08 — DocumentManager

**Priority:** P0. **Purpose:** Coordinate multiple sessions and save/close workflows.

**Contract sketch:** Open/create/activate/duplicateView/close by document ID; expose sessions and close blockers; support Save All with per-document results.

**Dependencies and boundary:** DocumentSession, WindowHost and format registry; file identity rules prevent accidental duplicate opens.

**Completion check:** Closing several dirty documents handles cancel and partial save failure without losing the remaining sessions.

<a id="app-09"></a>
## APP-09 — WorkspaceSession

**Priority:** P1. **Purpose:** Persist application workspace state beyond panel rectangles.

**Contract sketch:** Record window/view IDs, active document references, per-view state and dock-layout IDs under a versioned schema; restore missing capabilities gracefully.

**Dependencies and boundary:** Existing DockModel continues owning panel placement; document content is stored separately.

**Completion check:** Restore after a monitor or plugin disappears; documents and panels remain reachable and the user can reset the workspace.

<a id="app-10"></a>
## APP-10 — PropertyBinding

**Priority:** P0. **Purpose:** Typed connection between inspector editors and one or many selected objects.

**Contract sketch:** Expose descriptor, value/mixed state, default, writable, validation and animation state; begin/edit/reset/commit/cancel through transactions.

**Dependencies and boundary:** PropertyDescriptor provider and selected domain objects; conversions declare units and precision.

**Completion check:** Editing a mixed selection changes all eligible objects in one undo step and reports locked/incompatible targets.

<a id="app-11"></a>
## APP-11 — ValidationModel

**Priority:** P1. **Purpose:** Consistent field, document and asynchronous validation feedback.

**Contract sketch:** Issues have code, severity, target ID/path and remedies; pending differs from invalid; validation runs are tagged with revision.

**Dependencies and boundary:** Domain validators and localization; UI does not infer validity from border color.

**Completion check:** A stale asynchronous result cannot block a newer valid edit; keyboard focus can reach the first relevant issue.

<a id="app-12"></a>
## APP-12 — SettingsModel

**Priority:** P1. **Purpose:** Schema-based application preferences with defaults and scopes.

**Contract sketch:** Typed keys include defaults, app/document/workspace scope, validation and restart requirements; support staged apply/cancel/reset and migration.

**Dependencies and boundary:** Injected persistent settings store; secrets are references to secure storage rather than ordinary settings values.

**Completion check:** Cancel restores the prior settings; migration retains known values and reports unsupported keys.

<a id="app-13"></a>
## APP-13 — JobModel

**Priority:** P1. **Purpose:** Shared model for long operations and progress.

**Contract sketch:** Jobs expose id, state, progress, dependencies, error, result and cancel/retry capability; transitions are queued to the GUI and revision-tagged.

**Dependencies and boundary:** Worker scheduler owns execution and cancellation; views never run the operation in paint/binding code.

**Completion check:** Cancel import/render while navigating; the UI stays usable and a late result cannot resurrect a closed document.

<a id="app-14"></a>
## APP-14 — AssetProvider

**Priority:** P1. **Purpose:** Reusable asynchronous metadata, thumbnail, frame and peak requests.

**Contract sketch:** Resolve stable handles and request region/time/size/quality with cancellation, revision and cache identity; expose missing/loading/ready/failed states.

**Dependencies and boundary:** Filesystem/network, decode and indexing backends; Thumbnail/Filmstrip/Waveform consume results.

**Completion check:** Rapid scroll/zoom and relink reuse the correct cache entries; recycled delegates never show the prior asset's result.

<a id="app-15"></a>
## APP-15 — ClipboardService

**Priority:** P1. **Purpose:** Typed copy/cut/paste negotiation across views and applications.

**Contract sketch:** Advertise MIME formats and domain payloads, query canPaste, preview conversion loss, commit paste through a document transaction.

**Dependencies and boundary:** OS clipboard adapter and domain serializers; rich content keeps units and resource references.

**Completion check:** Copy objects/cells/text across supported views, undo paste, and handle unsupported or missing embedded resources explicitly.

<a id="app-16"></a>
## APP-16 — DragDropService

**Priority:** P1. **Purpose:** Common payload and drop-preview protocol.

**Contract sketch:** Drag payload includes stable IDs, source model and allowed operations; targets validate before preview/commit; cancel clears transient state.

**Dependencies and boundary:** Platform drag adapter, selection and domain commands; DockModel's existing internal gestures remain separate.

**Completion check:** Move/copy assets or objects without duplicate commits; rejected drops and escaped drags leave the model unchanged.

<a id="app-17"></a>
## APP-17 — HistoryView

**Priority:** P1. **Purpose:** Navigable visualization of a document's undo history.

**Contract sketch:** Read operation labels, current/clean position and grouped changes; request navigation through UndoService, never edit history storage directly.

**Dependencies and boundary:** UndoService and snapshot/memory policy supplied by the document.

**Completion check:** Selecting an earlier history entry restores the corresponding state and marks the clean position correctly.

<a id="app-18"></a>
## APP-18 — RecoveryManager

**Priority:** P0. **Purpose:** Autosave and crash-recovery coordination.

**Contract sketch:** Track recovery snapshots/journals by document ID and revision, expose recover/discard/inspect choices, and separate recovery from explicit save success.

**Dependencies and boundary:** Durable storage, schema migration and domain replay adapters; time/size limits are configurable.

**Completion check:** Terminate during writes, recover the last valid revision, and preserve the original file when a recovery entry is corrupt.

<a id="app-19"></a>
## APP-19 — ResourceResolver

**Priority:** P1. **Purpose:** Resolve document links to external or embedded assets.

**Contract sketch:** Map logical resource IDs to locations, revisions and availability; expose missing/modified/relink operations and batch search proposals.

**Dependencies and boundary:** Document resource manifest, AssetProvider and storage adapters.

**Completion check:** Relink a missing media file once and update every reference while preserving clip timing and undoable document changes.

<a id="app-20"></a>
## APP-20 — CapabilityRegistry

**Priority:** P1. **Purpose:** Discover optional domain and backend features before use.

**Contract sketch:** Expose feature IDs, availability, version, supported operations and reason when unavailable; notify when a plugin/backend changes.

**Dependencies and boundary:** Extension registry and backend probes; capability is independent of license/account UI policy.

**Completion check:** An app hides or disables unavailable export/edit actions with a useful reason and preserves unsupported document content.
