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
| F01 | P0, resolved in [WP-002](../implementation/evidence/WP-002.md) | The original [Label.qml](../../../src/qml/Label.qml) declared `selectable` without using it | The integrated read-only selection overlay enables mouse/keyboard selection and copy; focused regression evidence is in the F01 record |
| F02 | P0, resolved in [WP-002 evidence](../implementation/evidence/WP-002.md) | The original [Ruler.qml](../../../src/qml/Ruler.qml) painted with `pixelsPerUnit` but listened to Item.scale changes only | At fixed size/origin a runtime 10→20 zoom change left pixels stale; a focused regression now proves repaint |
| F03 | P0, resolved in [WP-002 evidence](../implementation/evidence/WP-002.md) | The original [Waveform.qml](../../../src/qml/Waveform.qml) divided by `columns - 1` for both traces | Empty and many-peak output is retained; one peak now draws finite full-width geometry |
| F04 | P1, confirmed implementation gap | [SectionHeader](../../../src/qml/SectionHeader.qml) uses TapHandler for collapse; [TimeRuler](../../../src/qml/TimeRuler.qml) has pointer scrubbing and a role but no explicit keyboard/value action contract | Add keyboard and assistive actions to custom composites, then verify real screen-reader interaction; templates cover only some controls |
| F05 | P1, resolved in [WP-003 evidence](../implementation/evidence/WP-003.md) | The original [KeyCap](../../../src/qml/KeyCap.qml) and [Filmstrip](../../../src/qml/Filmstrip.qml) used Row positioners | Converted to `Tk.Flex`, with geometry and RGB pixel-difference evidence |
| F06 | P1, resolved in [WP-003 evidence](../implementation/evidence/WP-003.md) | The original [TableColumn](../../../src/qml/TableColumn.qml), [Ruler](../../../src/qml/Ruler.qml) and [Dialog](../../../src/qml/Dialog.qml) hardcoded chrome defaults | Added theme size roles; runtime density, marker geometry and large-text captures pass |
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
| [architecture](../../architecture.md) and top-level [CMake](../../../CMakeLists.txt) comment | Older text called LayoutInfo the only private API use and omitted newer C++ telemetry/input roles | WP-003 updated both to describe current StylusHandler and Graph/Meter ownership; WP-005 will revise the boundary again if it changes |
| [ToolTip](../../../src/qml/ToolTip.qml) leading comment and a convention row in [controls](../../controls.md) | Attached-tooltip advice conflicted with the repository's child-Tk.ToolTip rule | WP-003 corrected source and controls instructions |
| [media controls](../../controls-media.md) | Intro said the page was awaiting a merge; its features were not linked in the top-level docs map | WP-003 removed the stale note and added the index link |
| [theming](../../theming.md) size table | Media size tokens present in source were not included in that table | WP-003 added the existing media roles and new chrome roles |
| [verification](../../verification.md) older test summary | Did not enumerate newer telemetry/window/stylus coverage | WP-003 updated the test table and direct-build policy |
| [graph header](../../../src/core/graph.h) introductory comment | “No raster repaint” did not describe the added software path | WP-003 corrected the header comment for both render paths |

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
