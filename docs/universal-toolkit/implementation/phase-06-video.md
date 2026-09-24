<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 06 — Video, motion and 3D workflows

**IN PROGRESS — WP-070 monitor and bin controls and local asset-frame decoding are implemented; playback and sequence output remain open.**

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-070"></a>
## WP-070 — Integrate media decoding and source/program monitors

**Implementation lead:** Sol. **Status:** accepted (media decode and encode, playback engine and media catalog). **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-040](phase-03-authoring.md#wp-040), [WP-043](phase-03-authoring.md#wp-043), [WP-047](phase-03-authoring.md#wp-047), [WP-048](phase-03-authoring.md#wp-048)

**Proposal scope:** [VID-01 SourceProgramMonitor](../proposed/07-video-motion.md#vid-01), [VID-02 MediaBin](../proposed/07-video-motion.md#vid-02)

**Change area:** SourceProgramMonitor, MediaBin and decode/frame/cache/composite backend.

**Additional obligations:** [ENG-13](supporting-scope.md#eng-13).

1. Define exact frame identity, timestamps, duration/rate policy, audio sync, stream/color metadata and bounded frame ownership.
2. Select/integrate actual decode, basic sequence composite and encode providers using rate, seek and damaged-media fixtures.
3. Implement source/program monitors and virtual media bins with metadata, proxy state and relink.
4. Connect authoritative playback/seek acknowledgments, decoder cancellation and GPU/software frame transfer.
5. Verify first/last/boundary frames, long seeks, supported variable-rate policy, missing assets, audio sync and a real sequence export.

**Accept when:** A basic real media sequence can be browsed, played, sought and exported with verified frame identity.

**Delegate:** Luna owns monitor/bin controls and fixture metadata; Sol owns codecs, caches, sync and frame lifecycle.

**Astra trigger:** Escalate a reproducible timestamp, A/V sync or decoder/render-resource issue to Astra when ordinary backend investigation does not resolve it.

<a id="wp-071"></a>
## WP-071 — Implement professional timeline video edits

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-048](phase-03-authoring.md#wp-048), [WP-070](phase-06-video.md#wp-070)

**Proposal scope:** [VID-03 TrimView](../proposed/07-video-motion.md#vid-03), [VID-04 MulticamView](../proposed/07-video-motion.md#vid-04), [VID-05 TransitionEditor](../proposed/07-video-motion.md#vid-05)

**Change area:** TrimView, MulticamView, TransitionEditor and NLE edit/evaluation policies.

1. Define source/program trim roles, roll/ripple/slip/slide operations, multicam angles/sync and transition handles.
2. Implement actual frame-exact sequence edits and transition evaluation with legal media bounds.
3. Implement dual trim previews, multicam switching and parameterized transition controls through shared timeline transactions.
4. Connect audio-linked selections, ripple policies and missing-media states without losing exact source offsets.
5. Verify boundary frames, linked edits, insufficient handles, angle switching, undo and exported sequence equivalence.

**Accept when:** NLE controls perform real legal sequence edits and produce the expected output frames and audio relationships.

**Delegate:** Luna owns trim/multicam/transition presentation and edit fixtures; Sol owns temporal edit/composite algorithms.

<a id="wp-072"></a>
## WP-072 — Implement captions, scopes and grading controls

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-043](phase-03-authoring.md#wp-043), [WP-050](phase-04-office.md#wp-050), [WP-070](phase-06-video.md#wp-070)

**Proposal scope:** [VID-06 CaptionEditor](../proposed/07-video-motion.md#vid-06), [VID-07 VideoScopes](../proposed/07-video-motion.md#vid-07), [VID-08 ColorWheels](../proposed/07-video-motion.md#vid-08)

**Change area:** CaptionEditor, VideoScopes, ColorWheels and caption/analysis/grade providers.

1. Define caption intervals/styles, supported sidecar/embedded capabilities, scope signal conventions and grading parameter spaces.
2. Implement real caption timing/edit/serialization and supported render/export paths.
3. Implement waveform/vectorscope/histogram analysis from decoded frames with explicit range, transfer and channel conventions.
4. Implement grading wheel gestures and an actual grade transform in the output pipeline using ColorContext.
5. Verify caption overlaps/boundaries, multilingual output, known color bars/scopes, grade undo and exported color behavior.

**Accept when:** Caption/scopes/grade controls operate on actual media data with verified timing and color interpretation.

**Delegate:** Luna builds caption and wheel controls; Sol owns scope computation, color and caption output.

<a id="wp-073"></a>
## WP-073 — Implement motion composition and keyframe editing

**Implementation lead:** Sol. **Status:** accepted (layer timing, path editing, bounded caches and exact-tick frame export). **Depends on:** [WP-042](phase-03-authoring.md#wp-042), [WP-043](phase-03-authoring.md#wp-043), [WP-049](phase-03-authoring.md#wp-049), [WP-070](phase-06-video.md#wp-070)

**Proposal scope:** [VID-09 EffectBrowser](../proposed/07-video-motion.md#vid-09), [VID-10 KeyframeTimeline](../proposed/07-video-motion.md#vid-10), [VID-11 DopeSheet](../proposed/07-video-motion.md#vid-11), [VID-12 CompositionView](../proposed/07-video-motion.md#vid-12), [VID-14 MotionPathOverlay](../proposed/07-video-motion.md#vid-14), [VID-17 OnionSkinView](../proposed/07-video-motion.md#vid-17)

**Change area:** EffectBrowser, KeyframeTimeline, DopeSheet, CompositionView, MotionPathOverlay and OnionSkinView.

1. Define effect descriptors, composition/layer hierarchy, keyed properties, interpolation, motion paths and onion-skin sampling policy.
2. Implement actual composition/keyframe evaluation and persistent effect state using exact time and curve contracts.
3. Implement keyframe/dope-sheet editing, effect insertion and path/handle edits with one semantic transaction per gesture.
4. Implement composition preview and onion-skin frame requests with bounded caches and stale-result handling.
5. Verify sampled frames/tangents, time remapping, effect order, frame offsets, undo and save/reopen/export equivalence.

**Accept when:** The motion slice edits persistent animation and produces deterministic sampled/exported frames.

**Delegate:** Luna owns browser/timeline/overlay controls and animation fixtures; Sol owns evaluation and compositing.

<a id="wp-074"></a>
## WP-074 — Implement node-based compositing

**Implementation lead:** Sol. **Status:** accepted (typed node graph, groups, keyboard editing and live compositing). **Depends on:** [WP-027](phase-02-controls-data.md#wp-027), [WP-041](phase-03-authoring.md#wp-041), [WP-070](phase-06-video.md#wp-070), [WP-073](phase-06-video.md#wp-073)

**Proposal scope:** [VID-13 NodeGraph](../proposed/07-video-motion.md#vid-13)

**Change area:** NodeGraph and typed graph/evaluation adapter.

1. Define node/port/edge IDs, typed connections, cycle rules, parameter identity and evaluation/cache invalidation.
2. Implement actual node processing with a useful initial set of source, transform, blend and output operations.
3. Implement graph navigation, connect/disconnect, node search/grouping and keyboard/accessibility alternatives.
4. Connect transactions, cancellation, evaluation errors and versioned serialization without embedding engine state in node delegates.
5. Verify incompatible links, cycles, deleted ports, deterministic output, incremental invalidation and undo/roundtrip.

**Accept when:** NodeGraph edits a real typed processing graph and renders validated outputs with recoverable errors.

**Delegate:** Luna owns node/port/wire visuals and graph fixtures; Sol owns graph validation, scheduling and evaluation.

**Astra trigger:** Astra may address a concrete graph scheduling/cache invalidation or cyclic dependency problem with a small reproducer.

<a id="wp-075"></a>
## WP-075 — Implement tracking and rotoscoping

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-044](phase-03-authoring.md#wp-044), [WP-070](phase-06-video.md#wp-070), [WP-073](phase-06-video.md#wp-073)

**Proposal scope:** [VID-15 TrackingPanel](../proposed/07-video-motion.md#vid-15), [VID-16 RotoscopeEditor](../proposed/07-video-motion.md#vid-16)

**Change area:** TrackingPanel, RotoscopeEditor and tracked feature/mask backend.

**Additional obligations:** [ENG-14](supporting-scope.md#eng-14).

1. Define tracked features, coordinate spaces, confidence/failure states and time-varying mask path topology.
2. Choose/integrate actual tracking using synthetic motion/occlusion fixtures, with user correction and worker cancellation.
3. Implement persistent tracking results, offset/rebase operations and keyframed roto paths/masks.
4. Connect masks to real composition output and expose tracking loss rather than silently extrapolating certainty.
5. Verify known transforms, occlusion, topology edits, interrupted analysis, undo and rendered masks across frames.

**Accept when:** Tracking and roto modify actual frame evaluation with measured accuracy and explicit failure behavior.

**Delegate:** Luna owns tracking/mask controls and test clips; Sol owns algorithms, path-time mapping and processing.

<a id="wp-076"></a>
## WP-076 — Implement 3D scene rendering and manipulation

**Implementation lead:** Sol. **Status:** accepted (Quick3D viewport, materials, transform gizmo, graphics-loss recovery and shared scene session). **Depends on:** [WP-040](phase-03-authoring.md#wp-040), [WP-042](phase-03-authoring.md#wp-042), [WP-043](phase-03-authoring.md#wp-043), [WP-073](phase-06-video.md#wp-073)

**Proposal scope:** [VID-19 SceneView3D](../proposed/07-video-motion.md#vid-19), [VID-20 SceneOutliner](../proposed/07-video-motion.md#vid-20), [VID-21 TransformGizmo3D](../proposed/07-video-motion.md#vid-21), [VID-22 MaterialPreview](../proposed/07-video-motion.md#vid-22)

**Change area:** SceneView3D, SceneOutliner, TransformGizmo3D, MaterialPreview and scene renderer adapter.

1. Define scene/entity/camera/material IDs, transform hierarchy, units, picking and render-resource lifecycle.
2. Integrate a functional 3D renderer with a persistent scene model and software/fallback capability reporting where applicable.
3. Implement outliner selection, world/local gizmos and material preview/edit bindings with real scene updates.
4. Connect camera controls, viewport sizing, device loss and native format save/reopen.
5. Verify transform hierarchy, inverse/picking accuracy, material output, multi-view resources, cancel/undo and scene reload.

**Accept when:** A real 3D scene can be viewed, selected, transformed, shaded and persisted through reusable toolkit surfaces.

**Delegate:** Luna owns outliner/material UI and scene fixtures; Sol owns renderer, geometry and resource integration.

**Astra trigger:** Astra is reserved for an unresolved render lifecycle, transform/picking or GPU interoperability issue with a reproducer.

<a id="wp-077"></a>
## WP-077 — Implement rig, UV and texture authoring

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-045](phase-03-authoring.md#wp-045), [WP-073](phase-06-video.md#wp-073), [WP-076](phase-06-video.md#wp-076)

**Proposal scope:** [VID-18 RigEditor](../proposed/07-video-motion.md#vid-18), [VID-23 UVEditor](../proposed/07-video-motion.md#vid-23), [VID-24 TexturePaintView](../proposed/07-video-motion.md#vid-24)

**Change area:** RigEditor, UVEditor, TexturePaintView and rig/mesh/texture evaluation providers.

**Additional obligations:** [ENG-15](supporting-scope.md#eng-15).

1. Define supported bones/constraints/weights, mesh UV topology, texture spaces and non-destructive edit storage.
2. Implement actual rig evaluation and mesh/UV edit operations with valid constraint/topology rules.
3. Implement rig/UV controls, selection synchronization and camera-aware texture painting using the raster pipeline.
4. Persist rig/mesh/texture edits and connect animation evaluation and render/export.
5. Verify known rig poses, constraint cycles, seams/UV islands, paint projection, undo and output after reopen.

**Accept when:** The advanced 3D controls manipulate real geometry/rig/texture state and produce verified scene output.

**Delegate:** Luna owns editor presentation and small rig/mesh fixtures; Sol owns geometry, rigging and projection algorithms.

**Astra trigger:** Use Astra for a minimized constraint, deformation or UV/texture projection correctness problem.

<a id="wp-078"></a>
## WP-078 — Implement durable render queues

**Implementation lead:** Sol. **Status:** accepted (durable queue, view and real PNG/PDF/WAV providers). **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-026](phase-02-controls-data.md#wp-026), [WP-070](phase-06-video.md#wp-070)

**Proposal scope:** [VID-25 RenderQueueView](../proposed/07-video-motion.md#vid-25)

**Change area:** RenderQueueView, render job persistence and media/image/document output adapters.

1. Define immutable render job snapshots, dependencies, output manifests, resource limits and supported resume/checkpoint policy.
2. Implement durable queue storage, scheduling, cancellation/retry and temporary-output cleanup without corrupting finished results.
3. Implement render queue controls and per-job diagnostics over JobModel.
4. Connect actual frame/audio/document render providers, preserving engine ownership and output version/capability metadata.
5. Verify interrupted renders, changed source documents, failed assets/devices/plugins, retry consistency and output integrity.

**Accept when:** Queued jobs produce actual reproducible output and survive failures under the declared resume policy.

**Delegate:** Luna owns queue presentation and failure fixtures; Sol owns job durability and rendering integration.
