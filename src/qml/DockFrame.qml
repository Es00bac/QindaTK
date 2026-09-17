// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: one docked slot: Tk.Panel chrome around the content of
// one panel, or a tab strip plus the contents of a whole group. The frame
// is created from the host's structural snapshot (which panels sit here)
// and reads the live facts (active tab, collapsed, share) from the model
// through `host.slotInfo`, so a seam drag or a tab click never rebuilds
// it. Content items are seated (reparented) into per-panel containers and
// reclaimed on teardown; they are never re-created.
FocusScope {
    id: frame

    required property var host
    property string zone: ""
    property int lane: 0
    property int slotIndex: 0
    property var panelIds: []
    readonly property bool sideZone: zone === "left" || zone === "right"

    // Live facts.
    readonly property var slot: host.slotInfo(zone, lane, slotIndex)
    readonly property string activeId: slot.activeId !== undefined && slot.activeId !== "" ? slot.activeId
                                       : (panelIds.length > 0 ? panelIds[0] : "")
    readonly property bool collapsed: slot.collapsed === true
    readonly property real share: slot.share !== undefined ? slot.share : 1
    readonly property var info: host.panelInfo(activeId)
    readonly property var declaration: host.declarationOf(activeId)
    readonly property string chrome: info.chrome !== undefined ? info.chrome : "default"

    objectName: "dockFrame_" + activeId

    Tk.Flex.grow: collapsed ? 0 : share
    Tk.Flex.shrink: collapsed ? 0 : 1
    Tk.Flex.basis: collapsed ? panel.headerItem.implicitHeight : 0
    Tk.Flex.minWidth: 0
    Tk.Flex.minHeight: 0

    function seat(panelId, container) {
        const decl = host.declarationOf(panelId)
        if (decl === null) {
            return
        }
        const content = decl.contentItem
        content.parent = container
        content.anchors.fill = container
        content.visible = true
    }

    // AGENT-GUARD: Repeaters destroy replaced frames with deleteLater, so
    // this runs after the successor frame has already seated the content.
    // Only content still inside this frame is reclaimed; reclaiming blindly
    // would pull it out of the new frame.
    function owns(item) {
        let p = item
        while (p !== null && p !== undefined) {
            if (p === frame) {
                return true
            }
            p = p.parent
        }
        return false
    }

    function detach() {
        for (const panelId of panelIds) {
            const decl = host.declarationOf(panelId)
            if (decl !== null && owns(decl.contentItem)) {
                decl.reclaim()
            }
        }
    }

    Component.onCompleted: host.registerFrame(frame)
    Component.onDestruction: {
        detach()
        host.unregisterFrame(frame)
    }

    Tk.Panel {
        id: panel
        anchors.fill: parent
        title: host.panelTitle(frame.activeId)
        iconName: frame.declaration !== null ? frame.declaration.iconName : ""
        grip: frame.chrome === "default"
        headerVisible: frame.chrome !== "bare"
        collapsible: frame.declaration !== null ? frame.declaration.collapsible : true
        closable: frame.info.closable !== undefined ? frame.info.closable : true
        floatable: true
        floating: false
        active: frame.activeFocus
        padding: frame.declaration !== null ? frame.declaration.padding : Tk.Theme.space.sm

        onCollapseToggled: function(collapsed) { host.model.collapse(frame.activeId, collapsed) }
        onFloatRequested: host.model.floatPanel(frame.activeId)
        onCloseRequested: host.model.hide(frame.activeId)

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0

            Tk.DockTabStrip {
                objectName: "dockTabStrip_" + frame.activeId
                visible: frame.panelIds.length > 1
                host: frame.host
                panelIds: frame.panelIds
                activeId: frame.activeId
                zone: frame.zone
                Tk.Flex.shrink: 0
            }

            Item {
                id: contentArea
                clip: true
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minHeight: 0

                Repeater {
                    model: frame.panelIds
                    delegate: Item {
                        id: container
                        required property string modelData
                        objectName: "dockContent_" + modelData
                        anchors.fill: parent
                        visible: modelData === frame.activeId
                        Component.onCompleted: frame.seat(modelData, container)
                    }
                }
            }
        }
    }

    // AGENT-GUARD: the model owns `collapsed`; Tk.Panel toggles its own
    // property before signalling, which would break a plain binding, so a
    // Binding element re-asserts the model's value after every toggle.
    Binding {
        target: panel
        property: "collapsed"
        value: frame.collapsed
    }

    // Dragging the header beyond 8px tears the active panel out; the host
    // shows drop targets and applies the drop on release.
    DragHandler {
        parent: panel.headerItem
        target: null
        dragThreshold: 8
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active) {
                host.beginDrag(frame.activeId, centroid.scenePosition, false)
            } else {
                host.endDrag()
            }
        }
        onTranslationChanged: {
            if (active) {
                host.updateDrag(centroid.scenePosition)
            }
        }
    }
}
