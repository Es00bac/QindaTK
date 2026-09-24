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
| **mode** | `docked`, `floating` (inside DockHost), `collapsed` (docked, only the header shows), `hidden`, `detached` (native window). |
| **zone** | Where a docked panel sits: `left`, `right`, `top`, `bottom`, `center`, `overlay`. Top/bottom span the host width; left/right are columns beside the centre column; `center` stacks rows *above the canvas* inside the centre column (Sloom's monitors); `overlay` panels sit over the canvas at its top-right. |
| **lane** | A sub-strip of a zone. Side zones have lanes side by side (columns), the others have stacked lanes (rows). Lane 0 is nearest the host edge. Each lane has one `extent` (width for side zones, height otherwise): the largest extent among its panels. |
| **order** | Position of a slot inside its lane, renumbered 0..n-1 after every move. |
| **share** | Relative weight of a slot inside its lane (stacked slots split the lane by share). Group members share one weight. |
| **group** | Panels in the same lane with the same `group` id form one slot with a tab strip; `groupActive` marks the shown tab. |
| **floatingRect** | Position and size while floating (host-local coordinates); `zOrder` stacks floating panels. In `detached` mode, the same rectangle is native global logical geometry and `detachedScreenId` records the screen name. On restore, WindowHost clamps an absent screen to an available one. |
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
| `panel(id)` | Full map: arrangement keys plus `title, minWidth, minHeight, fixedSize, closable, chrome, allowedZones, x, y, width, height, docked, floating, collapsed, hidden, detached, visible`. |
| `panelsInZone(zone)` | Docked and collapsed panels of a zone, by lane then order. |
| `lanes(zone)` | `[{lane, extent, slots: [{panelIds, activeId, share, collapsed}]}]` — the structure DockHost renders. |
| `zoneExtent(zone)` | Sum of the zone's lane extents. |
| `floatingPanels()` | Floating panels by `zOrder`. |
| `detachedPanels()` | Panels assigned to native windows, by stable panel ID. |
| `acceptsZone(id, zone)` | Whether the panel's `allowedZones` admits the zone. |
| `setMode(id, mode)` | Mode by name; routes to the calls below. |
| `dock(id, zone, lane = -1)` | Docks at the end of the lane (lane 0, or the given lane); clears its group when the zone changes. |
| `dockBeside(id, targetId, placement)` | `before`/`after` (same lane, next to the target's slot), `tab` (join the target's group), `lane-before`/`lane-after` (a new lane nearer the edge / nearer the canvas). Respects `allowedZones`. |
| `floatPanel(id, x = -1, y = -1)` | Floats (optionally at x,y), leaves its group, raises. |
| `detachPanel(id, globalRect = {})` | Assigns the panel to a native window; keeps the last dock zone and uses global logical geometry. `setDetachedScreen(id, screenId)` records the preferred screen. |
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
| `serialize()` / `deserialize(json)` | The same state as a JSON string, for custom storage. Restore accepts schema version 1 and validates the complete payload before applying any changes; unsupported versions and malformed nested arrangements leave model and pending-panel state untouched. Unknown panel IDs remain pending for later registration. |

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

`Tk.DetachedPanelHost` connects a `DockModel` to a `WindowHost` and seats the
same `DockPanel.contentItem` in a native `QQuickWindow`. Give it to DockHost as
`detachedHost`; DockHost registers declarations automatically. Call
`dockHost.detachPanelNative(panelId)` or `dockHost.reattachPanel(panelId)`.
Closing a detached window re-docks its panel. `WindowHost` owns the native
window, while DockModel remains the authority for placement and geometry.
Its `windows` list maps panel IDs to WindowHost IDs; the native window gets
`detachedPanelWindow_<panelId>` as its stable objectName. A restored window
on a missing screen is clamped to the available primary screen. Existing
`floating` panels remain inside DockHost.

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

Floating rectangles belong to the host coordinate space. Once the host has
a nonzero viewport, and after either viewport dimension changes, DockHost
calls `DockModel.constrainFloatingToViewport(width, height,
reachableHeaderWidth, headerHeight)`. The model preserves each panel's size
and moves only its origin. At least a header-width strip and the header's
height stay reachable; if the host is smaller than those dimensions, the
reachable dimensions shrink to the viewport. This is also applied after
restore. The operation is idempotent and emits a layout change only if a
rectangle moved.

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
| divider focus ring | `<divider objectName>_focus` | visible theme focus outline while a dock divider is focused |
| frame | `dockFrame_<activeId>` | one slot; named after its active panel |
| content container | `dockContent_<panelId>` | one per member; inactive tabs are invisible, never destroyed |
| tab strip / tab / focus ring | `dockTabStrip_<activeId>` / `dockTab_<panelId>` / `dockTabFocus_<panelId>` | only for groups; PageTabList/PageTab semantics, active selection, visible focused ring |
| canvas host | `dockCanvas` | `canvas` is reparented here; the overlay zone (`dockZone_overlay`) sits at its top-right |
| floating frame | `dockFloating_<panelId>` | above the zones, `z` = model zOrder |

Frames are rebuilt only when the *structure* changes (which panels sit in
which lane and slot); extents, shares, the active tab, collapsed flags and
floating rectangles are read live, so seam drags and tab clicks never
destroy the item under the pointer. Before a zone rebuilds, its frames
hand the content back to the DockPanel declarations; the new frames seat
it again (`DockFrame.seat`/`detach`).

### Gestures

Dock divider hit surfaces use `Theme.size.seamTarget`; the painted seam and
Flex layout basis continue to use `Theme.size.seam`. In Touch density the
invisible surface reaches 48 logical px without widening the dock lane.

| Gesture | Effect |
| --- | --- |
| drag a lane seam | `setLaneExtent` (floored at the panels' minimum) |
| drag the seam between two stacked slots | `setShare` on both, keeping their total |
| focus a vertical seam, then Left/Right | emit a negative/positive local horizontal delta through the same resize path as dragging |
| focus a horizontal seam, then Up/Down | emit a negative/positive local vertical delta through the same resize path as dragging |
| accessible increase/decrease on a seam | emit a positive/negative `Theme.space.lg` delta on the seam axis through the host callbacks |
| click a tab | `activateInGroup` |
| focus a tab, then Left/Right or Home/End | move focus and activate through `activateInGroup`; the model remains the selection authority |
| Space/Return/Enter or accessible press on a tab | `activateInGroup`; emits `DockTabStrip.tabActivated(panelId)` only when a different tab is accepted |
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
| floating edges/corners | `setFloatingRect` (8 focusable handles; themed expanded targets) |

Floating resize handles use `Theme.size.seamTarget` across their edge, and
corner squares use a distinct target up to that size. In Touch density the
targets reach 48 logical px while the frame and its painted header/content
geometry remain unchanged. Corners occupy the ends of each edge target, so
edge and corner hit regions meet without overlapping. The handles expose
named `Accessible.Splitter` actions, a visible focus outline, arrow-key
resizing along their applicable axes, and accessible increase/decrease.
Increase grows the applicable dimensions from either side; decrease shrinks
them. Arrow keys move the selected edge in the pressed direction.
All accepted input routes call `DockModel.setFloatingRect` and honor the
panel's minimum size. On panels smaller than the target layout, corner targets
shrink to at most half the frame's width and height; an edge handle whose
remaining non-overlapping segment cannot contain a target is disabled. This
preserves distinct, safe hit areas but cannot guarantee 48px targets for a
panel whose minimum dimensions are below the space required for eight handles.
| press anywhere on a floating panel | `bringToFront` |
| collapse button on a floating panel | shade to the header (transient host presentation state) |
| Escape in a focused floating panel | `hide` |

Floating shade state belongs to DockHost presentation. It remains while the
same host rebuilds frames, clears when the host switches workspaces, and is
not included in DockModel serialization. Restarting the application restores
floating panels at their saved size rather than shaded.

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

### Small windows: preserve the workspace through scrolling

`DockHost` renders the model's lane extents and shares; it does not invent a
minimum canvas size or rearrange panels when its window shrinks. If those
extents exhaust the host, the remaining canvas can reach zero size.
Applications must choose their presentation policy for that case.

The docking example wraps its complete workspace in `Tk.Scroll` with both
axes enabled and an `Item` declaring a 1280×800 implicit minimum. The actual
workspace grows with a larger viewport. At smaller sizes it keeps that
minimum and scrolls. The minimum is example data, chosen for its registered
panels and canvas; applications must choose a minimum suited to their own
content. The wrapper is an Item because Flex owns its implicit size.

`dockingViewport` is the example's stable viewport objectName. Arrow keys
and Page Up/Down scroll it when focused; Home/End reach vertical ends.
Focus reveal and `ensureVisible(item)` can bring individual controls or
panels into view. Dock drag coordinates stay local to the host even when
its ancestor scrolls. Window resize changes neither DockModel placement nor
persisted floating rectangles. Content scrolling and tab selection remain
ordinary user actions.

This recipe covers the example's default arrangement, including its History
floating panel. It is not automatic recovery of arbitrary floating panels
placed outside the chosen minimum, nor automatic text reflow within every
panel. Applications needing an adaptive single-panel presentation must
specify selection, focus, drop targets and canvas behavior separately rather
than silently rewriting a saved arrangement on resize.

## Keyboard and accessible panel placement

Each docked or floating header includes a named **Place <panel title>** button
(`dockPlacement_<panelId>`). Its keyboard activation and accessible press open
one host-owned `DockPlacementDialog`. Shift+F10 from a panel or focused tab
opens the same dialog; a tab's context menu also offers **Place panel…**.
The dialog uses a named destination ComboBox with ordinary keyboard selection:

- Dock at the end of any permitted zone.
- Dock before/after another visible docked panel, in an adjacent lane, or as
  its tab. Destinations are filtered through `DockModel.acceptsZone`.
- Choose an explicit tab position for a grouped panel. Reordering preserves
  the active panel, matching pointer reorder.
- Move a floating panel using named horizontal/vertical NumberFields. Arrow
  keys and their accessible increase/decrease actions change a proposed
  position by `Theme.space.lg`. Bounds keep the whole frame within the host
  when it fits; an oversized frame is anchored at zero on that axis.

**Apply** makes one model placement call and emits
`DockHost.panelPlacementAccepted(panelId)` once. **Cancel** leaves arrangement
unchanged. Escape closes the dialog when focus is outside an editing field;
an editing NumberField first consumes Escape to abandon its text draft.
The fields hold proposed coordinates only; `DockModel` continues to own live
placement. Owner model updates never emit the user acceptance signal. Applying
rechecks panel visibility, model/workspace identity and destination legality;
a disappeared target or disabled host cannot perform a stale operation.
Focus returns to the successor frame's Place button after a structural move.
Bare-chrome panels retain Shift+F10 from their focusable content; apps can also
call `DockHost.openPlacement(panelId)` from an existing menu.

Stable dialog parts are `dockPlacementDialog`, `dockPlacementDestination`,
`dockPlacementX`, `dockPlacementY`, `dialogPrimary`, and `dialogSecondary`.
Native assistive-technology delivery remains a WP-092 qualification; QML action
contract tests do not claim that qualification.
