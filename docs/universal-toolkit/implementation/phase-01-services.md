<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 01 — Application contracts and services

**Execution beginning.** WP-010 and WP-011 are accepted; WP-012 is ready. Other
packages remain planned until their dependencies and acceptance gates are met.

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-010"></a>
## WP-010 — Define typed cross-layer contracts and capability discovery

**Implementation lead:** Sol. **Status:** accepted; [integrated evidence](evidence/WP-010.md). **Depends on:** [WP-004](phase-00-baseline.md#wp-004), [WP-005](phase-00-baseline.md#wp-005), [WP-006](phase-00-baseline.md#wp-006), [WP-007](phase-00-baseline.md#wp-007)

**Proposal scope:** [APP-20 CapabilityRegistry](../proposed/01-application-services.md#app-20)

**Change area:** New core contract interfaces/value records, capability service, optional pack/build boundaries and ADRs.

**Additional obligations:** [ENG-01](supporting-scope.md#eng-01).

1. Specify EntityId, document/revision IDs, typed units/time/color handles, errors, validation, command/edit contexts, request cancellation and model-reset semantics.
2. Define GUI/worker/render/realtime ownership and versioned immutable snapshots. Specify which values may cross into QML and how exact integers are represented.
3. Implement CapabilityRegistry with operation-level availability, disabled reasons and backend/version provenance; capability probes must not load every optional engine.
4. Set the optional feature-pack and adapter registration/build policy while preserving import QindaTK as Tk and lightweight use of existing controls.
5. Publish minimal C++/QML contract fixtures and adapter conformance tests. Approve these interfaces before dependent workers start implementation.

**Accept when:** A real consumer can query capabilities and exchange typed, versioned records; ownership, cancellation and unsupported operations are explicit.

**Delegate:** Luna writes examples and conformance fixtures from the settled interfaces; Sol owns contract design and registration.

**Astra trigger:** Astra is reserved for an unresolved cross-thread ownership or precision design with competing implementations and a measurable failure case.

<a id="wp-011"></a>
## WP-011 — Unify commands, contexts, shortcuts and focus

**Implementation lead:** Sol. **Status:** accepted; [integrated evidence](evidence/WP-011.md). **Depends on:** [WP-010](phase-01-services.md#wp-010)

**Proposal scope:** [APP-01 CommandRegistry](../proposed/01-application-services.md#app-01), [APP-02 CommandContext](../proposed/01-application-services.md#app-02), [APP-03 ShortcutMap](../proposed/01-application-services.md#app-03), [PLT-08 FocusManager](../proposed/08-platform-integrations.md#plt-08)

**Change area:** Command services, FocusManager and adapters for existing menu/toolbar/palette controls.

1. Define context-stack lifetime, command IDs, enable/check state, disabled reasons, shortcut scopes and focus restoration tokens.
2. Implement registry/context resolution and focus ownership across documents, editors, popups and windows; avoid stale targets after a document closes.
3. Implement shortcut conflict detection, persistence hooks and platform display strings while allowing IME/text input to retain ordinary editing keys.
4. Wire existing Menu, ToolBar and CommandPalette paths to the same commands; preserve direct-use compatibility for simple applications.
5. Exercise pointer, menu, shortcut and assistive invocation, nested focus scopes and closing targets; expose diagnostics for unresolved/conflicting shortcuts.

**Accept when:** Every invocation path resolves the same command/context; focus returns predictably and text composition is not intercepted.

**Delegate:** Luna supplies command presentation bindings and focus/shortcut fixture scenes after the services are stable.

<a id="wp-012"></a>
## WP-012 — Implement reversible editing, stable selection and properties

**Implementation lead:** Sol. **Status:** ready. **Depends on:** [WP-010](phase-01-services.md#wp-010), [WP-011](phase-01-services.md#wp-011)

**Proposal scope:** [APP-04 UndoService](../proposed/01-application-services.md#app-04), [APP-05 EditTransaction](../proposed/01-application-services.md#app-05), [APP-06 SelectionModel](../proposed/01-application-services.md#app-06), [APP-10 PropertyBinding](../proposed/01-application-services.md#app-10), [APP-11 ValidationModel](../proposed/01-application-services.md#app-11)

**Change area:** UndoService, EditTransaction, SelectionModel, PropertyBinding and ValidationModel.

1. Specify begin/update/validate/commit/cancel, stale revision handling, merge keys, undo memory policy and deletion/model-reset reconciliation.
2. Implement transactions and undo adapters for a small authoritative model, including rollback after validation failure or lost pointer grab.
3. Implement ID/range selection with current item and anchor; preserve identity through proxy sorting/filtering and define removal behavior.
4. Implement typed property descriptors, mixed/default/read-only/animated states and validation issues; separate programmatic synchronization from user edit requests.
5. Verify one drag is one undo entry, cancel restores the original state, multi-target edits are atomic, invalid edits explain failure and bindings do not loop.

**Accept when:** The same reversible edit works through field, handle and command paths with stable identity and clear validation.

**Delegate:** Luna builds fixtures and presentation adapters; Sol owns transaction/selection/model semantics.

**Astra trigger:** Ask Astra for unresolved transaction rebasing or undo memory correctness only after reducing the issue to deterministic model operations.

<a id="wp-013"></a>
## WP-013 — Implement document sessions, persistence and crash recovery

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-012](phase-01-services.md#wp-012)

**Proposal scope:** [APP-07 DocumentSession](../proposed/01-application-services.md#app-07), [APP-08 DocumentManager](../proposed/01-application-services.md#app-08), [APP-18 RecoveryManager](../proposed/01-application-services.md#app-18)

**Change area:** DocumentSession/Manager/RecoveryManager, native fixture serializer and persistence tests.

**Additional obligations:** [ENG-02](supporting-scope.md#eng-02).

1. Define document dirty/revision state, multiple views, save-as, external-change handling, close negotiation, schema migration and recovery retention.
2. Implement a versioned reference document serializer with atomic replacement, explicit failure handling and immutable save snapshots.
3. Implement multi-document lifecycle and asynchronous save completion without clearing dirty state for newer edits.
4. Implement recovery journaling/checkpoints, validation and restoration; distinguish workspace state from document content.
5. Inject interrupted/failed saves, invalid recovery data and concurrent edits; verify undo, close cancellation and reopen preserve the expected document revision.

**Accept when:** A two-document application survives save failure and process interruption and reopens a validated revision without losing newer edits.

**Delegate:** Luna owns lifecycle fixture UI and fault-injection harness plumbing; Sol owns persistence ordering and recovery correctness.

**Astra trigger:** Escalate a demonstrated durability or recovery ordering problem to Astra with the exact failure sequence and storage assumptions.

<a id="wp-014"></a>
## WP-014 — Persist settings and workspace state

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-013](phase-01-services.md#wp-013)

**Proposal scope:** [APP-09 WorkspaceSession](../proposed/01-application-services.md#app-09), [APP-12 SettingsModel](../proposed/01-application-services.md#app-12)

**Change area:** SettingsModel, WorkspaceSession and DockModel/DockHost persistence boundary.

**Additional obligations:** [FIX-F09](supporting-scope.md#fix-f09).

1. Define settings scopes, defaults, migrations and validation; separate application preferences from document and window/session data.
2. Define workspace serialization for windows, panels, active documents/view references and reachable restored geometry.
3. Resolve F09 by declaring floating shade state transient or moving persistent state into the C++ model; never duplicate arrangement authority in QML.
4. Implement schema migration, unknown-field handling and recovery from invalid or unavailable monitor/panel references.
5. Verify restarting, changing displays, restoring multiple views and resetting preferences; failed workspace restore must not damage document data.

**Accept when:** Settings and workspace roundtrip independently with a documented shade-state policy and usable restored geometry.

**Delegate:** Luna builds settings descriptors and restore fixtures; Sol owns storage schema and DockModel changes.

<a id="wp-015"></a>
## WP-015 — Implement asynchronous jobs and asset services

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-010](phase-01-services.md#wp-010), [WP-013](phase-01-services.md#wp-013)

**Proposal scope:** [APP-13 JobModel](../proposed/01-application-services.md#app-13), [APP-14 AssetProvider](../proposed/01-application-services.md#app-14), [APP-19 ResourceResolver](../proposed/01-application-services.md#app-19)

**Change area:** JobModel, AssetProvider, ResourceResolver and bounded worker/cache infrastructure.

1. Define request IDs, cancellation, progress/error states, document/revision association and asset URI/relink rules.
2. Implement bounded job scheduling, priorities and resource lifetimes with GUI-thread model updates and worker-owned heavy work.
3. Implement asset metadata/preview handles, cache limits, missing-asset resolution and duplicate request coalescing.
4. Reject stale results after edits, view recycling or document closure; canceled work must release subscriptions and temporary resources.
5. Exercise delayed providers, failures, relink and repeated open/close; measure cache bounds and cancellation completion.

**Accept when:** Async results cannot mutate obsolete documents or recycled delegates, and job/asset state is inspectable and bounded.

**Delegate:** Luna implements deterministic delayed/failing providers and job views; Sol owns concurrency and cache policy.

<a id="wp-016"></a>
## WP-016 — Implement clipboard and drag/drop protocols

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-012](phase-01-services.md#wp-012), [WP-015](phase-01-services.md#wp-015)

**Proposal scope:** [APP-15 ClipboardService](../proposed/01-application-services.md#app-15), [APP-16 DragDropService](../proposed/01-application-services.md#app-16)

**Change area:** ClipboardService, DragDropService, payload serializers and domain adapters.

1. Define versioned internal payloads, external MIME alternatives, stable entity references and copy/move/link semantics.
2. Implement serialization and parsing with size/type validation, clear unsupported-data errors and resource ownership.
3. Route paste/drop through validation and one transaction; resolve cross-document IDs and missing assets without reusing foreign identities.
4. Implement keyboard alternatives and cancellation, including drag autoscroll and view recycling.
5. Verify internal and external text/image/range/object payloads, malformed input, cross-document moves and undo of a large paste.

**Accept when:** Transfer is atomic, reversible and interoperable for each advertised MIME capability, with explicit unsupported cases.

**Delegate:** Luna can implement simple MIME adapters and fixtures from the approved schema; Sol reviews cross-document mutation.

<a id="wp-017"></a>
## WP-017 — Provide accessibility, locale and gesture services

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-007](phase-00-baseline.md#wp-007), [WP-011](phase-01-services.md#wp-011), [WP-012](phase-01-services.md#wp-012)

**Proposal scope:** [PLT-06 AccessibilityAdapter](../proposed/08-platform-integrations.md#plt-06), [PLT-07 LocaleService](../proposed/08-platform-integrations.md#plt-07), [PLT-09 GestureRouter](../proposed/08-platform-integrations.md#plt-09)

**Change area:** AccessibilityAdapter, LocaleService and GestureRouter; custom scene/model semantics.

1. Define semantic roles, actions, value/range/selection exposure and virtual-child identity for grids, canvas and timeline items.
2. Implement adapters that route accessible actions to the same commands/transactions as pointer/keyboard input; provide text alternatives for data displays.
3. Implement locale-aware formatting/parsing, translation hooks, bidi/RTL and independent document numeric storage.
4. Implement pointer/touch/stylus/wheel arbitration, grab loss and gesture cancellation with focus rules inherited from WP-011.
5. Test real assistive interaction on the available target plus IME, RTL, translated overflow and competing gestures. Record additional platform qualification requirements.

**Accept when:** Services enable actionable custom editors; role declarations alone do not close accessibility work.

**Delegate:** Luna authors semantic adapters and localized fixture content; Sol owns virtual accessibility and gesture arbitration.

<a id="wp-018"></a>
## WP-018 — Add native window, file and desktop integration

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-014](phase-01-services.md#wp-014), [WP-017](phase-01-services.md#wp-017)

**Proposal scope:** [PLT-01 WindowHost](../proposed/08-platform-integrations.md#plt-01), [PLT-03 FileDialogAdapter](../proposed/08-platform-integrations.md#plt-03), [PLT-05 DesktopIntegration](../proposed/08-platform-integrations.md#plt-05)

**Change area:** WindowHost, FileDialogAdapter, DesktopIntegration and platform implementations.

1. Specify window/document ownership, native parent/child relationships, display scaling, file URL semantics and desktop operation capabilities.
2. Implement host lifecycle and file dialog adapters without tying reusable controls to a platform-specific style.
3. Implement supported recent-file, open-with/reveal and application activation integrations behind capability checks.
4. Handle canceled dialogs, revoked/unavailable file locations, monitor removal and platform errors as ordinary model states.
5. Verify two independent windows, focus/close behavior, mixed display scales and file selection on each claimed target.

**Accept when:** Application shells can manage actual native windows and OS file interactions with explicit platform capability evidence.

**Delegate:** Luna builds file/status presentation and platform fixture apps; Sol owns native lifecycle integration.

<a id="wp-019"></a>
## WP-019 — Establish format registry and import/export jobs

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-015](phase-01-services.md#wp-015), [WP-016](phase-01-services.md#wp-016), [WP-018](phase-01-services.md#wp-018)

**Proposal scope:** [PLT-17 ImportDialog](../proposed/08-platform-integrations.md#plt-17), [PLT-18 ExportDialog](../proposed/08-platform-integrations.md#plt-18), [PLT-20 FormatRegistry](../proposed/08-platform-integrations.md#plt-20)

**Change area:** FormatRegistry, ImportDialog/ExportDialog, adapter metadata and fixture corpus.

1. Define per-format capabilities for recognition, rendering, editable import, export and roundtrip, with version/options/provenance.
2. Register the native reference format and straightforward actual import/export adapters; keep later domain providers independently loadable.
3. Implement option negotiation, preflight, destination conflict handling, progress, cancellation and structured loss/error reports.
4. Preserve unsupported content where an adapter can, and show explicit limitations before a lossy operation; avoid advertising an entire vendor format family from one passing fixture.
5. Exercise malformed, large, multilingual, missing-linked-asset and partial-support fixtures. Add fixtures whenever a later engine claims another operation.

**Accept when:** Import/export uses real provider jobs and publishes operation-specific capabilities; failed/canceled writes leave recoverable state.

**Delegate:** Luna implements dialog presentation and metadata fixtures; Sol owns format contracts and provider selection.
