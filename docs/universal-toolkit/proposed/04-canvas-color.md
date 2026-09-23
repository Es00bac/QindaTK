<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Canvas, vector, color and image authoring

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

Current building blocks are [Viewport/Ruler/StylusHandler](../current/08-media-canvas.md),
color fields and property controls. Spatial editors should share transforms,
selection, snapping and gesture transactions while retaining distinct raster,
vector, slide and page-layout document models. All geometry below is in declared
document units; screen pixels affect hit tolerances and rendering only.

<a id="can-01"></a>
## CAN-01 — CanvasView

**Priority:** P1. **Purpose:** General document-surface host extending Viewport's navigation.

**Contract sketch:** Expose document adapter, visible document rect, transform including rotation, render layers and overlay/tool slots; support fit and synchronized views.

**Dependencies and boundary:** Render adapter with tiling/invalidation, ToolController and document session; Viewport can supply initial scroll/zoom behavior.

**Completion check:** Pan/zoom/rotate a large document while edits and hit tests remain in stable document coordinates.

<a id="can-02"></a>
## CAN-02 — ToolController

**Priority:** P1. **Purpose:** Own active and temporary tools and their gesture lifetimes.

**Contract sketch:** Register tool IDs/capabilities/cursors; activate, suspend, begin/update/end/cancel gestures with document context.

**Dependencies and boundary:** CommandRegistry, EditTransaction and input adapters; domain tools implement semantic operations.

**Completion check:** A pen stroke interrupted by Escape, lost grab or document switch follows a defined cancel/commit policy with no stuck tool.

<a id="can-03"></a>
## CAN-03 — HitTestService

**Priority:** P1. **Purpose:** Resolve pointer/lasso hits to stable objects, subobjects and handles.

**Contract sketch:** Query point/region with transform, tolerance, ordering and visibility/lock policy; return IDs and semantic hit parts.

**Dependencies and boundary:** Domain spatial index and geometry engine; UI may provide overlay hit geometry.

**Completion check:** Hit testing agrees at several zoom levels and resolves overlapping/locked objects predictably.

<a id="can-04"></a>
## CAN-04 — SnapModel

**Priority:** P1. **Purpose:** Shared snapping candidates and feedback.

**Contract sketch:** Sources include grid, guides, object edges/centers and domain points; tolerance and priorities produce candidates with provenance and screen feedback.

**Dependencies and boundary:** Domain geometry adapters and current transform; time snapping can share policy structure but uses time units.

**Completion check:** Dragging near competing targets selects a deterministic candidate; disabling snap mid-drag removes the constraint without jumping.

<a id="can-05"></a>
## CAN-05 — SelectionOverlay

**Priority:** P1. **Purpose:** Visible selection bounds, lasso and marquee feedback.

**Contract sketch:** Selection IDs, document bounds, active tool and transform; expose region-selection requests and accessible object alternatives.

**Dependencies and boundary:** SelectionModel, HitTestService and model geometry.

**Completion check:** Rotated/transformed selections remain aligned and are selectable with keyboard equivalents; cancel removes only transient feedback.

<a id="can-06"></a>
## CAN-06 — TransformHandles

**Priority:** P1. **Purpose:** Reusable resize, rotate, skew and pivot handles.

**Contract sketch:** Target transform/bounds, allowed operations, aspect/center modifiers; preview/commit transforms through transactions.

**Dependencies and boundary:** Geometry adapter validates transforms and supports multi-object selection.

**Completion check:** Resize a rotated multi-selection and undo exactly; minimum-size and singular-transform cases are handled explicitly.

<a id="can-07"></a>
## CAN-07 — GuideLayer

**Priority:** P1. **Purpose:** Rulers, guides, grids and measurement overlays.

**Contract sketch:** Guide IDs/positions/orientation, grid origin/spacing and unit model; add/move/delete requests use transactions.

**Dependencies and boundary:** Ruler extension, SnapModel and document/workspace guide ownership policy.

**Completion check:** Move a guide from a ruler at different zooms, snap an object to it and restore it after save/reopen.

<a id="can-08"></a>
## CAN-08 — ArtboardView

**Priority:** P2. **Purpose:** Navigate and edit multiple artboards/pages on a spatial surface.

**Contract sketch:** Board IDs, rectangles, names, background and export-region metadata; arrange/resize/activate requests.

**Dependencies and boundary:** Document board model and CanvasView; content ownership/clipping belongs to the domain.

**Completion check:** Resize/reorder boards without changing object coordinates unexpectedly; export regions match the saved geometry.

<a id="can-09"></a>
## CAN-09 — LayerTree

**Priority:** P1. **Purpose:** Hierarchy specialized for graphical layers, groups and objects.

**Contract sketch:** Layer IDs, thumbnails, visibility, lock, blend/opacity/mask and selection roles; reorder/group/toggle/rename requests.

**Dependencies and boundary:** VirtualTree plus domain layer model and transactions; do not store a second layer hierarchy in QML.

**Completion check:** Reorder nested layers, undo, and verify rendered stacking and selection; locked/hidden rules apply consistently.

<a id="can-10"></a>
## CAN-10 — MaskChannelPanel

**Priority:** P2. **Purpose:** Inspect and select image masks, channels and clipping relationships.

**Contract sketch:** Channel/mask IDs, thumbnails, enabled/link state and active paint target; emit target/change/relationship commands.

**Dependencies and boundary:** Raster/vector mask model and renderer supply actual coverage and compositing.

**Completion check:** Painting target changes explicitly between layer and mask; toggling a mask is reversible and never edits hidden data accidentally.

<a id="can-11"></a>
## CAN-11 — ColorContext

**Priority:** P1. **Purpose:** Explicit bridge between document colors, display preview and output.

**Contract sketch:** Describe document/working/display/output spaces, profile IDs, precision, rendering intent and proof state; transform requests return tagged results.

**Dependencies and boundary:** Color-management engine supplies transforms; Theme is restricted to interface chrome.

**Completion check:** The same stored color survives theme changes and exports; preview changes only when its declared transform/context changes.

<a id="can-12"></a>
## CAN-12 — ColorPicker

**Priority:** P1. **Purpose:** Complete component/color-plane picker with alpha and numeric spaces.

**Contract sketch:** ColorValue/context, supported models/channels, old/new comparison and gamut feedback; preview/accept/cancel preserve the original on cancellation.

**Dependencies and boundary:** ColorContext, domain palette model and optional screen-sampling adapter.

**Completion check:** Pick/edit/cancel colors with alpha and out-of-gamut values; numeric and visual controls report the same canonical value.

<a id="can-13"></a>
## CAN-13 — GradientEditor

**Priority:** P2. **Purpose:** Edit color/opacity stops and spatial gradient parameters.

**Contract sketch:** Stable stop IDs, positions, interpolation space, type and geometry; add/move/delete/select stops via transactions.

**Dependencies and boundary:** Domain gradient model and ColorContext; renderer defines interpolation and spread modes.

**Completion check:** Stops at the same position, alpha transitions and color-space changes remain deterministic across preview and export.

<a id="can-14"></a>
## CAN-14 — CurveEditor

**Priority:** P1. **Purpose:** Reusable editable curves for animation, automation and adjustments.

**Contract sketch:** Curve IDs, typed x/y domains, knots, interpolation/tangents, selection and constraints; begin/update/commit edits plus numeric entry.

**Dependencies and boundary:** Domain curve evaluator; time/value curves and color transfer curves declare separate units/policies.

**Completion check:** Drag tangents or multiple keys, cancel/undo, and verify evaluated samples match the saved curve within defined tolerance.

<a id="can-15"></a>
## CAN-15 — BrushPresetView

**Priority:** P2. **Purpose:** Search and preview brush presets and recent choices.

**Contract sketch:** Preset IDs, tags, previews and capability metadata; select/import/duplicate requests; lazy generated samples.

**Dependencies and boundary:** Brush engine and preset store provide content; AssetGrid supplies virtualization.

**Completion check:** Switch presets without altering an active stroke; preview work is canceled when items leave the view.

<a id="can-16"></a>
## CAN-16 — BrushDynamicsEditor

**Priority:** P2. **Purpose:** Edit brush response to pressure, tilt, speed and randomization.

**Contract sketch:** Parameter descriptors and response curves with sample-stroke preview; reset and transaction-aware commits.

**Dependencies and boundary:** Brush engine evaluates dynamics and supplies deterministic preview seeds.

**Completion check:** Pressure curve edits change preview predictably and restore after undo/save/reload.

<a id="can-17"></a>
## CAN-17 — StrokeStyleEditor

**Priority:** P2. **Purpose:** Edit vector stroke width, alignment, caps, joins and dashes.

**Contract sketch:** Canonical stroke style with units, dash pattern and miter limits; preview and commit whole-style changes.

**Dependencies and boundary:** Vector model/renderer and PropertyBinding.

**Completion check:** Dashed/closed paths preview consistently with export; unit switching and mixed selection retain exact intent.

<a id="can-18"></a>
## CAN-18 — FillStyleEditor

**Priority:** P2. **Purpose:** Choose solid, gradient, pattern or no fill.

**Contract sketch:** Fill descriptor, palette/pattern references and capability set; nested editors update one transaction.

**Dependencies and boundary:** Domain fill renderer, ColorPicker and GradientEditor.

**Completion check:** Changing fill kind preserves recoverable prior settings and undo restores referenced assets.

<a id="can-19"></a>
## CAN-19 — PathEditor

**Priority:** P2. **Purpose:** Direct vector path/subpath/node editing.

**Contract sketch:** Stable node/segment IDs, open/closed state, node type and tangents; insert/delete/split/join/move requests.

**Dependencies and boundary:** Geometry/path engine owns topology, booleans and tessellation; ToolController owns interaction.

**Completion check:** Edit a closed cubic path, undo and export; selection survives segment insertion and degenerate geometry is rejected/explained.

<a id="can-20"></a>
## CAN-20 — ShapePalette

**Priority:** P2. **Purpose:** Discover parametric shapes and symbols.

**Contract sketch:** Shape definitions expose preview, parameters, capability and insert command; categories/search/recent choices are model-driven.

**Dependencies and boundary:** Shape factory and document object model; AssetProvider supplies previews.

**Completion check:** Inserting and parameterizing a shape produces one editable domain object and one intentional undo transaction.

<a id="can-21"></a>
## CAN-21 — AlignDistributePanel

**Priority:** P1. **Purpose:** Consistent alignment and spacing operations across spatial apps.

**Contract sketch:** Selection, reference mode (selection/key object/page), axis and spacing; preview/result commands use canonical bounds.

**Dependencies and boundary:** Domain geometry adapter defines visual/geometric bounds and locked-object policy.

**Completion check:** Distribute differently sized/rotated objects with known expected bounds, then undo without drift.

<a id="can-22"></a>
## CAN-22 — NavigatorView

**Priority:** P1. **Purpose:** Overview/minimap synchronized with a large canvas.

**Contract sketch:** Document preview, visible rect and transform; dragging/zooming the viewport box requests view changes.

**Dependencies and boundary:** CanvasView and low-resolution render provider.

**Completion check:** Navigate without requesting full-resolution content for the overview; selection and edit state stay independent.

<a id="can-23"></a>
## CAN-23 — CompareView

**Priority:** P2. **Purpose:** Before/after, split/wipe or side-by-side inspection.

**Contract sketch:** Version/asset pairs, linked transforms, split position, channel/color context and difference mode.

**Dependencies and boundary:** Render/analysis providers; comparison math and alignment come from the domain.

**Completion check:** Linked views show the same document region at different resolutions, and stale previews cannot mix revisions.

<a id="can-24"></a>
## CAN-24 — AppearanceStack

**Priority:** P2. **Purpose:** Inspect ordered fills, strokes, filters and nondestructive adjustments.

**Contract sketch:** Effect IDs, enabled/order/parameters/masks and unsupported state; reorder/bypass/edit requests use the model.

**Dependencies and boundary:** Domain processing/effect graph and PropertyBinding.

**Completion check:** Reorder or bypass an effect, undo, save/reopen and reproduce the render using stable parameter IDs.
