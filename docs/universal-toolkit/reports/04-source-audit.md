<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Source audit and confidence limits

**Snapshot: 2026-09-22**, HEAD `89d5ca39a13c974d2877597a39d0fe6db88fbaf0` plus
existing local edits. This is a source review. No current runtime defect below
is claimed to have been reproduced unless explicitly stated; no runtime tests
were executed for this documentation task.

## Confirmed implementation boundaries

| ID | Finding and evidence | Consequence | Recommended work |
| --- | --- | --- | --- |
| B01 | [DataTable](../../../src/qml/DataTable.qml) uses a row ListView and a Repeater for visible columns; `recordAt` calls `get()` or array indexing | Row virtualization exists; broad QAbstractItemModel support, two-axis virtualization and cell editing are not established | Add a standard model adapter and separate editable grid contract |
| B02 | [TreeRow](../../../src/qml/TreeRow.qml) is a ListRow with depth/disclosure/rename | No reusable tree view/model is supplied | VirtualTree with stable selection, expand state, lazy children and keyboard navigation |
| B03 | [Graph](../../../src/core/graph.h) stores scalar series with fixed capacity; [render paths](../../../src/core/graph_nodes.cpp) include GPU and software handling | Useful histories, without explicit x data, chart axes or editable curves | Keep telemetry behavior; add chart/curve models separately |
| B04 | [Waveform](../../../src/qml/Waveform.qml) expects pre-reduced peaks; [Filmstrip](../../../src/qml/Filmstrip.qml) expects URLs | Media decoding, timing and caches are host responsibilities | Asset/peak/frame provider interfaces |
| B05 | [DockFloating](../../../src/qml/DockFloating.qml) is an Item, and dock rectangles are in host coordinates | “Floating” does not mean native detachable windows | Window/session host adapter before cross-monitor docking |
| B06 | [CommandPalette](../../../src/qml/CommandPalette.qml) filters command records and emits an ID | Command execution, shortcuts and undo are application work | Shared command context/registry and undo service |
| B07 | [Viewport](../../../src/qml/Viewport.qml) scales a single child's implicit surface | A document rendering and editing model is still required | Tiling/render hooks, selection overlays, tools and transactions |
| B08 | [studio timeline example](../../../examples/studio-shell/TimelineCanvas.qml) composes demo tracks/clips in application QML | There is no exported timeline editor type or edit engine | Extract a reusable timeline contract only after defining model/time ownership |
| B09 | [AppWindow](../../../src/qml/AppWindow.qml) provides bands and palette; [TextArea](../../../src/qml/TextArea.qml) wraps a template input | “Office shell” is not an Office document engine | Document session and domain views |
| B10 | [Meter](../../../src/core/meter.h) renders a normalized value, optionally segmented/ramped | Professional audio measurements are not part of its semantics | Dedicated audio meter model, ballistics, calibration and tests |

## Source defects and risks to triage

Priority here indicates what to resolve before relying on the affected
contract. An unverified risk needs a focused reproduction before a code change.

| ID | Priority/status | Evidence | Effect and next check |
| --- | --- | --- | --- |
| F01 | P0, confirmed no-op property | [Label.qml](../../../src/qml/Label.qml) declares `selectable` but never uses it | A caller cannot enable selection through that property. Implement a real selectable text path or deprecate/document the property; test selection/copy |
| F02 | P0, source risk | [Ruler.qml](../../../src/qml/Ruler.qml) paints using `pixelsPerUnit` but listens to `onScaleChanged`, not `onPixelsPerUnitChanged` | Verify a zoom change with constant size/origin; if ticks remain stale, add correct invalidation and a rendered regression |
| F03 | P0, source risk | [Waveform.qml](../../../src/qml/Waveform.qml) divides by `columns - 1` in both trace loops | A one-peak array produces undefined coordinates mathematically. Verify one/zero/many peaks and add a defined one-column path |
| F04 | P1, confirmed implementation gap | [SectionHeader](../../../src/qml/SectionHeader.qml) uses TapHandler for collapse; [TimeRuler](../../../src/qml/TimeRuler.qml) has pointer scrubbing and a role but no explicit keyboard/value action contract | Add keyboard and assistive actions to custom composites, then verify real screen-reader interaction; templates cover only some controls |
| F05 | P1, confirmed convention drift | [KeyCap](../../../src/qml/KeyCap.qml) and [Filmstrip](../../../src/qml/Filmstrip.qml) use Row positioners | They depart from the repository's one-layout-model rule. Convert carefully with geometry/pixel comparison |
| F06 | P1, confirmed token coverage gap | [TableColumn](../../../src/qml/TableColumn.qml) defaults width/minWidth to 80/24; [Ruler](../../../src/qml/Ruler.qml) hardcodes marker size 9; [Dialog](../../../src/qml/Dialog.qml) declares width 420 | Classify domain values versus chrome metrics and derive missing theme roles; test density and text growth rather than replacing every numeric constant blindly |
| F07 | P1, resolved in [WP-004](../implementation/evidence/WP-004.md) | The original [gen_catalog.py](../../../tools/scripts/gen_catalog.py) omitted `QML_VALUE_TYPE`, the bridge, pointer-name spacing, MEMBER properties and multiline invokables | The revised source parser and fixtures cover the cited cases. Its output remains a source inventory, not compiled runtime reflection |
| F08 | P1, integration risk | [StylusHandler](../../../src/core/stylus_handler.h) derives from a Qt private class; [src/CMakeLists.txt](../../../src/CMakeLists.txt) publicly links QuickPrivate | Test supported Qt versions and isolate private dependencies. Public consumers including the stylus header inherit that compatibility exposure |
| F09 | P2, source persistence boundary | [DockHost](../../../src/qml/DockHost.qml) stores floating shade IDs as host state | Distinguish transient shade presentation from persisted DockModel collapse/placement; define restart behavior before promising it |

These are deliberately bounded findings, not an assertion that the repository
was exhaustively reviewed for every possible runtime bug. Proposed new feature
work should start with the relevant source and focused regressions.

## Documentation drift

| Existing material | Discrepancy | Treatment in this book |
| --- | --- | --- |
| `docs/catalog.json` at audit start | 93 entries; six source components were absent: Filmstrip, KeyCap, RangeSlider, Thumbnail, TimeRuler, Waveform | First regenerated to 99 entries; WP-004 later expanded the generator and checked-in catalog to 101 entries, including the value type and bridge |
| [architecture](../../architecture.md) and top-level [CMake](../../../CMakeLists.txt) comment | Older text calls LayoutInfo the only private API use and omits newer C++ telemetry/input roles | Current reference includes StylusHandler/telemetry and cites actual linking |
| [ToolTip](../../../src/qml/ToolTip.qml) leading comment and a convention row in [controls](../../controls.md) | Attached-tooltip advice conflicts with the repository's child-Tk.ToolTip rule and detailed documentation | Reference explicitly uses child instances |
| [media controls](../../controls-media.md) | Intro says the page is awaiting a merge; its features are not linked in the top-level docs map | This book indexes media alongside all current controls |
| [theming](../../theming.md) size table | Media size tokens present in source are not included in that table | Current media entries name actual token roles and point to source |
| [verification](../../verification.md) older test summary | Does not enumerate all newer telemetry/window/stylus coverage | Tools/tests chapter inventories present test files without claiming a fresh pass |
| [graph header](../../../src/core/graph.h) introductory comment | “No raster repaint” does not describe the added software path | Reference describes both current render paths |

Existing user changes in source, README, controls, catalog, gallery, icon tooling
and tests were present before the audit. Documentation additions do not imply
those changes were authored or verified by this task.

## Evidence levels and what remains unmeasured

| Evidence | Established here | Not established here |
| --- | --- | --- |
| Source inventory | Files, declarations, model/rendering boundaries and test-source presence | Runtime success, exported binary ABI or installed package contents |
| Test source review | Tests exist for layouts, themes, dock persistence, controls, media and stylus samples | The tests passed against this exact working tree |
| Source performance design | DataTable uses ListView reuse; Graph uses bounded rings | Frame time, peak memory, latency or million-item scalability |
| Accessibility declarations | Many template controls supply names/roles; some custom composites are incomplete | End-to-end assistive technology usability or conformance |
| Platform dependencies | Qt 6.8 minimum and private-header usage are visible | Windows/macOS portability, all GPU backends, mixed-DPI or tablet hardware coverage |
| Product documentation | Representative professional workflows exist | Product feature parity, legal format access or compatibility of a future adapter |

See [verification](../verification.md) for checks performed on the generated
documentation, and [roadmap](../roadmap.md) for runtime acceptance gates.
