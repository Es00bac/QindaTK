<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Docking

QindaTK's docking has one authority, `Tk.DockModel` (C++,
`src/core/dock_model*.cpp`), and one renderer, `Tk.DockHost` (QML,
`src/qml/Dock*.qml`). The model owns where every panel is; the host draws
that and turns gestures (seam drags, header drags, tab clicks) into model
calls. Nothing about placement is stored in QML, so a saved arrangement
comes back exactly, and an application can drive the arrangement from a
View menu, a preset, or a script without touching the host.

## Concepts

| Term | Meaning |
| --- | --- |
| **workspace** | A named set of panels with its own arrangement (`image`, `paper`, …). The model holds several; a host shows one (`workspace`). |
| **mode** | `docked`, `floating`, `collapsed` (docked, only the header shows), `hidden`. |
| **zone** | Where a docked panel sits: `left`, `right`, `top`, `bottom`, `center`, `overlay`. Top/bottom span the host width; left/right are columns beside the centre column; `center` stacks rows *above the canvas* inside the centre column (Sloom's monitors); `overlay` panels sit over the canvas at its top-right. |
| **lane** | A sub-strip of a zone. Side zones have lanes side by side (columns), the others have stacked lanes (rows). Lane 0 is nearest the host edge. Each lane has one `extent` (width for side zones, height otherwise): the largest extent among its panels. |
| **order** | Position of a slot inside its lane, renumbered 0..n-1 after every move. |
| **share** | Relative weight of a slot inside its lane (stacked slots split the lane by share). Group members share one weight. |
| **group** | Panels in the same lane with the same `group` id form one slot with a tab strip; `groupActive` marks the shown tab. |
| **floatingRect** | Position and size while floating (host coordinates); `zOrder` stacks floating panels. |
| **allowedZones** | Optional list a panel may dock into; absent means any zone. `minWidth`/`minHeight` floor the lane extent and the floating size. `fixedSize` refuses floating resizes. |

## DockModel API

Properties: `workspace` (rw), `panels` (list of maps, notify `layoutChanged`),
`savedLayouts`, `presets`, `storageKey` (QSettings group for
`persist()`/`restore()`; empty disables persistence), `generation` (counter,
bumps on every change).

| Invokable | Semantics |
| --- | --- |
| `registerPanel(workspace, panelId, definition)` | Declares a panel and its defaults (`title, mode, zone, lane, order, extent, share, floatingRect{x,y,width,height}, minWidth, minHeight, fixedSize, closable, chrome, allowedZones, group, groupActive`). Idempotent: a panel that already carries a user arrangement keeps it; a pending arrangement from `deserialize()` is applied on first registration. |
| `unregisterPanel(workspace, panelId)` / `hasPanel(id)` / `panelIds()` | Registry maintenance and queries (active workspace). |
| `panel(id)` | Full map: arrangement keys plus `title, minWidth, minHeight, fixedSize, closable, chrome, allowedZones, x, y, width, height, docked, floating, collapsed, hidden, visible`. |
| `panelsInZone(zone)` | Docked and collapsed panels of a zone, by lane then order. |
| `lanes(zone)` | `[{lane, extent, slots: [{panelIds, activeId, share, collapsed}]}]` — the structure DockHost renders. |
| `zoneExtent(zone)` | Sum of the zone's lane extents. |
| `floatingPanels()` | Floating panels by `zOrder`. |
| `acceptsZone(id, zone)` | Whether the panel's `allowedZones` admits the zone. |
| `setMode(id, mode)` | Mode by name; routes to the calls below. |
| `dock(id, zone, lane = -1)` | Docks at the end of the lane (lane 0, or the given lane); clears its group when the zone changes. |
| `dockBeside(id, targetId, placement)` | `before`/`after` (same lane, next to the target's slot), `tab` (join the target's group), `lane-before`/`lane-after` (a new lane nearer the edge / nearer the canvas). Respects `allowedZones`. |
| `floatPanel(id, x = -1, y = -1)` | Floats (optionally at x,y), leaves its group, raises. |
| `hide(id)` / `show(id)` / `toggle(id)` | Hidden panels keep their arrangement; `show` re-docks (or re-activates its tab) and emits `panelShown`. |
| `collapse(id, bool)` | Docked ⇄ collapsed (header only). |
| `moveFloating(id, x, y)` / `resizeFloating(id, w, h)` / `setFloatingRect(id, x, y, w, h)` | Floating geometry; sizes are floored at the minimum, `fixedSize` refuses resizes. |
| `resizeDocked(id, extent)` / `setLaneExtent(zone, lane, extent)` | Lane extent for every panel in the lane, floored at the largest declared minimum. |
| `setShare(id, share)` | Slot weight (group members together), minimum 0.05. |
| `bringToFront(id)` | Raises a floating panel. |
| `groupWith(id, targetId)` | Joins the target's group (creating it), becomes the active tab. |
| `ungroup(id)` | Leaves the group into its own slot after it; a group of one dissolves. |
| `activateInGroup(id)` / `moveInGroup(id, index)` | Active tab; tab order. |
| `reset()` | Every panel back to its registered defaults. |
| `registerPreset(workspace, presetId, states)` / `applyPreset(id)` / `presets` | Named arrangements: `states` maps panelId → arrangement map (or a bare mode string, PanelLayoutController style); applying starts from the defaults and overlays the preset. |
| `saveLayout(name)` / `applyLayout(name)` / `deleteLayout(name)` / `savedLayouts` | User-named snapshots of the active workspace. |
| `persist()` / `restore()` | Whole state under `storageKey` in QSettings (automatic after every change when the key is set). |
| `serialize()` / `deserialize(json)` | The same state as a JSON string, for custom storage. |

Signals: `workspaceChanged`, `layoutChanged`, `savedLayoutsChanged`,
`storageKeyChanged`, `panelShown(panelId)`.

### Persisted JSON schema (version 1)

```json
{
  "version": 1,
  "workspaces": {
    "image": {
      "layers": {
        "mode": "docked", "zone": "right", "lane": 0, "order": 1,
        "extent": 280, "share": 1, "zOrder": 3,
        "group": "group:layers", "groupActive": true,
        "floatingRect": { "x": 80, "y": 80, "width": 320, "height": 400 }
      }
    }
  },
  "saved": { "image": { "Painting": { "layers": { "...same keys..." : 0 } } } }
}
```

Keys are stable; absent keys keep the current value; unknown panels are
kept pending until they register. `dock_model_store.cpp` is the source of
truth (`arrangementOf`/`applyArrangement`).

## DockHost and DockPanel (QML)

```qml
Tk.DockHost {
    model: Tk.DockModel { storageKey: "myapp/image" }   // optional; a private model is created otherwise
    workspace: "image"
    canvas: Item { ... }                                 // the fixed centre, reparented into `canvasItem`
    Tk.DockPanel {
        panelId: "layers"; title: "Layers"; iconName: "layers"
        zone: "right"; lane: 0; order: 1; extent: 280; share: 1
        minWidth: 160; minHeight: 100; closable: true; chrome: "default"
        group: "image-stack"; groupActive: true
        Tk.Scroll { ... }                                // content; kept alive across moves
    }
}
```

`DockHost` properties: `model`, `workspace`, `canvas`, `canvasItem`,
`panelMenuModel` (`[{panelId, title, hidden}]` by title, for a View ▸ Panels
menu), `hiddenPanels`, `dragging`/`dragId`/`dropTarget` (read-only drag
state), signal `panelDropped(panelId)`. Functions: `showPanel(id)`,
`hidePanel(id)`, `togglePanel(id)`, `floatPanel(id)`, `resetLayout()`,
`applyPreset(id)`, `declarationOf(id)` (the DockPanel), `panelTitle(id)`.

`DockPanel` properties: `panelId`, `title`, `iconName`, `mode`, `zone`,
`lane`, `order` (-1 = declaration index), `extent`, `share`, `minWidth`,
`minHeight`, `floatingRect` (`{x, y, width, height}`), `allowedZones`
(list), `closable`, `fixedSize`, `chrome` (`default` | `compact` (no grip)
| `bare` (no header)), `group`, `groupActive`, `padding`, `collapsible`;
default property = content; `contentItem` = the Item that travels.

Content seating follows `Tk.Box`: a single child that does not anchor or
size itself fills the frame body; several children anchor themselves.

### Rendering and objectNames

| Item | objectName | Notes |
| --- | --- | --- |
| zone | `dockZone_<zone>` | `Tk.DockZone`, a Flex of lanes and seams; hidden when empty |
| lane | `dockLane_<zone>_<lane>` | width/height = model extent (`Tk.Flex.basis`) |
| lane seam | `dockDivider_<zone>_<lane>` | drawn after the lane (left/top/center) or before it (right/bottom) |
| share seam | `dockShareSeam_<zone>_<lane>_<index>` | between slot `index` and `index + 1` |
| frame | `dockFrame_<activeId>` | one slot; named after its active panel |
| content container | `dockContent_<panelId>` | one per member; inactive tabs are invisible, never destroyed |
| tab strip / tab | `dockTabStrip_<activeId>` / `dockTab_<panelId>` | only for groups |
| canvas host | `dockCanvas` | `canvas` is reparented here; the overlay zone (`dockZone_overlay`) sits at its top-right |
| floating frame | `dockFloating_<panelId>` | above the zones, `z` = model zOrder |

Frames are rebuilt only when the *structure* changes (which panels sit in
which lane and slot); extents, shares, the active tab, collapsed flags and
floating rectangles are read live, so seam drags and tab clicks never
destroy the item under the pointer. Before a zone rebuilds, its frames
hand the content back to the DockPanel declarations; the new frames seat
it again (`DockFrame.seat`/`detach`).

### Gestures

| Gesture | Effect |
| --- | --- |
| drag a lane seam | `setLaneExtent` (floored at the panels' minimum) |
| drag the seam between two stacked slots | `setShare` on both, keeping their total |
| click a tab | `activateInGroup` |
| drag a tab sideways, release | `moveInGroup` (index from the release position) |
| drag a tab or a header more than 8px | tear-off: ghost + drop targets |
| drop on a host edge strip (28px) | `dock(id, zone)` |
| drop on a docked frame: centre | `dockBeside(id, target, "tab")` |
| drop on a frame: 25% bands | `before`/`after` along the lane, `lane-before`/`lane-after` across it |
| drop nowhere (torn from a dock) | `floatPanel` at the pointer |
| Escape while dragging | cancel |
| right-click a tab | menu: Take out of group / Float / Close |
| header buttons | collapse (`collapse`), float/dock (`floatPanel`/`dock`), close (`hide`) |
| floating header drag | `moveFloating` (clamped to the host) plus dock targets |
| floating edges/corners | `setFloatingRect` (8 handles) |
| press anywhere on a floating panel | `bringToFront` |
| collapse button on a floating panel | shade to the header (host-side state) |
| Escape in a focused floating panel | `hide` |

## Mapping from QindaStudio's PanelLayoutController

| PanelLayoutController | DockModel |
| --- | --- |
| `registerPanel(ws, id, def)` (`dockZone`, `dockedExtent`, `column`) | same name; `zone`/`extent`/`lane` (old keys still read) |
| `workspace`, `panels`, `panel(id)` | same |
| `setMode`, `dock(id, zone)`, `floatPanel`, `hide`, `collapse`, `toggle` | same (`dock` gains an optional lane) |
| `moveFloating`, `resizeFloating`, `resizeDocked`, `bringToFront` | same |
| `groupWith`, `ungroup`, `activateInGroup` | same, plus `moveInGroup` |
| `reset`, `registerPreset`, `applyPreset`, `presets()` | same (`presets` is also a property) |
| `saveLayout`, `applyLayout`, `deleteLayout`, `savedLayouts` | same |
| `persist`, `restore` (fixed INI keys) | same, keyed by `storageKey`; plus `serialize`/`deserialize` |
| `DockHost.qml` zone columns + `DockPanel.qml` chrome | `Tk.DockHost` + `Tk.DockPanel` declarations; content is reparented, not rebuilt through a `panelContent(id)` chooser |
| — | new: lanes, shares, `dockBeside`, `show`, `setLaneExtent`, `setShare`, `setFloatingRect`, drag-to-dock, `panelShown` |

## Known limits

- Floating panels stay inside the host window; there are no external
  (OS-level) floating windows yet.
- Tab reordering applies on release (no live preview while dragging).
- The overlay zone renders its first lane only, as a top-right stack whose
  slot heights come from each panel's `floatingRect.height`.
- Structural rebuilds re-create frame chrome (not content); a very large
  workspace will notice a frame of latency on dock/float/group operations.


### Name helpers

| Invokable | Returns |
| --- | --- |
| `modeName(mode)` | `"docked"`, `"floating"`, `"collapsed"`, `"hidden"` for a `Tk.DockModel.Mode` value |
| `zoneName(zone)` | the zone string for a `Tk.DockModel.Zone` value |
| `zoneNames()` | all six zone strings in canonical order |
