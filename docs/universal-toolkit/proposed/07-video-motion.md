<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Video, motion graphics and 3D

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

Reuse [audio/time contracts](06-audio-music.md) for exact time, transport and
shared timeline mechanics, plus [canvas tools](04-canvas-color.md) for spatial
selection. Video trimming, compositing, tracking and 3D evaluation need separate
model policies and engines. Existing studio-shell panels are examples, not the
components proposed below.

<a id="vid-01"></a>
## VID-01 — SourceProgramMonitor

**Priority:** P1. **Purpose:** Frame viewer for source and edited sequence output.

**Contract sketch:** Provider/sequence ID, exact frame position, view role, zoom, channel/display transform, overlays and transport commands.

**Dependencies and boundary:** Frame/decode/compositor adapter and TimeMap; rendering texture ownership is explicit.

**Completion check:** Source and program views seek to known frame IDs, handle missing media and never display a stale frame after rapid seeks.

<a id="vid-02"></a>
## VID-02 — MediaBin

**Priority:** P1. **Purpose:** Asset browser specialized for editorial metadata and organization.

**Contract sketch:** Bin/clip IDs, columns, thumbnails, tags, source timecode, offline/proxy state and search/filter/relink commands.

**Dependencies and boundary:** AssetGrid/VirtualTree/DataTable with media catalog adapter and ResourceResolver.

**Completion check:** Organize/relink media without changing sequence references; proxy/original status remains inspectable per clip.

<a id="vid-03"></a>
## VID-03 — TrimView

**Priority:** P2. **Purpose:** Precision edit-point view for ripple, roll, slip and slide.

**Contract sketch:** Selected edit/clip IDs, adjacent source frames, legal handles, mode and exact delta; preview and commit via timeline policy.

**Dependencies and boundary:** Sequence engine enforces source bounds, linked tracks and ripple behavior.

**Completion check:** Fixture edits show expected adjacent frames and affected clips; cancel/undo restores every linked change.

<a id="vid-04"></a>
## VID-04 — MulticamView

**Priority:** P2. **Purpose:** Synchronized camera grid and angle-switch editing.

**Contract sketch:** Angle IDs, sync offsets, active/program angle, playback frame and switch requests with exact timing.

**Dependencies and boundary:** Multicam sequence/decode engine; audio follow policy is explicit.

**Completion check:** Switch angles during mock playback and reproduce the committed cuts after reopen with stable source sync.

<a id="vid-05"></a>
## VID-05 — TransitionEditor

**Priority:** P2. **Purpose:** Edit transitions between clips or composition states.

**Contract sketch:** Transition ID/type, duration/alignment/handles and parameters; preview warns about unavailable source handles.

**Dependencies and boundary:** Transition renderer and sequence model validate ranges and interpolation.

**Completion check:** Change a transition at a clip boundary, preview and export the same frames, with clear missing-handle behavior.

<a id="vid-06"></a>
## VID-06 — CaptionEditor

**Priority:** P2. **Purpose:** Timed subtitle/caption text and style editing.

**Contract sketch:** Cue IDs, exact in/out times, text/style/language and overlap/reading warnings; waveform/video navigation and export requests.

**Dependencies and boundary:** Caption format/layout engine and TimeMap; accessibility text stays separate from burned-in pixels.

**Completion check:** Split/merge and shift cues, undo, then roundtrip a supported format with timing and language preserved.

<a id="vid-07"></a>
## VID-07 — VideoScopes

**Priority:** P2. **Purpose:** Waveform, RGB parade, vectorscope and histogram panels.

**Contract sketch:** Scope type, analysis/color context, frame ID, scale/units and overlays; demand-driven snapshots avoid analyzing every UI repaint.

**Dependencies and boundary:** Video analysis/color backend supplies correctly transformed measurements.

**Completion check:** Reference ramps/bars produce expected scope values under stated transforms and retain frame association during scrubbing.

<a id="vid-08"></a>
## VID-08 — ColorWheels

**Priority:** P2. **Purpose:** Lift/gamma/gain or other domain-defined grading controls.

**Contract sketch:** Parameter descriptors, ranges, numeric values, reset and linked-luma/chroma policy; gestures use transactions.

**Dependencies and boundary:** Grading/color engine defines the math; widgets do not invent transfer behavior.

**Completion check:** Numeric and wheel changes generate the same parameters and preview/export use the same color pipeline.

<a id="vid-09"></a>
## VID-09 — EffectBrowser

**Priority:** P2. **Purpose:** Discover/apply video, motion or 3D effects and presets.

**Contract sketch:** Effect IDs/capabilities, categories, search and preview jobs; insert into target processing graph by command.

**Dependencies and boundary:** Effect registry, AssetProvider and domain graph; AppearanceStack handles ordered presentation where applicable.

**Completion check:** Applying an incompatible effect is refused with a reason; undo removes a valid insertion and restores graph connections.

<a id="vid-10"></a>
## VID-10 — KeyframeTimeline

**Priority:** P1. **Purpose:** Tracks of animated properties and selectable keys.

**Contract sketch:** Object/property/key IDs, exact key times, interpolation and disclosure; move/scale/copy/paste keys via transactions.

**Dependencies and boundary:** Animation model, TimeMap and CurveEditor; expression and keyframe precedence is explicit.

**Completion check:** Move keys across several properties, cancel/undo and verify interpolation/evaluation at known times.

<a id="vid-11"></a>
## VID-11 — DopeSheet

**Priority:** P2. **Purpose:** Compact exposure and key timing overview for animation.

**Contract sketch:** Layer/frame/exposure IDs, holds, keys and range selection; insert/remove/retime exposure commands.

**Dependencies and boundary:** Animation/sequence model with frame-based timing.

**Completion check:** Extend a held drawing and retime multiple exposures without duplicating artwork or changing unrelated layers.

<a id="vid-12"></a>
## VID-12 — CompositionView

**Priority:** P2. **Purpose:** Spatial preview and editing of a composited scene.

**Contract sketch:** Composition/time IDs, selected layers, render/overlay adapters, safe areas and camera/view mode.

**Dependencies and boundary:** Compositor/evaluation graph and CanvasView; renderer owns effects and cache dependencies.

**Completion check:** Manipulate a layer at a known time, undo and render the same scene after save/reopen.

<a id="vid-13"></a>
## VID-13 — NodeGraph

**Priority:** P2. **Purpose:** General connected-node editor for effects, materials, processing and workflows.

**Contract sketch:** Stable node/port/edge IDs, positions, typed ports, selection, groups and connection preview; request add/connect/disconnect/move.

**Dependencies and boundary:** Graph model validates cycles/types; evaluator and scheduling remain domain-specific.

**Completion check:** Connect/reorder/delete with undo, reject invalid links, and navigate a large graph without every node requiring full detail.

<a id="vid-14"></a>
## VID-14 — MotionPathOverlay

**Priority:** P2. **Purpose:** Edit spatial motion trajectories in a canvas.

**Contract sketch:** Object/property ID, sampled path, key/tangent handles and current time; edits target exact key IDs.

**Dependencies and boundary:** Animation curve/coordinate adapter and CanvasView.

**Completion check:** Editing a path in transformed parent space produces the expected world-space motion and reversible key data.

<a id="vid-15"></a>
## VID-15 — TrackingPanel

**Priority:** P3. **Purpose:** Configure and inspect motion/camera/object tracking jobs.

**Contract sketch:** Track ID, source region/range, algorithm options, progress, confidence and apply-target mapping.

**Dependencies and boundary:** Tracking engine and JobModel; analysis results are versioned and separate from accepted edits.

**Completion check:** Cancel or rerun tracking without applying obsolete results; accepted tracking creates an undoable model operation.

<a id="vid-16"></a>
## VID-16 — RotoscopeEditor

**Priority:** P3. **Purpose:** Animated mask/path editing tied to source frames.

**Contract sketch:** Mask/control-point IDs, topology, key times, feather and propagation preview; explicit accepted versus generated state.

**Dependencies and boundary:** Roto/tracking/interpolation engine and PathEditor/TimeMap.

**Completion check:** Edit shape topology across keys, preview propagation and preserve intended masks through undo and reopen.

<a id="vid-17"></a>
## VID-17 — OnionSkinView

**Priority:** P2. **Purpose:** Temporal comparison overlay for drawing/pose animation.

**Contract sketch:** Current frame, neighboring offsets, opacity/tint and inclusion rules; overlays are display-only.

**Dependencies and boundary:** Frame/composition provider and document color policy.

**Completion check:** Onion frames track edits/retiming and never get serialized as additional artwork or unintentionally exported.

<a id="vid-18"></a>
## VID-18 — RigEditor

**Priority:** P3. **Purpose:** Controls for bones, constraints, deformers and animation handles.

**Contract sketch:** Stable rig element IDs, hierarchy, limits, pose and binding metadata; authoring and posing modes are explicit.

**Dependencies and boundary:** Rig/deformation/constraint engine and scene model.

**Completion check:** Pose within constraints, reset and undo; editing the rig does not silently bake or destroy animation data.

<a id="vid-19"></a>
## VID-19 — SceneView3D

**Priority:** P2. **Purpose:** Embed a 3D scene renderer with camera navigation and selection overlays.

**Contract sketch:** Scene/render adapter, camera, picking, shading/view mode, display context and resource lifecycle callbacks.

**Dependencies and boundary:** 3D renderer owns meshes, materials, lighting and GPU evaluation; toolkit owns interaction/chrome.

**Completion check:** Pick/manipulate objects across resize/device loss and verify serialized scene transforms independent of camera view.

<a id="vid-20"></a>
## VID-20 — SceneOutliner

**Priority:** P2. **Purpose:** Hierarchy of 3D objects, collections and scene instances.

**Contract sketch:** Stable IDs, visibility/lock/instance/type and hierarchy roles with selection/reparent commands.

**Dependencies and boundary:** VirtualTree and 3D scene model; transforms and instancing are domain rules.

**Completion check:** Reparent an instanced/transformed object with explicit keep-world/local policy and restore through undo.

<a id="vid-21"></a>
## VID-21 — TransformGizmo3D

**Priority:** P2. **Purpose:** Axis/plane translation, rotation and scale handles.

**Contract sketch:** Target/world/local transforms, pivot, allowed degrees of freedom and snapping; issue preview/commit transforms.

**Dependencies and boundary:** 3D picking/geometry adapter and transaction service.

**Completion check:** Gizmo and numeric edits agree for parented objects; screen hit targets remain usable across camera distances.

<a id="vid-22"></a>
## VID-22 — MaterialPreview

**Priority:** P2. **Purpose:** Asynchronous rendered material/lighting preview.

**Contract sketch:** Material ID/revision, preview geometry/environment, render quality and color context; cancel stale render requests.

**Dependencies and boundary:** Material renderer and cache; NodeGraph/PropertyBinding edit parameters.

**Completion check:** Rapid parameter changes display only the latest valid preview and release obsolete GPU resources.

<a id="vid-23"></a>
## VID-23 — UVEditor

**Priority:** P3. **Purpose:** Two-dimensional texture-coordinate editor for mesh surfaces.

**Contract sketch:** Mesh/component/UV IDs, islands, seams, selection and packing previews; transform/cut/stitch requests.

**Dependencies and boundary:** Mesh topology, unwrap/packing and texture engines; coordinate ranges are explicit.

**Completion check:** Edit UV islands, undo and export without changing mesh topology or selected component identity accidentally.

<a id="vid-24"></a>
## VID-24 — TexturePaintView

**Priority:** P3. **Purpose:** Paint texture/material channels on a 3D surface.

**Contract sketch:** Mesh/material/channel IDs, brush parameters, stroke samples and projection settings; share paint transactions with canvas tools.

**Dependencies and boundary:** 3D picking and tiled texture-paint engine; StylusHandler supplies input only.

**Completion check:** Paint across a UV seam, undo and reopen; preview and saved texture channels agree under stated color transforms.

<a id="vid-25"></a>
## VID-25 — RenderQueueView

**Priority:** P2. **Purpose:** Domain-aware render/export queue with presets and outputs.

**Contract sketch:** Jobs, composition/range, format/settings, output paths and dependency/cache state; validate then submit to JobModel.

**Dependencies and boundary:** Render/export engines and FormatRegistry; UI never equates queued with successfully written output.

**Completion check:** Retry a failed range and verify outputs are validated and not duplicated; canceled work is reported accurately.
