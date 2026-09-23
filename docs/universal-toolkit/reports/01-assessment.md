<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Assessment: what universality requires

**Status: source-backed assessment and proposed direction, 2026-09-22.**

QindaTK is already a useful foundation for dense desktop interfaces. It has a
coherent theme, its own layout system, template-based controls, persistent dock
arrangements, office-style window bands, a zoomable viewport, a table, telemetry
plots, media displays and pressure/tilt input. An application can use those
today while providing its own models and engines.

The missing layer is a reusable **application and editing framework**: a common
way to represent commands, documents, selection and reversible edits, plus
scalable views for structured data, spatial documents and time-based media.
Without it, each application must independently implement much of the behavior
that makes professional software feel consistent.

## The concrete answer

| Priority | Missing capability | Why it changes what can be built |
| --- | --- | --- |
| P0 | Command registry, contextual dispatch, shortcuts and shared undo | One edit can be invoked consistently from menus, toolbar, palette, keyboard, script and accessibility actions |
| P0 | Document lifecycle and recovery | New/open/save/close, dirty state, failed saves, multiple documents and recovery become reusable behavior |
| P0 | Stable IDs, selection, edit transactions and observable models | Views can survive sorting, filtering, reloads, virtualization and undo without editing the wrong object |
| P0 | Accessibility and input contracts for composite editors | Keyboard, assistive technology, IME and pointer users can perform the same meaningful operations |
| P1 | Virtual trees, asset views and editable two-dimensional grids | Layers, bins, file browsers, spreadsheet-like data and large property sets become first-class surfaces |
| P1 | Canvas tools and document rendering boundary | Selection boxes, handles, guides, snapping and tools can be shared by image, vector, slide and layout apps |
| P1 | Timeline and curve editing framework | Audio, video and motion apps can share time transforms, range selection, snapping and reversible gestures |
| P1 | Rich document and chart surfaces | Office applications can edit structured content and show analytical data beyond plain text and telemetry |
| P1 | Async jobs, asset providers and cache contracts | Loading, decoding, rendering and exports can remain responsive and cancellable |
| P2 | Domain packs | Mixers/MIDI, page layout, workbooks, presentations, color grading, compositing and 3D acquire specialized reusable controls |
| P2 | Platform and format adapters | OS dialogs, printing, hardware I/O, media/document formats and plugins integrate without becoming assumptions in every widget |
| P3 | Collaborative editing and specialist workspaces | Shared documents, presence, node tools, CAD, maps and scientific editors build on established contracts |

See the [proposal index](../proposed/10-proposal-index.md) for concrete component
names and the [roadmap](../roadmap.md) for implementation order.

## What is already substantial

The [current reference](../current/00-type-index.md) covers 101 entries: 78 QML
components, 13 named C++ types/singletons, 8 anonymous C++ helper types, one value
type and one bridge. This includes exported implementation pieces; 101 is not a
marketing count of widgets.

| Strength | Source evidence | Appropriate use now |
| --- | --- | --- |
| Consistent visual language | [Theme](../../../src/core/theme.h), [presets](../../../src/core/theme_presets.cpp), [Density](../../../src/core/density.h) | Dense app chrome and runtime theme/density changes |
| Flexible native composition | [Flex](../../../src/core/flex.h), [Grid](../../../src/core/grid.h), [Stack](../../../src/core/stack.h), [Box](../../../src/qml/Box.qml), [Scroll](../../../src/qml/Scroll.qml) | Complex inspectors, sidebars, tool strips and overlays |
| Real docking model | [DockModel](../../../src/core/dock_model.h), [DockHost](../../../src/qml/DockHost.qml) | Persistent panel arrangements inside one host |
| Broad ordinary controls | [QML sources](../../../src/qml), [controls reference](../../controls.md) | Commands, forms, property panels and navigation chrome |
| Document-shell primitives | [AppWindow](../../../src/qml/AppWindow.qml), [Viewport](../../../src/qml/Viewport.qml), [Ruler](../../../src/qml/Ruler.qml) | A shell around an app-supplied document surface |
| Compact displays | [DataTable](../../../src/qml/DataTable.qml), [Graph](../../../src/core/graph.h), [Meter](../../../src/core/meter.h) | Readout tables, bounded histories and utilization |
| Media/input building blocks | [media reference](../../controls-media.md), [StylusHandler](../../../src/core/stylus_handler.h) | Supplied thumbnails/peaks, simple rulers and pen samples |
| Verification infrastructure | [tests](../../../tests/CMakeLists.txt), [preview](../../../tools/preview/main.cpp) | Unit/scene tests and headless inspection when run |

## Important limits of similarly named pieces

| Existing piece | What it actually supplies | Work still required |
| --- | --- | --- |
| `DataTable` | A ListView of rows, column delegates and sort requests | Editable cells/ranges, horizontal virtualization, frozen panes, clipboard ranges, formula integration |
| `TreeRow` | Indented row with disclosure and optional rename | Hierarchy model, loading, tree navigation, multi-selection, drag reorder |
| `Graph` | Bounded sample-order history plot | Arbitrary axes/XY data, categorical charts, annotations, legends, export, curve editing |
| `Meter` | Normalized magnitude display | Audio measurement semantics, peak hold, clipping, calibrated scale and ballistics |
| `Waveform` | Envelope drawing from precomputed peaks | Decode/cache, multichannel resolution levels, range editing, fades and audio processing |
| `TimeRuler` | Adaptive ticks and scrubbing in caller-defined units | Frame/sample/beat conversions, tempo maps, timecode and transport synchronization |
| `Viewport` | Scroll and scale of one child | Document rendering, tiling, hit testing, tool modes, transforms, selection and rotation |
| `StylusHandler` | Input samples with pressure and tilt | Brush dynamics, smoothing, stroke geometry, rasterization and retained edits |
| `DockFloating` | Floating Item inside DockHost | OS windows, cross-monitor movement and cross-window docking |
| `TextArea` | Multiline input inheriting Qt editing primitives | Paginated rich documents, style hierarchy, tracked revisions and publishing |
| `ColorField` / `Theme` | Color values and UI styling | Managed document color spaces, HDR, proofing, spot colors and production transforms |

These limits are opportunities for adapters and extensions, not claims that Qt
or application C++ cannot already implement the behavior.

## Scope of “any app”

A finite widget catalog cannot implement every future application. A useful
universal toolkit provides composable controls, predictable contracts, custom
rendering/input hooks, and optional domain components. New application semantics
must remain possible without changing the toolkit's core.

For the requested families, target three reusable surface types: **structured
data**, **spatial documents**, and **time-based media**, supported by a shared
command/document layer. This reduces repeated work across word processors,
slides, graphics editors, DAWs and video editors without forcing all of them to
use the same document representation.

Document formats and execution engines remain major projects. A spreadsheet
needs a calculation engine; a DAW needs sample-timed audio processing; a word
processor needs document layout; a video editor needs decode/compositing/export.
The [engine boundary report](03-engine-boundaries.md) assigns those responsibilities.

## Recommended first investment

Build the application contracts and one reusable selection/editing layer first.
Then prove them with three small real apps: an object canvas editor, an editable
table/document app, and a timeline editor backed by a deterministic mock engine.
Only claim professional workflow support after save/reopen, undo/cancel, keyboard
use, failure recovery and large datasets work end to end.

Before expanding, resolve the [source audit findings](04-source-audit.md),
especially documentation/API mismatch and unverified interaction edge cases.
Adding more controls on top of inconsistent ownership would spread that cost
across every future application.
