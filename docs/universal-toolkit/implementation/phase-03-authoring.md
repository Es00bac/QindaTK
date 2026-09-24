<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 03 — Spatial editing and exact time

**Execution underway.** WP-046 through WP-048 are accepted; WP-040 is in
integrated review. WP-041 through WP-043 have active slices. Other packages
remain planned.

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Integrate and accept package contracts only after their listed dependencies
are accepted. Phase numbers organize the work; the dependency graph controls
readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
The user-directed WP-041/WP-042 presentation slices began early in separate
files; their model integration and acceptance still wait for those dependencies.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-040"></a>
## WP-040 — Build the spatial canvas and rendering boundary

**Implementation lead:** Sol. **Status:** active ([evidence](evidence/WP-040.md); WP-033 dependency accepted, render lifecycle qualification in progress). **Depends on:** [WP-012](phase-01-services.md#wp-012), [WP-015](phase-01-services.md#wp-015), [WP-017](phase-01-services.md#wp-017), [WP-033](phase-02-controls-data.md#wp-033)

**Proposal scope:** [CAN-01 CanvasView](../proposed/04-canvas-color.md#can-01), [CAN-02 ToolController](../proposed/04-canvas-color.md#can-02), [CAN-03 HitTestService](../proposed/04-canvas-color.md#can-03), [PLT-10 RenderSurfaceAdapter](../proposed/08-platform-integrations.md#plt-10)

**Change area:** CanvasView, ToolController, HitTestService, RenderSurfaceAdapter and Viewport compatibility.

**Additional obligations:** [FND-06](supporting-scope.md#fnd-06).

1. Define document/view/device transforms, inverse mapping, tool gesture ownership, picking IDs and render request/resource lifetimes.
2. Implement pan/zoom/rotation, cursor anchoring, touch/trackpad behavior and synchronized views without changing document coordinates.
3. Implement tool state machines and spatial hit queries against an actual object model, with cancel/lost-grab paths.
4. Implement a real software rendering provider and the selected GPU path, visible-region/tile invalidation and context-loss recovery.
5. Exercise 10,000 objects plus a tiled image, transformed picking, mixed display scales and two views; record bounded work and inspected output.

**Accept when:** A real spatial document is rendered and interacted with through explicit model/render boundaries; Viewport compatibility is retained.

**Delegate:** Luna implements overlays, fixture objects and visual scenes; Sol owns transform, picking and render-resource contracts.

**Astra trigger:** Use Astra for an unresolved render-thread lifetime, precision or picking-index problem after a measured prototype.

<a id="wp-041"></a>
## WP-041 — Implement spatial selection, snapping and transforms

**Implementation lead:** Sol. **Status:** active spatial slice ([evidence](evidence/WP-041.md); WP-040 dependency remains in review). **Depends on:** [WP-022](phase-02-controls-data.md#wp-022), [WP-040](phase-03-authoring.md#wp-040)

**Proposal scope:** [CAN-04 SnapModel](../proposed/04-canvas-color.md#can-04), [CAN-05 SelectionOverlay](../proposed/04-canvas-color.md#can-05), [CAN-06 TransformHandles](../proposed/04-canvas-color.md#can-06), [CAN-07 GuideLayer](../proposed/04-canvas-color.md#can-07)

**Change area:** SnapModel, SelectionOverlay, TransformHandles and GuideLayer.

1. Define snap priorities/tolerances, selection bounds, pivot/origin and constrained-transform semantics in document versus screen coordinates.
2. Implement marquee/lasso selection and snap queries against stable object IDs and visible guide/model data.
3. Implement move/resize/rotate handles, numeric/keyboard equivalents and group transforms through EditTransaction.
4. Add guide creation/movement and transformed hit regions with stable objectNames and accessible actions.
5. Verify rotated/scaled groups, negative scale, singular transforms, cross-axis constraints, autoscroll, cancel and one-entry undo.

**Accept when:** Handles, keyboard and inspector perform the same reversible operation with reliable hit/snap behavior under zoom.

**Delegate:** Luna owns handle/guide visuals and fixture cases; Sol owns geometry and snapping algorithms.

<a id="wp-042"></a>
## WP-042 — Add artboards, layers and document navigation

**Implementation lead:** Luna. **Status:** active layer, navigator, alignment and artboard presentation slices ([evidence](evidence/WP-042.md); WP-041 dependency remains open). **Depends on:** [WP-027](phase-02-controls-data.md#wp-027), [WP-041](phase-03-authoring.md#wp-041)

**Proposal scope:** [CAN-08 ArtboardView](../proposed/04-canvas-color.md#can-08), [CAN-09 LayerTree](../proposed/04-canvas-color.md#can-09), [CAN-21 AlignDistributePanel](../proposed/04-canvas-color.md#can-21), [CAN-22 NavigatorView](../proposed/04-canvas-color.md#can-22)

**Change area:** ArtboardView, LayerTree, AlignDistributePanel and NavigatorView.

1. Define artboard bounds, scene hierarchy, lock/visibility state and alignment reference choices through the domain model.
2. Implement virtual layer editing/reordering with valid parent checks and shared scene/tree selection.
3. Implement artboard creation/resize, align/distribute commands and an overview navigator using the same render provider.
4. Connect multiple views and prevent feedback loops in selection and viewport synchronization.
5. Verify hidden/locked objects, nested transforms, reorder undo, many layers/artboards and overview navigation after model reset.

**Accept when:** Layer/artboard/overview controls operate on the same persistent scene with reversible alignment and hierarchy changes.

**Delegate:** Luna builds views against fixed model roles; Sol supplies hierarchy and geometry commands.

<a id="wp-043"></a>
## WP-043 — Implement document color and curve editing

**Implementation lead:** Sol. **Status:** active curve, tangent, gradient and picker presentation plus numerical RGB ICC, typed curve, gradient and ColorPicker model slices ([evidence](evidence/WP-043.md); WP-040 dependency in review). **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-022](phase-02-controls-data.md#wp-022), [WP-040](phase-03-authoring.md#wp-040)

**Proposal scope:** [CAN-11 ColorContext](../proposed/04-canvas-color.md#can-11), [CAN-12 ColorPicker](../proposed/04-canvas-color.md#can-12), [CAN-13 GradientEditor](../proposed/04-canvas-color.md#can-13), [CAN-14 CurveEditor](../proposed/04-canvas-color.md#can-14)

**Change area:** ColorContext, ColorPicker, GradientEditor, CurveEditor and color/curve backend interfaces.

1. Define numeric color spaces/profiles/precision, alpha semantics, display/output transforms and invalid/out-of-gamut reporting independently of Theme.
2. Integrate a real color transform backend and fixture profiles; document the supported precision and proof/output operations.
3. Implement color and gradient editing, stop identity/order/interpolation and multi-target property transactions.
4. Implement generic curve point/tangent editing with defined interpolation/extrapolation and constraint hooks reusable by automation and motion.
5. Verify known color conversions, alpha handling, gradient stops, curve samples/tangents, cancel/undo and GPU/software display agreement within declared tolerance.

**Accept when:** Color and curve controls edit typed document values and have numerical/render evidence for the supported transformations.

**Delegate:** Luna owns picker/stop/tangent controls and fixture presentation; Sol owns color and interpolation semantics.

**Astra trigger:** Consult Astra only for unresolved color precision/transform correctness or curve constraint mathematics with reference fixtures.

<a id="wp-044"></a>
## WP-044 — Implement vector authoring and appearance

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-042](phase-03-authoring.md#wp-042), [WP-043](phase-03-authoring.md#wp-043)

**Proposal scope:** [CAN-17 StrokeStyleEditor](../proposed/04-canvas-color.md#can-17), [CAN-18 FillStyleEditor](../proposed/04-canvas-color.md#can-18), [CAN-19 PathEditor](../proposed/04-canvas-color.md#can-19), [CAN-20 ShapePalette](../proposed/04-canvas-color.md#can-20), [CAN-24 AppearanceStack](../proposed/04-canvas-color.md#can-24)

**Change area:** StrokeStyleEditor, FillStyleEditor, PathEditor, ShapePalette and AppearanceStack.

1. Define persistent path segments, winding/fill rules, stroke joins/caps/dashes, shape parameters and appearance stack order.
2. Implement actual vector geometry/edit operations and rendering, including node/handle changes and supported path combinations.
3. Implement style and shape controls, reorderable appearance effects and property binding to selected objects.
4. Connect clipboard, undo, save/reopen and at least one declared vector export path with explicit unsupported-feature reporting.
5. Verify winding, self-intersections, degenerate paths, transform accuracy and output fixtures; geometry edits must remain reversible.

**Accept when:** Users can create/edit styled vector content and recover equivalent geometry/appearance after persistence and supported export.

**Delegate:** Luna implements style/shape panels and fixtures; Sol owns vector geometry and rendering adapters.

<a id="wp-045"></a>
## WP-045 — Implement raster, masks, brushes and comparison

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-015](phase-01-services.md#wp-015), [WP-019](phase-01-services.md#wp-019), [WP-042](phase-03-authoring.md#wp-042), [WP-043](phase-03-authoring.md#wp-043), [WP-044](phase-03-authoring.md#wp-044)

**Proposal scope:** [CAN-10 MaskChannelPanel](../proposed/04-canvas-color.md#can-10), [CAN-15 BrushPresetView](../proposed/04-canvas-color.md#can-15), [CAN-16 BrushDynamicsEditor](../proposed/04-canvas-color.md#can-16), [CAN-23 CompareView](../proposed/04-canvas-color.md#can-23)

**Change area:** MaskChannelPanel, BrushPresetView, BrushDynamicsEditor, CompareView and tiled raster/brush backend.

**Additional obligations:** [ENG-03](supporting-scope.md#eng-03).

1. Define tiled image storage, layer/mask/channel precision, stroke samples, pressure/tilt dynamics and deterministic brush preset serialization.
2. Implement or integrate real tiled raster editing/compositing and brush processing, with bounded undo storage and color-aware output.
3. Implement mask/channel and brush panels plus split/blink/difference comparison using synchronized render views.
4. Connect pen gestures to one stroke transaction, including device/grab loss, and persist actual pixel/mask changes.
5. Verify known strokes/masks/composites, tile-boundary behavior, recovery, image export and output color; qualify the combined graphics engine with WP-040/043/044.

**Accept when:** The graphics slice paints, masks, transforms, compares, undoes and exports real content with bounded tile/undo work.

**Delegate:** Luna implements brush/channel presentation and deterministic fixture scenes; Sol owns raster/DSP/render behavior.

**Astra trigger:** Astra may resolve a demonstrated tile seam, brush determinism or compositing/color correctness problem.

<a id="wp-046"></a>
## WP-046 — Implement exact time and tempo conversions

**Implementation lead:** Sol. **Status:** accepted; [integrated evidence](evidence/WP-046.md). **Depends on:** [WP-010](phase-01-services.md#wp-010), [WP-012](phase-01-services.md#wp-012)

**Proposal scope:** [AUD-01 TimeMap](../proposed/06-audio-music.md#aud-01)

**Change area:** TimeMap, typed exact-time records and conversion/formatting fixtures.

1. Define sample positions, rational frame rates, frame indices, musical ticks, tempo/signature segments, origin and explicit rounding rules.
2. Implement exact conversions and comparisons in C++, including long durations beyond safe JavaScript integer arithmetic.
3. Implement supported timecode/drop-frame formatting separately from storage and specify discontinuity/negative-time behavior.
4. Expose precision-safe QML facades and shared display/pixel transforms for ruler, timeline, music and video consumers.
5. Run known-answer and roundtrip fixtures around tempo changes, rate changes and timecode boundaries; record precision/error limits.

**Accept when:** Exact positions survive storage, UI transfer and cross-domain conversions with documented rounding and boundary behavior.

**Delegate:** Luna can assemble known-answer tables and time display fixtures; Sol implements and reviews arithmetic.

**Astra trigger:** Use Astra if exact rational/tempo conversion or drop-frame boundary correctness remains unresolved after independent fixtures.

<a id="wp-047"></a>
## WP-047 — Connect transport state to an authoritative clock

**Implementation lead:** Sol. **Status:** accepted. **Depends on:** [WP-011](phase-01-services.md#wp-011), [WP-015](phase-01-services.md#wp-015), [WP-046](phase-03-authoring.md#wp-046)

**Proposal scope:** [AUD-02 TransportModel](../proposed/06-audio-music.md#aud-02), [AUD-03 TransportBar](../proposed/06-audio-music.md#aud-03)

**Change area:** TransportModel, TransportBar and deterministic clock adapter.

1. Define acknowledged play/stop/seek/record, loop/punch state, engine capabilities and bounded GUI snapshot cadence.
2. Implement the facade and a deterministic functional clock adapter for contract tests; label it as a test clock.
3. Implement transport controls and exact time readout through commands, without making QML timers the media clock.
4. Handle out-of-order acknowledgments, denied recording, device errors and transport changes originating outside the GUI.
5. Verify slow GUI frames and seek/play races; retain a mandatory later connection to actual audio/video engines before their workflows qualify.

**Accept when:** Transport represents acknowledged engine state under races and slow GUI updates; mock timing evidence is labeled accurately.

**Delegate:** Luna owns the bar/readout and deterministic scenarios; Sol owns clock/control message semantics.

<a id="wp-048"></a>
## WP-048 — Implement the shared edit timeline

**Implementation lead:** Sol. **Status:** accepted ([integrated evidence](evidence/WP-048.md); D-045 reconciles the independent time-axis and host-media seams). **Depends on:** [WP-016](phase-01-services.md#wp-016), [WP-027](phase-02-controls-data.md#wp-027), [WP-047](phase-03-authoring.md#wp-047)

**Proposal scope:** [AUD-04 EditTimeline](../proposed/06-audio-music.md#aud-04), [AUD-05 TrackHeader](../proposed/06-audio-music.md#aud-05), [AUD-06 ClipItem](../proposed/06-audio-music.md#aud-06), [AUD-07 TimeSelection](../proposed/06-audio-music.md#aud-07), [AUD-08 MarkerLane](../proposed/06-audio-music.md#aud-08)

**Change area:** EditTimeline, TrackHeader, ClipItem, TimeSelection and MarkerLane.

1. Define indexed tracks/clips, exact intervals/source offsets, edit-policy capabilities and visible time/track ranges.
2. Implement bounded track/clip virtualization, shared header scrolling and peak/frame provider requests.
3. Implement move/trim/split/ripple intents, snapping, range/marker editing and drag autoscroll through domain policy adapters.
4. Implement keyboard/numeric editing and accessible track/clip/range navigation with stable identity.
5. Exercise 100 tracks/10,000 clips, long time spans, endpoint crossing, canceled drags and model changes; verify visible-item bounds and exact undo.

**Accept when:** A real timeline model supports precise reversible editing while offscreen clips remain data rather than live QML delegates.

**Delegate:** Luna builds clip/header/range visuals and synthetic sessions; Sol owns time indexes, edit policies and virtualization.

**Astra trigger:** Astra is available for an unresolved exact-time/edit-policy interaction or measured timeline index bottleneck.

<a id="wp-049"></a>
## WP-049 — Implement tempo and automation editing

**Implementation lead:** Sol. **Status:** accepted ([evidence](evidence/WP-049.md)). **Depends on:** [WP-043](phase-03-authoring.md#wp-043), [WP-048](phase-03-authoring.md#wp-048)

**Proposal scope:** [AUD-09 TempoLane](../proposed/06-audio-music.md#aud-09), [AUD-19 AutomationLane](../proposed/06-audio-music.md#aud-19)

**Change area:** TempoLane, AutomationLane and parameter/curve mapping adapters.

1. Define tempo/signature event identity, automation parameter units/ranges, interpolation and read/write/touch/latch capability policy.
2. Implement tempo editing with exact time-map recomputation and explicit beat-locked versus time-locked content behavior.
3. Implement automation point/segment edits through CurveEditor and timeline coordinates, preserving parameter precision.
4. Connect gesture preview/commit/cancel and an engine scheduling interface that consumes timestamped data rather than GUI frames.
5. Verify tempo boundaries, changed parameter ranges, overlapping edits and sampled automation values before/after undo and persistence.

**Accept when:** Automation/tempo edits are exact, reversible and ready for actual engine scheduling without GUI-thread timing authority.

**Delegate:** Luna implements lane presentation and interaction fixtures; Sol owns time remapping and engine scheduling contracts.
