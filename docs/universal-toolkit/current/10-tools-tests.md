<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Tools, examples, tests and implementation helpers

**CURRENT — source inventory, 2026-09-22.** This chapter covers the non-widget
pieces behind the [101-entry reference](00-type-index.md).

## Build and packaging

| Piece | Current responsibility | Boundary |
| --- | --- | --- |
| [Top-level CMake](../../../CMakeLists.txt) | C++20, Qt 6.8 minimum, Debug/release configuration, optional tests/tools/examples, package config | Declared minimum is not a verified compatibility matrix |
| [Module CMake](../../../src/CMakeLists.txt) | Shared qindatk library, QindaTK QML module version 1.0, QML/JS resource glob, exported CMake target, install rules | Project version is 0.1.0; module version and project version are distinct |
| `QindaTK::qindatk` | CMake target for C++ consumers and the backing QML registration library | Public QuickPrivate dependency follows the stylus header |
| `qindatkplugin` | Main QML plugin, optional for consumers already linking the backing library | Import-path and package installation must still be correct |
| `qindatk_qindaqt` / plugin | Optional-runtime desktop token bridge | Requires QindaQt.Tokens to import/use the bridge |
| [Gentoo packaging](../../../packaging/gentoo/dev-libs/qindatk) | Ebuilds/metadata and optional headless test execution | Packaging exists; this audit did not build/install packages |
| `src/.qmlls.ini` | Build-generated, ignored language-server configuration | Resolves build-tree imports; not public API |

Build commands are documented in the repository's
[verification guide](../../verification.md). Follow the user's configured system
MAKEOPTS unchanged: read `portageq envvar MAKEOPTS` when building; direct builds
must use equivalent configured job/load limits. Do not reduce them using flags,
presets, pools or environment variables. The original audit documentation run
did not build; live integration results are recorded in the
[execution evidence](../implementation/execution-ledger.md).

## qtk-preview

[Source](../../../tools/preview/main.cpp).

| Operation | What it establishes when actually run |
| --- | --- |
| `file.qml --check` | Whether a scene instantiates |
| `file.qml --dump` | Text item tree with objectNames and geometry |
| `file.qml --dump-json` | Machine-readable item tree |
| `file.qml --grab out.png --size 1280x800` | Rendered pixels at a stated size; the PNG still must be inspected |
| `--theme ... --density ...` | Rendering with selected preset and density |
| `--dump-theme` | Resolved runtime tokens |
| `--list-icons` | Runtime icon names |
| `--list-qst` / `--qst ...` | Desktop theme discovery/adoption when preview support is available |
| `--gpu` | Selects the real graphics backend for checking scene-graph rendering; availability still depends on the environment |
| `--wait ...`, `--show`, `--stay`, `--offscreen` | Settling time and preview window/platform behavior |
| `-I ...` / `--import ...` | Additional QML import paths for the inspected scene |

`qst_support.cpp/.h` adapt optional desktop theme publication into the preview
tool. Neither preview nor the QST bridge is a document renderer, app runtime,
performance profiler or installer.

## Generators and diagnostics

| Tool | Purpose | Caveat |
| --- | --- | --- |
| [gen_catalog.py](../../../tools/scripts/gen_catalog.py) | Source-derived JSON type catalog with 101 main, value and bridge entries | Declaration parser covers the audited pointer, multiline and MEMBER forms; it is not compiled Qt runtime reflection |
| [gen_icons.py](../../../tools/scripts/gen_icons.py) | Generates icon path/alias data from Lucide inputs | Generated icon_data.cpp is exempt from the source file length limit |
| [screenshots.sh](../../../tools/scripts/screenshots.sh) | Renders example screenshots and selected theme/density variants | Rendering without reviewing pixels is not visual verification |
| `all_qmllint` build target | Lints module QML | A lint result is distinct from runtime/interaction correctness |

## Example applications

| Example | Demonstrates | Does not establish |
| --- | --- | --- |
| [smoke](../../../examples/smoke/Main.qml) | Small composed toolkit scene | Broad workflow support |
| [gallery](../../../examples/gallery/Main.qml) | Layout, inputs, selection, structure, text, popups, office, docking and telemetry sections | Production app models or format support |
| [docking](../../../examples/docking/Main.qml) | Panels, groups, gestures and workspaces | Native detachable windows |
| [studio-shell](../../../examples/studio-shell/Main.qml) | Bins, monitors, inspectors and a composed demo timeline | Exported timeline editor, media engine or real editing session |
| [office-shell](../../../examples/office-shell/Main.qml) | Application bands, tools and document viewport composition | Word processing, workbook calculation or slide-document persistence |

## Present test sources

At the original audit snapshot there were **10 C++ test source files**
(including the QML runner), **8 QML test files**, and **5 example Main.qml
scenes**. The live implementation now also has catalog fixtures and layout
convention tests; consult the current source for totals. Counts describe
source inventory, not individual assertion totals.

| Source | Coverage visible in source |
| --- | --- |
| [tst_theme.cpp](../../../tests/cpp/tst_theme.cpp) | Role-table/property agreement, presets, density, JSON/QST, color helpers and reduced motion |
| [tst_layout.cpp](../../../tests/cpp/tst_layout.cpp) | Flex/Grid/Stack geometry, implicit sizes and nested composition |
| [tst_grid_tracks.cpp](../../../tests/cpp/tst_grid_tracks.cpp) | Track grammar and sizing |
| [tst_svg_path.cpp](../../../tests/cpp/tst_svg_path.cpp) | Path parsing and error handling |
| [tst_icon.cpp](../../../tests/cpp/tst_icon.cpp) | Built-in paths/aliases and custom registration |
| [tst_dock_model.cpp](../../../tests/cpp/tst_dock_model.cpp) | Dock modes, grouping, geometry, presets and serialization |
| [tst_stylus_handler.cpp](../../../tests/cpp/tst_stylus_handler.cpp) | Synthetic pen/eraser/mouse samples, duplicate suppression and touch exclusion |
| [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp) | Application-window behavior and layout inspection |
| [tst_telemetry.cpp](../../../tests/cpp/tst_telemetry.cpp) | Ring history, ramps, auto scale and normalized meter values |
| [tst_qml.cpp](../../../tests/cpp/tst_qml.cpp) | QtQuickTest runner and build-tree import setup |
| [tst_smoke.qml](../../../tests/qml/tst_smoke.qml) | Basic text, box, panel, icons and theme |
| [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml) | Ordinary inputs, action buttons, sizes and interaction |
| [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml) | Tabs, rows, menus, dialogs, palette, ruler and zoom |
| [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml) | Prompts, colors, rename, wrapping toolbar and viewport zoom |
| [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml) | Graph/series, meter, sparkline and table sort requests |
| [tst_media.qml](../../../tests/qml/tst_media.qml) | Thumbnail states, waveform scale, time ruler, filmstrip, range slider and keycaps |
| [tst_pill_switcher.qml](../../../tests/qml/tst_pill_switcher.qml) | Models, geometry, values and user/programmatic change distinction |
| [tst_dock.qml](../../../tests/qml/tst_dock.qml) | Dock geometry, content retention, groups, seams and drag targets |

Missing evidence for a universal-toolkit claim includes large-model benchmarks,
real screen-reader workflows, platform/GPU matrices, real tablet hardware,
document roundtrips, crash recovery and sustained media/engine integration.
The [roadmap](../roadmap.md) makes these separate acceptance gates.

## Nonvisual implementation support

| Source/helper | Description | API status |
| --- | --- | --- |
| [LayoutAttachedBase](../../../src/core/layout_attached.h) | Shared min/max sizes, order, ignore, alignSelf; invalidates parent layout | Base of anonymous attached objects, not a separately constructible QML type |
| `LayoutContainer` in the same header | C++ invalidation interface for layout containers | Internal cooperation contract |
| [grid_tracks.h/.cpp](../../../src/core/grid_tracks.h) | Parses/sizes tracks for Grid | C++ support; not a visual data grid |
| [svg_path.h/.cpp](../../../src/core/svg_path.h) | Parses SVG path data for icon geometry | Path parser, not an SVG authoring engine |
| [icon_data.h/.cpp](../../../src/core/icon_data.h) | Generated built-in paths and aliases | Data consumed by Icons |
| [theme_presets.h/.cpp](../../../src/core/theme_presets.h) | Presets, derived colors, ladders and QST mapping | Shared source of theme derivations |
| [graph_mesh.h/.cpp](../../../src/core/graph_mesh.h) | Graph geometry generation and plot description | Renderer implementation support |
| [graph_nodes.cpp](../../../src/core/graph_nodes.cpp) | Scene graph node construction/backend choice | Render-thread implementation support |
| [graph_raster.h/.cpp](../../../src/core/graph_raster.h) | Software rendering path for Graph | Fallback implementation, not a general image engine |
| [DockDropLogic.js](../../../src/qml/DockDropLogic.js) | Dock drag/drop target calculations | Shared with dock host; not a general drag-and-drop service |
| [dock_model_ops.cpp](../../../src/core/dock_model_ops.cpp) | Dock movement/group/geometry operations | Model implementation |
| [dock_model_store.cpp](../../../src/core/dock_model_store.cpp) | Layout serialization/presets/QSettings storage | Stores arrangements, not application documents |
| [dock_model_p.h](../../../src/core/dock_model_p.h) | Internal dock helpers | Private header excluded by install rule |

Some non-`_p.h` support headers are installed by the broad header install rule.
Installation alone should not be interpreted as a promise of stable public ABI;
formalize the supported extension surface before third-party consumers rely on
renderer internals.
