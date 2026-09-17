// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import "DockDropLogic.js" as DropLogic

// AGENT-CONTRACT: the dockable-panel host. It draws what Tk.DockModel
// says and reports gestures back; it never holds arrangement state.
//
//     Tk.DockHost {
//         model: Tk.DockModel { storageKey: "myapp/image" }
//         workspace: "image"
//         canvas: Item { ... }                  // the fixed centre
//         Tk.DockPanel { panelId: "layers"; title: "Layers"; zone: "right"; ... }
//     }
//
// Geometry: top and bottom zones span the full width; left and right zones
// are columns of lanes beside a centre column; the "center" zone stacks
// rows above the canvas inside that column; "overlay" panels sit over the
// canvas at its top-right; floating panels sit above everything.
//
// AGENT-GUARD: structure (which panels sit in which lane/slot) is a
// snapshot assigned only when it changes, so Repeaters rebuild frames only
// for real moves. Extents, shares, active tabs, collapsed flags and floating
// rectangles are read live through `gen`-dependent helpers — a seam drag
// must never destroy the seam being dragged. Before a zone is rebuilt its
// frames reclaim their content into the declarations (see DockFrame).
Item {
    id: host

    property Tk.DockModel model: null
    property string workspace: "default"
    property Item canvas: null
    default property alias panels: registry.data
    readonly property Item canvasItem: canvasHost
    // {panelId, title, hidden} for every registered panel, by title.
    readonly property var panelMenuModel: host.computeMenu()
    readonly property var hiddenPanels: host.panelMenuModel.filter(function(e) { return e.hidden })
                                            .map(function(e) { return e.panelId })

    // Live generation counter mirrored from the model; helpers read it so
    // their bindings re-evaluate on every model change.
    property int gen: 0
    // Structural snapshots per zone: [{lane, slots: [{panelIds}]}].
    property var leftLanes: []
    property var rightLanes: []
    property var topLanes: []
    property var bottomLanes: []
    property var centerLanes: []
    property var overlayLanes: []
    property var floatingIds: []
    property var signatures: ({})
    property var shadedIds: ({})

    // Drag state (see beginDrag/updateDrag/endDrag).
    property bool dragging: false
    property string dragId: ""
    property bool dragFromFloating: false
    property bool dragCancelled: false
    property point dragPointer: Qt.point(0, 0)
    property var dropTarget: null
    property var edgeTargets: []

    property var frames: ({})
    property var floatingFrames: ({})
    property bool ready: false

    signal panelDropped(string panelId)

    Tk.DockModel { id: ownModel }
    Item { id: registry; visible: false; width: 0; height: 0 }

    // ---- registration ------------------------------------------------------

    function declarationOf(panelId) {
        for (const child of registry.children) {
            if (child.isDockPanel === true && child.panelId === panelId) {
                return child
            }
        }
        return null
    }

    function registerAll() {
        if (host.model === null) {
            return
        }
        let index = 0
        for (const child of registry.children) {
            if (child.isDockPanel !== true || child.panelId === "") {
                continue
            }
            host.model.registerPanel(host.workspace, child.panelId, child.definition(index))
            ++index
        }
    }

    function panelTitle(panelId) {
        host.gen
        if (host.model === null) {
            return panelId
        }
        const p = host.model.panel(panelId)
        return p.title !== undefined ? p.title : panelId
    }

    function panelInfo(panelId) {
        host.gen
        return host.model !== null ? host.model.panel(panelId) : ({})
    }

    function laneExtent(zone, lane) {
        host.gen
        if (host.model === null) {
            return 0
        }
        for (const l of host.model.lanes(zone)) {
            if (l.lane === lane) {
                return l.extent
            }
        }
        return 0
    }

    function slotInfo(zone, lane, slotIndex) {
        host.gen
        if (host.model !== null) {
            for (const l of host.model.lanes(zone)) {
                if (l.lane === lane && slotIndex < l.slots.length) {
                    return l.slots[slotIndex]
                }
            }
        }
        return { "panelIds": [], "activeId": "", "share": 1, "collapsed": false }
    }

    function computeMenu() {
        host.gen
        if (host.model === null) {
            return []
        }
        const list = []
        for (const id of host.model.panelIds()) {
            const p = host.model.panel(id)
            list.push({ "panelId": id, "title": p.title, "hidden": p.hidden === true })
        }
        list.sort(function(a, b) { return a.title < b.title ? -1 : a.title > b.title ? 1 : 0 })
        return list
    }

    function showPanel(panelId) { if (host.model !== null) host.model.show(panelId) }
    function hidePanel(panelId) { if (host.model !== null) host.model.hide(panelId) }
    function togglePanel(panelId) { if (host.model !== null) host.model.toggle(panelId) }
    function floatPanel(panelId) { if (host.model !== null) host.model.floatPanel(panelId) }
    function resetLayout() { if (host.model !== null) host.model.reset() }
    function applyPreset(presetId) { if (host.model !== null) host.model.applyPreset(presetId) }
    function isShaded(panelId) { host.gen; return host.shadedIds[panelId] === true }
    function setShaded(panelId, shaded) {
        const next = Object.assign({}, host.shadedIds)
        next[panelId] = shaded
        host.shadedIds = next
        host.gen = host.gen + 1
    }

    // ---- structure ---------------------------------------------------------

    function refresh() {
        if (host.model === null) {
            return
        }
        host.gen = host.gen + 1
        const zones = ["left", "right", "top", "bottom", "center", "overlay"]
        const props = ["leftLanes", "rightLanes", "topLanes", "bottomLanes", "centerLanes", "overlayLanes"]
        for (let i = 0; i < zones.length; ++i) {
            const structure = host.model.lanes(zones[i]).map(function(l) {
                return { "lane": l.lane, "slots": l.slots.map(function(s) { return { "panelIds": s.panelIds } }) }
            })
            const signature = JSON.stringify(structure)
            if (host.signatures[zones[i]] !== signature) {
                host.detachZone(zones[i])
                host.signatures[zones[i]] = signature
                host[props[i]] = structure
            }
        }
        const ids = host.model.floatingPanels().map(function(p) { return p.panelId }).sort()
        const floatingSignature = JSON.stringify(ids)
        if (host.signatures["floating"] !== floatingSignature) {
            host.detachFloating()
            host.signatures["floating"] = floatingSignature
            host.floatingIds = ids
        }
    }

    function detachZone(zone) {
        const seen = []
        for (const id in host.frames) {
            const frame = host.frames[id]
            if (frame && frame.zone === zone && seen.indexOf(frame) < 0) {
                seen.push(frame)
                frame.detach()
            }
        }
    }

    function detachFloating() {
        for (const id in host.floatingFrames) {
            const frame = host.floatingFrames[id]
            if (frame) {
                frame.detach()
            }
        }
    }

    function registerFrame(frame) {
        for (const id of frame.panelIds) {
            host.frames[id] = frame
        }
    }
    function unregisterFrame(frame) {
        for (const id in host.frames) {
            if (host.frames[id] === frame) {
                delete host.frames[id]
            }
        }
    }
    function registerFloatingFrame(frame) { host.floatingFrames[frame.panelId] = frame }
    function unregisterFloatingFrame(frame) {
        if (host.floatingFrames[frame.panelId] === frame) {
            delete host.floatingFrames[frame.panelId]
        }
    }

    // ---- seams -------------------------------------------------------------

    function resizeLane(zone, lane, delta) {
        if (host.model === null) {
            return
        }
        host.model.setLaneExtent(zone, lane, host.laneExtent(zone, lane) + delta)
    }

    // A share seam moves size between the slot above/left (index) and the
    // one below/right (index + 1); the pair keeps its total.
    function adjustShares(zone, lane, index, delta, laneLength) {
        if (host.model === null) {
            return
        }
        let slots = []
        for (const l of host.model.lanes(zone)) {
            if (l.lane === lane) {
                slots = l.slots
            }
        }
        if (index + 1 >= slots.length) {
            return
        }
        let total = 0
        let open = 0
        for (const s of slots) {
            if (!s.collapsed) {
                total += s.share
                ++open
            }
        }
        const usable = Math.max(laneLength - (slots.length - open) * Tk.Theme.size.header
                                - (slots.length - 1) * Tk.Theme.size.seam, 1)
        const change = delta / usable * total
        const a = slots[index]
        const b = slots[index + 1]
        const next = Math.max(0.05, a.share + change)
        const pair = a.share + b.share
        host.model.setShare(a.activeId, Math.min(next, pair - 0.05))
        host.model.setShare(b.activeId, Math.max(0.05, pair - Math.min(next, pair - 0.05)))
    }

    // ---- drag to dock ------------------------------------------------------

    function beginDrag(panelId, scenePos, fromFloating) {
        host.dragId = panelId
        host.dragFromFloating = fromFloating === true
        host.dragCancelled = false
        host.dragging = true
        host.edgeTargets = DropLogic.edgeTargets(host, panelId, Tk.Theme.size.dropEdge)
        host.updateDrag(scenePos)
    }

    function updateDrag(scenePos) {
        if (!host.dragging) {
            return
        }
        const p = host.mapFromItem(null, scenePos.x, scenePos.y)
        host.dragPointer = Qt.point(p.x, p.y)
        host.dropTarget = DropLogic.hitTest(host, p)
    }

    function endDrag() {
        if (!host.dragging) {
            return
        }
        const target = host.dropTarget
        const id = host.dragId
        const pointer = host.dragPointer
        const fromFloating = host.dragFromFloating
        host.dragging = false
        host.dropTarget = null
        host.edgeTargets = []
        if (host.dragCancelled || host.model === null) {
            return
        }
        if (target !== null && target.kind === "zone") {
            host.model.dock(id, target.zone)
        } else if (target !== null && target.kind === "beside") {
            host.model.dockBeside(id, target.target, target.placement)
        } else if (!fromFloating) {
            host.model.floatPanel(id, Math.max(0, pointer.x - Tk.Theme.space.xl),
                                  Math.max(0, pointer.y - Tk.Theme.size.header / 2))
        }
        host.panelDropped(id)
    }

    function cancelDrag() {
        host.dragCancelled = true
        host.dragging = false
        host.dropTarget = null
        host.edgeTargets = []
    }

    // ---- lifecycle ---------------------------------------------------------

    function seatCanvas() {
        if (host.canvas === null) {
            return
        }
        host.canvas.parent = canvasHost
        host.canvas.anchors.fill = canvasHost
        host.canvas.z = 0
    }

    onCanvasChanged: seatCanvas()
    onWorkspaceChanged: {
        if (host.ready && host.model !== null) {
            host.model.workspace = host.workspace
            host.registerAll()
            host.refresh()
        }
    }
    onModelChanged: {
        if (host.ready && host.model !== null) {
            host.signatures = ({})
            host.model.workspace = host.workspace
            host.registerAll()
            host.refresh()
        }
    }

    Component.onCompleted: {
        if (host.model === null) {
            host.model = ownModel
        }
        host.model.workspace = host.workspace
        host.registerAll()
        if (host.model.storageKey !== "") {
            host.model.restore()
        }
        host.seatCanvas()
        host.ready = true
        host.refresh()
    }

    Connections {
        target: host.model
        function onLayoutChanged() { host.refresh() }
        function onWorkspaceChanged() { host.refresh() }
    }

    Shortcut {
        sequences: ["Escape"]
        enabled: host.dragging
        onActivated: host.cancelDrag()
    }

    // ---- geometry ------------------------------------------------------------

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.DockZone { host: host; zone: "top"; lanes: host.topLanes }

        Tk.Flex {
            id: middle
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0

            Tk.DockZone { host: host; zone: "left"; lanes: host.leftLanes }

            Tk.Flex {
                id: centerColumn
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minWidth: 0

                Tk.DockZone { host: host; zone: "center"; lanes: host.centerLanes }

                Item {
                    id: canvasHost
                    objectName: "dockCanvas"
                    clip: true
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    Tk.Flex.minHeight: 0

                    // Overlay zone: stacked at the canvas's top-right.
                    Tk.Flex {
                        id: overlayZone
                        objectName: "dockZone_overlay"
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: Tk.Theme.space.sm
                        width: host.laneExtent("overlay", 0)
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        visible: host.overlayLanes.length > 0
                        z: 5

                        Repeater {
                            model: host.overlayLanes.length > 0 ? host.overlayLanes[0].slots : []
                            delegate: Tk.DockFrame {
                                required property var modelData
                                required property int index
                                host: host
                                zone: "overlay"
                                lane: host.overlayLanes.length > 0 ? host.overlayLanes[0].lane : 0
                                slotIndex: index
                                panelIds: modelData.panelIds
                                Tk.Flex.grow: 0
                                Tk.Flex.basis: collapsed ? Tk.Theme.size.header
                                             : (info.height !== undefined ? info.height : 240)
                            }
                        }
                    }
                }
            }

            Tk.DockZone { host: host; zone: "right"; lanes: host.rightLanes }
        }

        Tk.DockZone { host: host; zone: "bottom"; lanes: host.bottomLanes }
    }

    Tk.DockFloating {
        id: floatingLayer
        anchors.fill: parent
        host: host
        ids: host.floatingIds
        z: 100
    }

    Tk.DockDropOverlay {
        anchors.fill: parent
        host: host
        z: 200
    }
}
