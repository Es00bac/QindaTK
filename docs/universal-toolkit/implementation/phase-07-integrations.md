<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 07 — Integration and specialist workflows

**PLANNED — no implementation or execution is claimed by this plan.**

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-080"></a>
## WP-080 — Implement detached native panels

**Implementation lead:** Sol. **Status:** active (native window and DockModel integration in progress; WP-040 dependency in review). **Depends on:** [WP-014](phase-01-services.md#wp-014), [WP-018](phase-01-services.md#wp-018), [WP-040](phase-03-authoring.md#wp-040)

**Proposal scope:** [PLT-02 DetachedPanelHost](../proposed/08-platform-integrations.md#plt-02)

**Change area:** DetachedPanelHost, WindowHost/DockModel integration and multi-window render/focus fixtures.

1. Define detachment, reattachment, ownership transfer, closing and workspace serialization with DockModel remaining placement authority.
2. Implement actual native panel windows and safe content/resource transfer or model-bound reconstruction.
3. Preserve command contexts, focus, document references and accessible relationships across windows.
4. Restore detached geometry on changed/removed displays and handle mixed scale factors and window destruction.
5. Verify detach/reattach during active documents, lost grabs, popup placement and restart; existing DockFloating retains its in-host semantics.

**Accept when:** Panels occupy real native windows with persistent reachable state and correct ownership across docking transitions.

**Delegate:** Luna authors multi-window fixture scenes and documentation; Sol owns native/QML/render lifecycle.

**Astra trigger:** Consult Astra for a reproduced cross-window resource or focus/lifetime failure that survives a minimal implementation.

<a id="wp-081"></a>
## WP-081 — Implement extensions, scripting and developer surfaces

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-025](phase-02-controls-data.md#wp-025), [WP-027](phase-02-controls-data.md#wp-027), [WP-032](phase-02-controls-data.md#wp-032), [WP-080](phase-07-integrations.md#wp-080)

**Proposal scope:** [PLT-12 ExtensionRegistry](../proposed/08-platform-integrations.md#plt-12), [PLT-13 ScriptConsole](../proposed/08-platform-integrations.md#plt-13), [EXT-01 CodeEditor](../proposed/09-specialized-domains.md#ext-01), [EXT-02 TerminalView](../proposed/09-specialized-domains.md#ext-02)

**Change area:** ExtensionRegistry, ScriptConsole, CodeEditor, TerminalView and local runtime/LSP/PTY adapters.

**Additional obligations:** [ENG-24](supporting-scope.md#eng-24).

1. Define extension discovery/versioning, declared capabilities, script command/undo access and resource/lifecycle boundaries.
2. Implement actual extension load/unload and a bounded scripting runtime with useful command/document APIs and cancel/error reporting.
3. Implement code editing with syntax/token providers, diagnostics and optional language-service integration behind a versioned adapter.
4. Implement a real terminal session provider with PTY/process lifecycle, resize, encoding, scrollback and explicit user-initiated process actions.
5. Verify extension failure/unload, canceled scripts, code IME/large files and terminal resize/process exit; tests use isolated local fixtures.

**Accept when:** Developer surfaces connect to functional local runtimes and terminate resources reliably; no hidden mock terminal or script facade ships as complete.

**Delegate:** Luna owns editor/console presentation and provider fixtures; Sol owns extension ABI, scripting and process integration.

**Astra trigger:** Use Astra only for a demonstrated extension isolation or terminal/parser lifecycle problem that blocks the contract.

<a id="wp-082"></a>
## WP-082 — Implement collaboration, presence and version history

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-013](phase-01-services.md#wp-013), [WP-016](phase-01-services.md#wp-016), [WP-032](phase-02-controls-data.md#wp-032), [WP-081](phase-07-integrations.md#wp-081)

**Proposal scope:** [PLT-14 CollaborationAdapter](../proposed/08-platform-integrations.md#plt-14), [PLT-15 PresenceLayer](../proposed/08-platform-integrations.md#plt-15), [PLT-16 VersionBrowser](../proposed/08-platform-integrations.md#plt-16)

**Change area:** CollaborationAdapter, PresenceLayer, VersionBrowser and local multi-client synchronization backend.

**Additional obligations:** [ENG-17](supporting-scope.md#eng-17).

1. Define revision/operation identity, ordering, conflict policy, permissions/capabilities and undo semantics for remote versus local edits.
2. Prototype concurrent object/text edits, offline reconnection and divergent history against a local two-client service; choose merge strategy from those fixtures.
3. Implement actual synchronization, persistence/reconnect and presence with ephemeral state separated from document history.
4. Implement version browsing/compare/restore as an explicit new document operation with preserved provenance.
5. Verify reordered/duplicate/dropped messages, concurrent deletes, permission changes, reconnect and convergence without erasing committed user edits.

**Accept when:** Two real local clients converge under documented conflict rules and recover from offline edits; presence and history remain correctly scoped.

**Delegate:** Luna owns presence/version views and deterministic network-failure fixtures; Sol owns synchronization and storage.

**Astra trigger:** Astra can consult on a concrete OT/CRDT/merge or distributed-undo problem after an operation trace exposes the conflict; it does not take orchestration.

<a id="wp-083"></a>
## WP-083 — Implement diagramming and CAD surfaces

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-044](phase-03-authoring.md#wp-044), [WP-074](phase-06-video.md#wp-074), [WP-076](phase-06-video.md#wp-076)

**Proposal scope:** [EXT-03 DiagramCanvas](../proposed/09-specialized-domains.md#ext-03), [EXT-04 CadViewport](../proposed/09-specialized-domains.md#ext-04)

**Change area:** DiagramCanvas, CadViewport and graph-layout/geometry/constraint adapters.

**Additional obligations:** [ENG-18](supporting-scope.md#eng-18), [VIS-11](supporting-scope.md#vis-11).

1. Define diagram nodes/edges/ports and CAD entities/units/constraints as separate domain models behind common canvas contracts.
2. Implement actual graph layouts, routed edges and flow/Sankey quantities with stable identities and meaningful totals.
3. Integrate a functional geometry/constraint backend for the declared CAD editing/measurement operations.
4. Implement snapping, selection, property editing and persistent domain operations with real output/export adapters.
5. Verify routing/flow conservation, constrained geometry, degenerate inputs, precision, cancel/undo and reopen/export fixtures.

**Accept when:** Diagram and CAD views manipulate real domain data; network/flow visualization and declared CAD operations are qualified.

**Delegate:** Luna owns palettes, property panels and fixtures; Sol owns layout/geometry and constraint integration.

**Astra trigger:** Use Astra for an unresolved constraint solver, geometry precision or graph layout failure with bounded input/output requirements.

<a id="wp-084"></a>
## WP-084 — Implement geographic mapping

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-029](phase-02-controls-data.md#wp-029), [WP-040](phase-03-authoring.md#wp-040), [WP-043](phase-03-authoring.md#wp-043)

**Proposal scope:** [EXT-05 MapView](../proposed/09-specialized-domains.md#ext-05)

**Change area:** MapView and geographic data/projection/tile providers.

**Additional obligations:** [ENG-19](supporting-scope.md#eng-19), [VIS-12](supporting-scope.md#vis-12).

1. Define coordinate reference systems, axis/unit conventions, feature identity, geometry and tile/source capabilities.
2. Integrate real projection and vector/raster providers with local fixture data and bounded visible-region caching.
3. Implement navigation, feature hit/selection, attribute inspection and supported edit/annotation commands.
4. Handle missing/offline tiles, projection limits, wrap/antimeridian behavior and source attribution metadata.
5. Verify known projected coordinates, mixed layers, selection accuracy, offline errors and saved/exported data for supported operations.

**Accept when:** MapView renders actual geographic data with explicit coordinate semantics and tested provider behavior.

**Delegate:** Luna owns layer/attribute controls and local map fixtures; Sol owns projection and geographic providers.

<a id="wp-085"></a>
## WP-085 — Implement scientific plotting

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-030](phase-02-controls-data.md#wp-030), [WP-031](phase-02-controls-data.md#wp-031), [WP-076](phase-06-video.md#wp-076)

**Proposal scope:** [EXT-06 ScientificPlot](../proposed/09-specialized-domains.md#ext-06)

**Change area:** ScientificPlot and typed numeric/uncertainty/2D/3D plotting adapters.

1. Define array shape, units, coordinate dimensions, missing values, precision and uncertainty semantics.
2. Implement useful scientific traces, scatter and sampled surfaces with axis/selection/annotation support and bounded data access.
3. Add 3D scatter/surface providers using the scene/render boundary with explicit interpolation and camera transforms.
4. Implement reproducible figure export with data/parameter provenance and a textual/tabular alternative.
5. Verify known analytic functions, units/log limits, downsampling, non-finite data and reproducible exported figures.

**Accept when:** ScientificPlot renders and exports real numeric data with declared precision and sampling behavior.

**Delegate:** Luna owns plot controls, analytical fixtures and export metadata; Sol owns numerical/render algorithms.

<a id="wp-086"></a>
## WP-086 — Implement image slices, volumes and transfer functions

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-043](phase-03-authoring.md#wp-043), [WP-076](phase-06-video.md#wp-076), [WP-085](phase-07-integrations.md#wp-085)

**Proposal scope:** [EXT-07 ImageSliceView](../proposed/09-specialized-domains.md#ext-07), [EXT-08 VolumeView](../proposed/09-specialized-domains.md#ext-08), [EXT-09 TransferFunctionEditor](../proposed/09-specialized-domains.md#ext-09)

**Change area:** ImageSliceView, VolumeView, TransferFunctionEditor and multidimensional image/volume providers.

**Additional obligations:** [ENG-20](supporting-scope.md#eng-20), [VIS-14](supporting-scope.md#vis-14).

1. Define volume axes, spacing/orientation, channel/time dimensions, units, voxel types and chunked storage.
2. Implement actual slice/reslice and volume rendering with bounded chunk/cache residency and explicit interpolation.
3. Implement linked crosshairs, plane navigation and transfer-function editing through typed property transactions.
4. Connect scientific 3D scatter/surface/volume family examples and persist view/transfer settings separately from immutable source data.
5. Verify known voxel phantoms, orientation/spacing, transfer mapping, GPU capability fallback, cancel/undo and large-data memory bounds.

**Accept when:** The three controls display actual multidimensional data with known spatial/numeric interpretation and complete 3D visualization examples.

**Delegate:** Luna owns slice/transfer controls and generated phantoms; Sol owns volume/render/numerical adapters.

**Astra trigger:** Use Astra for a reproduced resampling, orientation or GPU volume integration problem requiring deeper analysis.

<a id="wp-087"></a>
## WP-087 — Implement dependency and trace exploration

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-015](phase-01-services.md#wp-015), [WP-032](phase-02-controls-data.md#wp-032), [WP-048](phase-03-authoring.md#wp-048), [WP-074](phase-06-video.md#wp-074), [WP-083](phase-07-integrations.md#wp-083)

**Proposal scope:** [EXT-10 AssetDependencyView](../proposed/09-specialized-domains.md#ext-10), [EXT-11 TraceView](../proposed/09-specialized-domains.md#ext-11)

**Change area:** AssetDependencyView, TraceView and dependency/trace ingestion/index adapters.

1. Define asset reference edges, missing/cyclic dependency states and timestamped trace spans/events/threads with clock-domain metadata.
2. Implement actual dependency extraction/indexing and trace ingestion with bounded storage and explicit truncation policy.
3. Implement dependency navigation/relink diagnostics and trace zoom/filter/span inspection using shared graph/timeline patterns.
4. Keep large trace processing off the GUI thread and reject obsolete document/asset analyses.
5. Verify cycles, deleted references, nested/cross-thread spans, clock conversions, large traces and cancellation/export.

**Accept when:** Views explain real asset relationships and execution traces with correct identity/time interpretation and bounded work.

**Delegate:** Luna owns inspectors/filter controls and synthetic traces; Sol owns extraction, indexing and clock alignment.

<a id="wp-088"></a>
## WP-088 — Qualify platform and backend integration boundaries

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-005](phase-00-baseline.md#wp-005), [WP-018](phase-01-services.md#wp-018), [WP-053](phase-04-office.md#wp-053), [WP-060](phase-05-audio.md#wp-060), [WP-066](phase-05-audio.md#wp-066), [WP-070](phase-06-video.md#wp-070), [WP-077](phase-06-video.md#wp-077), [WP-080](phase-07-integrations.md#wp-080), [WP-081](phase-07-integrations.md#wp-081)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Platform adapter matrix, device/render/window integration fixtures and build options.

1. Revisit the supported OS/Qt/backend matrix established in WP-005 using actual implemented dependencies and available test hosts.
2. Exercise native windows, files, print, input, accessibility bridges, media devices, plugin editors and render context loss on each claimed configuration.
3. Verify optional engines can be omitted while lightweight QindaTK consumers still import/link and unsupported features report useful capabilities.
4. Exercise install/relocation and backend discovery with no source-tree or developer-path assumptions.
5. Record each environment/operation result and repair platform-specific defects; unavailable configurations remain unqualified rather than being marked passed.

**Accept when:** Claimed platform/backend combinations have actual integration evidence, and optional dependency boundaries work in clean consumers.

**Delegate:** Luna can run bounded fixture batches and consolidate result records; Sol owns native/backend fixes and support declarations.

<a id="wp-089"></a>
## WP-089 — Qualify format, device and output fidelity

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-045](phase-03-authoring.md#wp-045), [WP-052](phase-04-office.md#wp-052), [WP-054](phase-04-office.md#wp-054), [WP-055](phase-04-office.md#wp-055), [WP-056](phase-04-office.md#wp-056), [WP-058](phase-04-office.md#wp-058), [WP-059](phase-04-office.md#wp-059), [WP-065](phase-05-audio.md#wp-065), [WP-066](phase-05-audio.md#wp-066), [WP-067](phase-05-audio.md#wp-067), [WP-068](phase-05-audio.md#wp-068), [WP-078](phase-06-video.md#wp-078), [WP-083](phase-07-integrations.md#wp-083), [WP-084](phase-07-integrations.md#wp-084), [WP-086](phase-07-integrations.md#wp-086)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Format corpus, operation capability matrix, output comparison tools and backend provenance.

**Additional obligations:** [ENG-25](supporting-scope.md#eng-25).

1. Inventory every advertised format and separate recognition, rendering, editable import, export and roundtrip claims.
2. Expand fixtures to malformed, partial, linked, multilingual, high-precision and versioned content for each claimed operation.
3. Compare semantic results and rendered/audio outputs with declared tolerances and independent reference data where available.
4. Exercise unsupported data preservation, warnings, cancellation and failed output cleanup; avoid promoting sample success to general vendor parity.
5. Record backend versions, redistribution constraints and required runtime assets; fix claim/behavior mismatches before release.

**Accept when:** Every advertised interchange/output operation has evidence and precise limitations, including real device/media/plugin paths where relevant.

**Delegate:** Luna curates fixture manifests and runs deterministic comparisons; Sol owns fidelity decisions and backend corrections.
