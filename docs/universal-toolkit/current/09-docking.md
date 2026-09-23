<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Docking model and renderers

Status: **CURRENT — source review dated 2026-09-22.** These entries describe the
working tree, not promised capabilities or a runtime certification. Use
`import QindaTK as Tk` for main-module types. `T` below means QtQuick.Templates.

Each entry describes purpose, consumer behavior and limits. **Declared members**
list locally declared properties, including computed implementation helpers;
inherited Qt properties/signals are not repeated exhaustively. A `ro` suffix
means read-only at the declaration. Method lists can include internal helpers;
the consumer contract is the prose, the existing reference and the linked source.
Object names list source assignments; expressions ending in an ID/index generate
per-item names. Test links show source coverage, not a test run in this audit.

[Book index](../README.md) · [Type index](00-type-index.md) ·
[Source findings](../reports/04-source-audit.md)

## DockModel

C++ authority for panel placement and workspace persistence.

**Source:** [src/core/dock_model.h](../../../src/core/dock_model.h) · **Base:** `QObject` · **Kind:** C++ type.

Panels have stable IDs, modes, zones, lanes, order, shares, groups and floating rectangles. Register panels/presets; dock/dockBeside/float/hide/show/collapse, group operations and geometry setters change arrangement. saveLayout/applyLayout and serialize/deserialize/persist/restore preserve it. Current floats stay in a host; document/session persistence is separate.

**Declared members:** `workspace: QString`, `panels: QVariantList` (ro), `savedLayouts: QStringList` (ro), `presets: QStringList` (ro), `storageKey: QString`, `generation: qulonglong` (ro).

**Declared signals:** `workspaceChanged()`, `layoutChanged()`, `savedLayoutsChanged()`, `storageKeyChanged()`, `panelShown(const QString &panelId)`.

**Source methods:** `modeName()`, `zoneName()`, `zoneNames()`, `registerPanel()`, `unregisterPanel()`, `hasPanel()`, `panelIds()`, `panel()`, `panelsInZone()`, `lanes()`, `zoneExtent()`, `floatingPanels()`, `acceptsZone()`, `setMode()`, `dock()`, `dockBeside()`, `floatPanel()`, `hide()`, `show()`, `collapse()`, `toggle()`, `moveFloating()`, `resizeFloating()`, `setFloatingRect()`, `resizeDocked()`, `setLaneExtent()`, `setShare()`, `bringToFront()`, `groupWith()`, `ungroup()`, `activateInGroup()`, `moveInGroup()`, `reset()`, `registerPreset()`, `applyPreset()`, `saveLayout()`, `applyLayout()`, `deleteLayout()`, `persist()`, `restore()`, `serialize()`, `deserialize()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_dock_model.cpp](../../../tests/cpp/tst_dock_model.cpp), [tst_dock.qml](../../../tests/qml/tst_dock.qml).

## DockHost

QML renderer and gesture coordinator for DockModel.

**Source:** [src/qml/DockHost.qml](../../../src/qml/DockHost.qml) · **Base:** `Item` · **Kind:** QML component.

model/workspace, canvas and default DockPanel declarations produce the working area; canvasItem, panelMenuModel and hiddenPanels support app chrome. showPanel/hidePanel/togglePanel/floatPanel/resetLayout/applyPreset are app-facing helpers; panelDropped reports completion. Numerous root members are internal gesture bookkeeping, not a recommended extension API.

**Declared members:** `model: Tk.DockModel`, `workspace: string`, `canvas: Item`, `panels: alias`, `canvasItem: Item` (ro), `panelMenuModel: var` (ro), `hiddenPanels: var` (ro), `gen: int`, `leftLanes: var`, `rightLanes: var`, `topLanes: var`, `bottomLanes: var`, `centerLanes: var`, `overlayLanes: var`, `floatingIds: var`, `signatures: var`, `shadedIds: var`, `dragging: bool`, `dragId: string`, `dragFromFloating: bool`, `dragCancelled: bool`, `dragPointer: point`, `dropTarget: var`, `edgeTargets: var`, `frames: var`, `floatingFrames: var`, `ready: bool`.

**Declared signals:** `panelDropped(string panelId)`.

**Source methods:** `declarationOf()`, `registerAll()`, `panelTitle()`, `panelInfo()`, `laneExtent()`, `slotInfo()`, `computeMenu()`, `showPanel()`, `hidePanel()`, `togglePanel()`, `floatPanel()`, `resetLayout()`, `applyPreset()`, `isShaded()`, `setShaded()`, `refresh()`, `detachZone()`, `detachFloating()`, `registerFrame()`, `unregisterFrame()`, `registerFloatingFrame()`, `unregisterFloatingFrame()`, `resizeLane()`, `adjustShares()`, `beginDrag()`, `updateDrag()`, `endDrag()`, `cancelDrag()`, `seatCanvas()`. See source for parameters and helper visibility.

**Object names:** `"dockCanvas"`, `"dockZone_overlay"`.

**Coverage pointers:** [tst_dock.qml](../../../tests/qml/tst_dock.qml).

## DockPanel

Nonvisual declaration and persistent content holder for a dock panel.

**Source:** [src/qml/DockPanel.qml](../../../src/qml/DockPanel.qml) · **Base:** `Item` · **Kind:** QML component.

panelId/title/iconName, default placement, extents/minimum sizes, allowedZones, closable/fixedSize/chrome/group and padding describe it. The host reparents contentItem while preserving its state. Register placement with DockModel; avoid copying arrangement into application QML.

**Declared members:** `content: alias`, `contentItem: Item` (ro), `isDockPanel: bool` (ro), `panelId: string`, `title: string`, `iconName: string`, `mode: string`, `zone: string`, `lane: int`, `order: int`, `extent: real`, `share: real`, `minWidth: real`, `minHeight: real`, `floatingRect: var`, `allowedZones: var`, `closable: bool`, `fixedSize: bool`, `chrome: string`, `group: string`, `groupActive: bool`, `padding: real`, `collapsible: bool`.

**Source methods:** `definition()`, `reclaim()`, `seat()`. See source for parameters and helper visibility.

**Object names:** `"dockPanelContent_" + declaration.panelId`.

**Coverage pointers:** [tst_dock.qml](../../../tests/qml/tst_dock.qml).

## DockFrame

Internal renderer for one dock slot or tab group.

**Source:** [src/qml/DockFrame.qml](../../../src/qml/DockFrame.qml) · **Base:** `FocusScope` · **Kind:** QML component; internal renderer.

host/zone/lane/slotIndex/panelIds identify a slot; model facts choose active content, share, collapse and chrome. seat/detach move the persistent content. Published by the globbed module but intended for DockHost internals rather than direct app composition.

**Declared members:** `host: var`, `zone: string`, `lane: int`, `slotIndex: int`, `panelIds: var`, `sideZone: bool` (ro), `slot: var` (ro), `activeId: string` (ro), `collapsed: bool` (ro), `share: real` (ro), `info: var` (ro), `declaration: var` (ro), `chrome: string` (ro).

**Source methods:** `seat()`, `owns()`, `detach()`. See source for parameters and helper visibility.

**Object names:** `"dockFrame_" + activeId`, `"dockTabStrip_" + frame.activeId`, `"dockContent_" + modelData`.

## DockZone

Internal Flex composition of dock lanes and their seams.

**Source:** [src/qml/DockZone.qml](../../../src/qml/DockZone.qml) · **Base:** `Tk.Flex` · **Kind:** QML component; internal renderer.

host/zone/lanes determine orientation and visual order. Live model extents drive geometry. It is exported by the source glob but couples to DockHost's structural snapshot.

**Declared members:** `host: var`, `zone: string`, `lanes: var`, `sideZone: bool` (ro), `reversed: bool` (ro), `seamSign: real` (ro).

**Source methods:** `visualIndex()`. See source for parameters and helper visibility.

**Object names:** `"dockZone_" + zone`, `"dockLane_" + zoneItem.zone + "_" + lane`, `"dockShareSeam_" + zoneItem.zone + "_" + laneItem.lane + "_" + index`, `"dockDivider_" + zoneItem.zone + "_" + modelData.lane`.

## DockTabStrip

Internal dock-group tabs with activation, reorder and tear-out.

**Source:** [src/qml/DockTabStrip.qml](../../../src/qml/DockTabStrip.qml) · **Base:** `Item` · **Kind:** QML component; internal renderer.

host/panelIds/activeId/zone bind a group. Horizontal drags reorder; leaving the strip begins a host drag. This manages panel tabs, whereas ordinary TabStrip manages selection in an application-owned model.

**Declared members:** `host: var`, `panelIds: var`, `activeId: string`, `zone: string`.

**Source methods:** `indexForX()`. See source for parameters and helper visibility.

**Object names:** `"dockTab_" + panelId`.

## DockDivider

Internal seam that reports resize gesture deltas.

**Source:** [src/qml/DockDivider.qml](../../../src/qml/DockDivider.qml) · **Base:** `Rectangle` · **Kind:** QML component; internal renderer.

vertical/share configure orientation/use; dragStarted/dragged(delta)/dragEnded let the host change model extents or shares. Its hit surface uses size.seamTarget independently of the painted seam and Flex basis. It owns no placement state; independent use needs an owner to enforce minimum sizes.

**Declared members:** `vertical: bool`, `share: bool`, `dragging: bool` (ro).

**Declared signals:** `dragStarted()`, `dragged(real delta)`, `dragEnded()`.

## DockFloating

Internal in-host layer for floating panel frames.

**Source:** [src/qml/DockFloating.qml](../../../src/qml/DockFloating.qml) · **Base:** `Item` · **Kind:** QML component; internal renderer.

host and ids drive frames, raising and resize handles. Coordinates and z order come from DockModel. It is an Item, not an operating-system Window; cross-monitor detachable palettes require new functionality.

**Declared members:** `host: var`, `ids: var`.

**Object names:** `"dockFloating_" + panelId`, `"dockContent_" + frame.panelId`.

## DockDropOverlay

Internal visual feedback during panel dragging.

**Source:** [src/qml/DockDropOverlay.qml](../../../src/qml/DockDropOverlay.qml) · **Base:** `Item` · **Kind:** QML component; internal renderer.

host supplies drop targets, preview rectangle and drag ghost. Hit testing lives with DockHost and DockDropLogic.js. It is not a general application drag-and-drop or clipboard service.

**Declared members:** `host: var`.
