// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The floating layer: every floating panel of the model, drawn above the
// dock zones in z-order. A frame's rectangle is read live from the model,
// so header drags (moveFloating), the eight resize handles
// (setFloatingRect) and raising (bringToFront) never rebuild it. `shade`
// (collapse to the header) is host-side state that survives rebuilds.
// AGENT-GUARD: never name this id `layer`: Item.layer is a property of
// every delegate, and the scope object's property shadows the id.
Item {
    id: floatingLayer

    required property var host
    property var ids: []

    Repeater {
        model: floatingLayer.ids
        delegate: FloatingFrame {
            required property string modelData
            host: floatingLayer.host
            bounds: floatingLayer
            panelId: modelData
        }
    }

    // AGENT-NOTE: an inline component cannot see the enclosing file's ids,
    // so the host and the bounding layer are passed in explicitly.
    component FloatingFrame: FocusScope {
        id: frame

        required property var host
        required property Item bounds
        property string panelId: ""
        readonly property var info: host.panelInfo(panelId)
        readonly property var declaration: host.declarationOf(panelId)
        readonly property bool shaded: host.isShaded(panelId)
        readonly property real headerHeight: panel.headerItem.implicitHeight + 2
        readonly property real minW: info.minWidth !== undefined ? info.minWidth : 160
        readonly property real minH: info.minHeight !== undefined ? info.minHeight : 100

        objectName: "dockFloating_" + panelId
        x: info.x !== undefined ? info.x : 0
        y: info.y !== undefined ? info.y : 0
        width: info.width !== undefined ? info.width : 320
        height: shaded ? headerHeight : (info.height !== undefined ? info.height : 400)
        z: info.zOrder !== undefined ? info.zOrder : 0

        // Reclaims the content only while it still sits in this frame (a
        // successor frame may already have seated it; see DockFrame).
        function detach() {
            if (declaration === null) {
                return
            }
            let p = declaration.contentItem.parent
            while (p !== null && p !== undefined) {
                if (p === frame) {
                    declaration.reclaim()
                    return
                }
                p = p.parent
            }
        }

        Component.onCompleted: frame.host.registerFloatingFrame(frame)
        Component.onDestruction: {
            detach()
            frame.host.unregisterFloatingFrame(frame)
        }

        Keys.onEscapePressed: frame.host.model.hide(panelId)

        Tk.Panel {
            id: panel
            anchors.fill: parent
            title: frame.host.panelTitle(frame.panelId)
            iconName: frame.declaration !== null ? frame.declaration.iconName : ""
            grip: true
            floating: true
            floatable: true
            closable: frame.info.closable !== undefined ? frame.info.closable : true
            collapsible: true
            active: frame.activeFocus
            padding: frame.declaration !== null ? frame.declaration.padding : Tk.Theme.space.sm

            onCollapseToggled: function(collapsed) { frame.host.setShaded(frame.panelId, collapsed) }
            onFloatRequested: frame.host.model.dock(frame.panelId, frame.info.zone !== undefined ? frame.info.zone : "right")
            onCloseRequested: frame.host.model.hide(frame.panelId)

            Item {
                id: container
                objectName: "dockContent_" + frame.panelId
                clip: true
                Component.onCompleted: {
                    if (frame.declaration !== null) {
                        const content = frame.declaration.contentItem
                        content.parent = container
                        content.anchors.fill = container
                        content.visible = true
                    }
                }
            }
        }

        Binding {
            target: panel
            property: "collapsed"
            value: frame.shaded
        }

        // Any press raises the panel; the press itself continues to the
        // content underneath because it is not accepted here.
        MouseArea {
            anchors.fill: parent
            z: 10
            acceptedButtons: Qt.AllButtons
            onPressed: function(mouse) {
                frame.host.model.bringToFront(frame.panelId)
                frame.forceActiveFocus()
                mouse.accepted = false
            }
        }

        // Header drag moves the panel and offers dock targets.
        DragHandler {
            id: moveDrag
            parent: panel.headerItem
            target: null
            dragThreshold: 4
            acceptedButtons: Qt.LeftButton
            property real startX: 0
            property real startY: 0
            onActiveChanged: {
                if (active) {
                    startX = frame.x
                    startY = frame.y
                    frame.host.beginDrag(frame.panelId, centroid.scenePosition, true)
                } else {
                    frame.host.endDrag()
                }
            }
            onTranslationChanged: {
                if (!active) {
                    return
                }
                const maxX = Math.max(0, frame.bounds.width - 80)
                const maxY = Math.max(0, frame.bounds.height - frame.headerHeight)
                frame.host.model.moveFloating(frame.panelId,
                                              Math.max(0, Math.min(startX + translation.x, maxX)),
                                              Math.max(0, Math.min(startY + translation.y, maxY)))
                frame.host.updateDrag(centroid.scenePosition)
            }
        }

        // Eight resize handles (edges and corners), hidden while shaded or
        // when the panel declared fixedSize.
        Repeater {
            model: frame.shaded || frame.info.fixedSize === true ? [] : [
                { "ex": -1, "ey": 0 }, { "ex": 1, "ey": 0 }, { "ex": 0, "ey": -1 }, { "ex": 0, "ey": 1 },
                { "ex": -1, "ey": -1 }, { "ex": 1, "ey": -1 }, { "ex": -1, "ey": 1 }, { "ex": 1, "ey": 1 }]
            delegate: Item {
                id: handle
                required property var modelData
                readonly property int ex: modelData.ex
                readonly property int ey: modelData.ey
                readonly property real grip: Tk.Theme.size.seam + 2
                readonly property real corner: Tk.Theme.size.handle
                x: ex < 0 ? -grip / 2 : ex > 0 ? frame.width - grip / 2 : corner
                y: ey < 0 ? -grip / 2 : ey > 0 ? frame.height - grip / 2 : corner
                width: ex === 0 ? frame.width - corner * 2 : (ey === 0 ? grip : corner)
                height: ey === 0 ? frame.height - corner * 2 : (ex === 0 ? grip : corner)
                z: 20

                HoverHandler {
                    cursorShape: handle.ex === 0 ? Qt.SizeVerCursor
                               : handle.ey === 0 ? Qt.SizeHorCursor
                               : (handle.ex === handle.ey ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                }
                DragHandler {
                    target: null
                    dragThreshold: 1
                    property rect start: Qt.rect(0, 0, 0, 0)
                    onActiveChanged: {
                        if (active) {
                            start = Qt.rect(frame.x, frame.y, frame.width, frame.info.height)
                        }
                    }
                    onTranslationChanged: {
                        if (!active) {
                            return
                        }
                        let w = start.width + (handle.ex > 0 ? translation.x : handle.ex < 0 ? -translation.x : 0)
                        let h = start.height + (handle.ey > 0 ? translation.y : handle.ey < 0 ? -translation.y : 0)
                        w = Math.max(w, frame.minW)
                        h = Math.max(h, frame.minH)
                        const x = handle.ex < 0 ? start.x + start.width - w : start.x
                        const y = handle.ey < 0 ? start.y + start.height - h : start.y
                        frame.host.model.setFloatingRect(frame.panelId, x, y, w, h)
                    }
                }
            }
        }
    }
}
