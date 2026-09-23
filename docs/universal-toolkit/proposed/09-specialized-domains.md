<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Additional application domains

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

These optional surfaces test the claim that QindaTK can host application types
beyond the named suites. They reuse the same command/model/render contracts.
They are not prerequisites for a first Office or creative app, and no regulated,
scientific or engineering accuracy is implied by the presence of a viewer.

<a id="ext-01"></a>
## EXT-01 — CodeEditor

**Priority:** P2. **Purpose:** Large structured text editor for code, scripts and markup.

**Contract sketch:** Document buffer, logical cursor/selection, syntax/semantic tokens, folding, diagnostics and completion; expose edits and navigation commands.

**Dependencies and boundary:** Text-buffer/language-service backend, search and undo adapters; TextArea alone is not the proposed implementation.

**Completion check:** Edit a large Unicode file with IME, multi-cursor/folding and asynchronous diagnostics without stale positions or lost edits.

<a id="ext-02"></a>
## EXT-02 — TerminalView

**Priority:** P3. **Purpose:** Embed a terminal surface with explicit process and input ownership.

**Contract sketch:** Terminal buffer/cursor, selection, font/theme and process-state facade; send input/resize and copy operations through an adapter.

**Dependencies and boundary:** Terminal parser/PTY/process backend; shell execution is an application capability, not a text-control side effect.

**Completion check:** Resize and scroll under output load while selection/copy, Unicode and process exit behave predictably.

<a id="ext-03"></a>
## EXT-03 — DiagramCanvas

**Priority:** P2. **Purpose:** Create connected diagrams with reusable stencils.

**Contract sketch:** Shape/port/edge IDs, labels, geometry and routing constraints; connect/move/align commands share canvas transactions.

**Dependencies and boundary:** Diagram graph, orthogonal/curved routing and layout engine; stencil assets are provider-backed.

**Completion check:** Move a connected group, reroute edges, undo and roundtrip the diagram with stable port associations.

<a id="ext-04"></a>
## EXT-04 — CadViewport

**Priority:** P3. **Purpose:** Host precise 2D/3D engineering geometry and constraints.

**Contract sketch:** Geometry/component IDs, canonical units, dimensions, constraints and pick/transform commands; display approximation is separate.

**Dependencies and boundary:** CAD geometry/constraint kernel and renderer; toolkit does not certify precision or physical correctness.

**Completion check:** Known constrained fixtures retain exact model dimensions across zoom, unit display changes, undo and export.

<a id="ext-05"></a>
## EXT-05 — MapView

**Priority:** P3. **Purpose:** Projection-aware map and spatial-layer surface.

**Contract sketch:** Camera/extent, coordinate reference system, tile/vector providers, layer visibility and selection; position queries carry declared CRS.

**Dependencies and boundary:** GIS projection, spatial index, tile/network and analysis backends.

**Completion check:** Switch projection and pick known coordinates correctly; offline/stale tiles are labeled and never treated as fresh analysis results.

<a id="ext-06"></a>
## EXT-06 — ScientificPlot

**Priority:** P3. **Purpose:** Scientific figure composition with calibrated axes and annotations.

**Contract sketch:** Typed datasets/units, transforms, error/uncertainty metadata, panels and export layout; interactive cursors select data coordinates.

**Dependencies and boundary:** ChartView plus domain math/statistics and precision-aware dataset adapters.

**Completion check:** Export a known figure with labeled units and uncertainty, preserving values and readable typography at target output size.

<a id="ext-07"></a>
## EXT-07 — ImageSliceView

**Priority:** P3. **Purpose:** Navigate large multidimensional image/volume slices.

**Contract sketch:** Dataset/axis IDs, calibrated coordinates, slice index, window/level, channel and overlay transforms.

**Dependencies and boundary:** Array/volume provider and domain imaging renderer; pixel intensity interpretation is explicit.

**Completion check:** Linked views track the same calibrated point and reject overlays with incompatible geometry/metadata.

<a id="ext-08"></a>
## EXT-08 — VolumeView

**Priority:** P3. **Purpose:** Interactive volumetric rendering surface.

**Contract sketch:** Volume handle/revision, camera, clipping planes, transfer function, sampling quality and pick results.

**Dependencies and boundary:** Volume renderer and out-of-core data provider through RenderSurfaceAdapter.

**Completion check:** Manipulate clipping/transfer functions on large data with cancellation and consistent coordinate mapping after reload.

<a id="ext-09"></a>
## EXT-09 — TransferFunctionEditor

**Priority:** P3. **Purpose:** Map measured scalar values to color and opacity for visualization.

**Contract sketch:** Domain/units, stable stops/curves, interpolation, histogram and preset references; edits are document settings.

**Dependencies and boundary:** Volume/scientific renderer and ColorContext; distinct from ThemeRamp's UI telemetry purpose.

**Completion check:** Known scalar ramps preview and export consistently, with out-of-range/missing values explicitly represented.

<a id="ext-10"></a>
## EXT-10 — AssetDependencyView

**Priority:** P2. **Purpose:** Inspect relationships among files, documents, media and generated outputs.

**Contract sketch:** Resource IDs, dependency edges, availability/revision and impact queries; select/relink/rebuild requests.

**Dependencies and boundary:** ResourceResolver, build/render dependency graph and NodeGraph/VirtualTree views.

**Completion check:** Replacing an input identifies affected outputs and schedules only valid rebuilds; cycles/missing dependencies are visible.

<a id="ext-11"></a>
## EXT-11 — TraceView

**Priority:** P3. **Purpose:** Explore timed events, flame-like hierarchies and resource traces.

**Contract sketch:** Event/span IDs, exact timestamps, thread/resource tracks and aggregation metadata; zoom, search and selection are linked.

**Dependencies and boundary:** Trace parser/index/aggregation backend and timeline/chart adapters.

**Completion check:** Inspect a large trace without per-event delegates and preserve exact selected timestamps when zooming far out.
