// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import "NodeGraphGeometry.js" as GraphGeometry
pragma ComponentBehavior: Bound

// Hosts validate and republish snapshots after each graph edit request.
Item {
    id: root
    objectName: "nodeGraph"
    clip: true
    focus: true
    activeFocusOnTab: enabled

    property var nodes: [] // {nodeId,title,x,y,inputs:[{portId,label,type}],outputs:[...]}
    property var edges: [] // {edgeId,fromNodeId,fromPortId,toNodeId,toPortId}
    property string selectedNodeId: ""
    property string selectedEdgeId: ""
    property string wireRouting: "curved" // curved | orthogonal
    property real zoom: 1.0
    property real minZoom: 0.45
    property real maxZoom: 1.8
    property real panX: Tk.Theme.space.xl
    property real panY: Tk.Theme.space.xl
    property string tooltip: qsTr("Node graph")
    property string linkStatusText: ""
    property string linkStatusState: ""
    readonly property real nodeWidth: Tk.Theme.size.panelMinWidth
    readonly property int nodeHeight: Tk.Theme.size.header
        + Math.max(1, GraphGeometry.maxPortCount(nodes)) * Tk.Theme.size.row + Tk.Theme.space.sm

    signal nodeSelected(string nodeId)
    signal edgeSelected(string edgeId)
    signal nodeMoveRequested(string nodeId, real x, real y)
    signal linkConnectRequested(string requestId, string fromNodeId,
                                string fromPortId, string toNodeId, string toPortId)
    signal linkDisconnectRequested(string requestId, string edgeId)

    property string pendingNode: ""
    property string pendingPort: ""
    property int requestSerial: 0
    property string latestLinkRequestId: ""
    property string keyboardConnectSourceNodeId: ""
    property string keyboardConnectSourcePortId: ""
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 nodes, %2 connections, %3 percent zoom")
        .arg(nodes ? nodes.length : 0).arg(edges ? edges.length : 0)
        .arg(Math.round(zoom * 100))
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onIncreaseAction: root.setZoom(root.zoom * 1.15)
    Accessible.onDecreaseAction: root.setZoom(root.zoom / 1.15)

    function nodeById(nodeId) {
        return GraphGeometry.nodeById(nodes, nodeId)
    }

    function portById(nodeId, portId, side) {
        return GraphGeometry.portById(nodes, nodeId, portId, side)
    }

    function portColor(type) { return Tk.Theme.color[GraphGeometry.portRole(type)] }

    function visibleNode(record) {
        return GraphGeometry.visibleNode(record, nodeWidth, nodeHeight, zoom,
                                         panX, panY, width, height)
    }

    function beginOrFinishLink(nodeId, portId, side) {
        if (side === "output") {
            pendingNode = String(nodeId)
            pendingPort = String(portId)
            linkStatusState = "pending"
            linkStatusText = qsTr("Choose an input port to request a connection")
            return
        }
        if (!pendingNode || !pendingPort) {
            linkStatusState = "info"
            linkStatusText = qsTr("Choose an output port first")
            return
        }
        requestSerial += 1
        latestLinkRequestId = "node-link-" + requestSerial
        linkConnectRequested(latestLinkRequestId, pendingNode, pendingPort,
                             String(nodeId), String(portId))
        pendingNode = ""
        pendingPort = ""
    }

    function requestDisconnect(edgeId) {
        if (!edgeId) return
        requestSerial += 1
        latestLinkRequestId = "node-link-" + requestSerial
        linkDisconnectRequested(latestLinkRequestId, String(edgeId))
    }

    function acknowledgeLink(requestId, state, message) {
        if (String(requestId) !== latestLinkRequestId) return false
        linkStatusState = String(state)
        linkStatusText = String(message || (state === "accepted"
            ? qsTr("Connection accepted") : qsTr("Connection rejected")))
        return true
    }

    function setZoom(value) {
        zoom = Math.max(minZoom, Math.min(maxZoom, Number(value)))
    }

    function zoomAt(value, point) {
        const next = Math.max(minZoom, Math.min(maxZoom, Number(value)))
        const worldX = (point.x - panX) / zoom
        const worldY = (point.y - panY) / zoom
        panX = point.x - worldX * next
        panY = point.y - worldY * next
        zoom = next
    }

    function edgePoint(nodeId, portId, side) {
        return GraphGeometry.edgePoint(nodes, nodeWidth, Tk.Theme.size.header,
            Tk.Theme.size.row, zoom, panX, panY, nodeId, portId, side)
    }

    Keys.onPressed: function(event) {
        if (event.modifiers !== Qt.NoModifier) return
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            setZoom(zoom * 1.15)
            event.accepted = true
        } else if (event.key === Qt.Key_Minus) {
            setZoom(zoom / 1.15)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            pendingNode = ""
            pendingPort = ""
            keyboardConnectSourceNodeId = ""
            keyboardConnectSourcePortId = ""
            linkStatusText = qsTr("Connection request canceled")
            event.accepted = true
        } else if (event.key === Qt.Key_Space && selectedNodeId) {
            const node = nodeById(selectedNodeId)
            if (node && (node.outputs || []).length) {
                keyboardConnectSourceNodeId = selectedNodeId
                keyboardConnectSourcePortId = String(node.outputs[0].portId)
                pendingNode = keyboardConnectSourceNodeId
                pendingPort = keyboardConnectSourcePortId
                linkStatusText = qsTr("Connection armed from %1. Select a target node and press Enter.")
                    .arg(node.title || node.nodeId)
                event.accepted = true
            }
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && keyboardConnectSourceNodeId && selectedNodeId) {
            const target = nodeById(selectedNodeId)
            if (target && (target.inputs || []).length)
                beginOrFinishLink(selectedNodeId, target.inputs[0].portId, "input")
            keyboardConnectSourceNodeId = ""
            keyboardConnectSourcePortId = ""
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            if (selectedEdgeId) requestDisconnect(selectedEdgeId)
            event.accepted = Boolean(selectedEdgeId)
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                   || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            const index = GraphGeometry.nodeIndex(nodes, selectedNodeId)
            const delta = event.key === Qt.Key_Left || event.key === Qt.Key_Up ? -1 : 1
            const next = Math.max(0, Math.min((nodes || []).length - 1,
                                               index < 0 ? 0 : index + delta))
            if (nodes && nodes.length) nodeSelected(String(nodes[next].nodeId))
            event.accepted = Boolean(nodes && nodes.length)
        }
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    HoverHandler { id: graphHover }
    Tk.ToolTip { text: root.tooltip; visible: graphHover.hovered }

    Canvas {
        id: wireLayer
        objectName: "nodeGraphWires"
        anchors.fill: parent
        renderStrategy: Canvas.Threaded
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const source = root.edges || []
            for (let i = 0; i < source.length; ++i) {
                const edge = source[i]
                const a = root.edgePoint(edge.fromNodeId, edge.fromPortId, "output")
                const b = root.edgePoint(edge.toNodeId, edge.toPortId, "input")
                const port = root.portById(edge.fromNodeId, edge.fromPortId, "output")
                ctx.beginPath()
                GraphGeometry.paintEdge(ctx, a, b, root.wireRouting,
                                        Tk.Theme.size.icon, Tk.Theme.space.xl)
                ctx.strokeStyle = String(edge.edgeId) === root.selectedEdgeId
                    ? Tk.Theme.color.focus : root.portColor(port ? port.type : "")
                ctx.lineWidth = String(edge.edgeId) === root.selectedEdgeId
                    ? Tk.Theme.size.border * 2 : Tk.Theme.size.border
                ctx.stroke()
            }
        }
        Connections {
            target: root
            function onNodesChanged() { wireLayer.requestPaint() }
            function onEdgesChanged() { wireLayer.requestPaint() }
            function onPanXChanged() { wireLayer.requestPaint() }
            function onPanYChanged() { wireLayer.requestPaint() }
            function onZoomChanged() { wireLayer.requestPaint() }
            function onWireRoutingChanged() { wireLayer.requestPaint() }
            function onSelectedEdgeIdChanged() { wireLayer.requestPaint() }
        }
    }

    Repeater {
        model: root.edges || []
        delegate: T.Button {
            id: edgeTarget
            required property var modelData
            property string tooltip: qsTr("Select connection %1").arg(modelData.edgeId)
            readonly property var from: root.edgePoint(modelData.fromNodeId,
                                                       modelData.fromPortId, "output")
            readonly property var to: root.edgePoint(modelData.toNodeId,
                                                     modelData.toPortId, "input")
            x: (from.x + to.x) / 2 - width / 2
            y: (from.y + to.y) / 2 - height / 2
            width: Tk.Theme.size.icon
            height: Tk.Theme.size.icon
            text: qsTr("×")
            Accessible.name: tooltip
            Accessible.role: Accessible.Button
            background: Rectangle {
                radius: Tk.Theme.radius.full
                color: edgeTarget.down ? Tk.Theme.color.controlActiveBg : Tk.Theme.color.panelAlt
                border.color: String(edgeTarget.modelData.edgeId) === root.selectedEdgeId
                    ? Tk.Theme.color.focus : Tk.Theme.color.border
                border.width: Tk.Theme.size.border
            }
            contentItem: Tk.Label {
                text: edgeTarget.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Tk.Theme.color.text
                font.pixelSize: Tk.Theme.font.caption
            }
            onClicked: root.edgeSelected(String(modelData.edgeId))
            Tk.ToolTip { text: edgeTarget.tooltip; visible: edgeTarget.hovered }
            z: 2
        }
    }

    Repeater {
        model: root.nodes || []
        delegate: Loader {
            id: nodeLoader
            required property var modelData
            active: root.visibleNode(modelData)
            x: root.panX + Number(modelData.x || 0) * root.zoom
                + (item && item.dragging ? item.dragDelta.x : 0)
            y: root.panY + Number(modelData.y || 0) * root.zoom
                + (item && item.dragging ? item.dragDelta.y : 0)
            width: root.nodeWidth
            height: root.nodeHeight
            scale: root.zoom
            transformOrigin: Item.TopLeft
            sourceComponent: nodeCard
            property var record: modelData
            onLoaded: if (item) item.record = record
        }
    }

    Component {
        id: nodeCard
        Rectangle {
            id: card
            property var record: ({ nodeId: "", title: "", x: 0, y: 0,
                                    inputs: [], outputs: [] })
            property bool dragging: false
            property point dragDelta: Qt.point(0, 0)
            readonly property string nodeId: String(record.nodeId)
            width: root.nodeWidth
            height: root.nodeHeight
            radius: Tk.Theme.radius.md
            color: Tk.Theme.color.panel
            border.width: Tk.Theme.size.border
            border.color: nodeId === root.selectedNodeId
                ? Tk.Theme.color.accent : Tk.Theme.color.border
            Accessible.role: Accessible.ListItem
            Accessible.name: String(record.title || record.nodeId)
            Accessible.selected: nodeId === root.selectedNodeId
            objectName: "node_" + nodeId

            Rectangle {
                objectName: "nodeHeader_" + card.nodeId
                width: parent.width
                height: Tk.Theme.size.header
                radius: Tk.Theme.radius.md
                color: card.nodeId === root.selectedNodeId
                    ? Tk.Theme.color.controlActiveBg : Tk.Theme.color.panelAlt
                border.color: Tk.Theme.color.border
                border.width: Tk.Theme.size.border
                Tk.Label {
                    anchors.fill: parent
                    anchors.leftMargin: Tk.Theme.space.sm
                    anchors.rightMargin: Tk.Theme.space.sm
                    text: String(card.record.title || card.record.nodeId)
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Tk.Theme.font.body
                    color: Tk.Theme.color.text
                }
            }

            Repeater {
                model: card.record.inputs || []
                delegate: T.Button {
                    id: inputPort
                    required property var modelData
                    required property int index
                    property string tooltip: qsTr("Input %1, type %2. Activate after selecting an output.")
                        .arg(text).arg(modelData.type || qsTr("unspecified"))
                    x: 0
                    y: Tk.Theme.size.header + index * Tk.Theme.size.row
                    width: card.width / 2
                    height: Tk.Theme.size.row
                    text: String(modelData.label || modelData.portId)
                    Accessible.name: tooltip
                    Accessible.role: Accessible.Button
                    background: Rectangle {
                        color: inputPort.down ? Tk.Theme.color.hover : "transparent"
                    }
                    contentItem: Item {
                        Rectangle {
                            x: Tk.Theme.space.xs
                            y: (parent.height - height) / 2
                            width: Tk.Theme.size.iconSm
                            height: Tk.Theme.size.iconSm
                            radius: Tk.Theme.radius.full
                            color: root.portColor(inputPort.modelData.type)
                        }
                        Tk.Label {
                            anchors.fill: parent
                            anchors.leftMargin: Tk.Theme.size.iconSm + Tk.Theme.space.sm
                            anchors.rightMargin: Tk.Theme.space.xs
                            text: inputPort.text
                            color: Tk.Theme.color.text
                            font.pixelSize: Tk.Theme.font.caption
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                    onClicked: root.beginOrFinishLink(card.nodeId,
                        String(modelData.portId), "input")
                    Tk.ToolTip { text: inputPort.tooltip; visible: inputPort.hovered }
                    objectName: "nodePort_" + card.nodeId + "_" + modelData.portId
                }
            }

            Repeater {
                model: card.record.outputs || []
                delegate: T.Button {
                    id: outputPort
                    required property var modelData
                    required property int index
                    property string tooltip: qsTr("Output %1, type %2. Activate to begin a connection.")
                        .arg(text).arg(modelData.type || qsTr("unspecified"))
                    x: card.width / 2
                    y: Tk.Theme.size.header + index * Tk.Theme.size.row
                    width: card.width / 2
                    height: Tk.Theme.size.row
                    text: String(modelData.label || modelData.portId)
                    Accessible.name: tooltip
                    Accessible.role: Accessible.Button
                    background: Rectangle {
                        color: outputPort.down ? Tk.Theme.color.hover : "transparent"
                    }
                    contentItem: Item {
                        Tk.Label {
                            anchors.fill: parent
                            anchors.leftMargin: Tk.Theme.space.xs
                            anchors.rightMargin: Tk.Theme.size.iconSm + Tk.Theme.space.sm
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            text: outputPort.text
                            color: Tk.Theme.color.text
                            font.pixelSize: Tk.Theme.font.caption
                            elide: Text.ElideLeft
                        }
                        Rectangle {
                            x: parent.width - width - Tk.Theme.space.xs
                            y: (parent.height - height) / 2
                            width: Tk.Theme.size.iconSm
                            height: Tk.Theme.size.iconSm
                            radius: Tk.Theme.radius.full
                            color: root.portColor(outputPort.modelData.type)
                        }
                    }
                    onClicked: root.beginOrFinishLink(card.nodeId,
                        String(modelData.portId), "output")
                    Tk.ToolTip { text: outputPort.tooltip; visible: outputPort.hovered }
                    objectName: "nodePort_" + card.nodeId + "_" + modelData.portId
                }
            }

            DragHandler {
                id: nodeDrag
                target: null
                acceptedButtons: Qt.LeftButton
                onActiveChanged: {
                    card.dragging = active
                    if (!active && (Math.abs(card.dragDelta.x) > 0
                                    || Math.abs(card.dragDelta.y) > 0)) {
                        root.nodeMoveRequested(card.nodeId,
                            Number(card.record.x || 0) + card.dragDelta.x / root.zoom,
                            Number(card.record.y || 0) + card.dragDelta.y / root.zoom)
                        card.dragDelta = Qt.point(0, 0)
                    }
                }
                onTranslationChanged: {
                    card.dragDelta = Qt.point(translation.x, translation.y)
                }
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: root.nodeSelected(card.nodeId)
            }
        }
    }

    DragHandler {
        id: panGesture
        target: null
        acceptedButtons: Qt.MiddleButton
        onActiveChanged: {
            if (active) {
                startPanX = root.panX
                startPanY = root.panY
            }
        }
        onTranslationChanged: {
            if (active) {
                root.panX = startPanX + translation.x
                root.panY = startPanY + translation.y
            }
        }
        property real startPanX: 0
        property real startPanY: 0
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            if (event.modifiers & Qt.ControlModifier) {
                const factor = event.angleDelta.y > 0 ? 1.15 : 1 / 1.15
                root.zoomAt(root.zoom * factor, event.point.position)
            } else {
                root.panX -= event.angleDelta.x
                root.panY -= event.angleDelta.y
            }
            event.accepted = true
        }
    }

    Tk.Flex {
        objectName: "nodeGraphToolbar"
        anchors.right: parent.right
        anchors.top: parent.top
        padding: Tk.Theme.space.xs
        gap: Tk.Theme.space.xs
        direction: Tk.Flex.Row
        Rectangle {
            radius: Tk.Theme.radius.md
            color: Tk.Theme.color.islandBg
            border.color: Tk.Theme.color.islandBorder
            border.width: Tk.Theme.size.border
            implicitWidth: zoomReadout.implicitWidth + Tk.Theme.space.md
            implicitHeight: Tk.Theme.size.control
            Tk.Label {
                id: zoomReadout
                anchors.centerIn: parent
                text: qsTr("%1%").arg(Math.round(root.zoom * 100))
                color: Tk.Theme.color.textMuted
                font.pixelSize: Tk.Theme.font.caption
            }
        }
        Tk.Button {
            text: qsTr("Disconnect")
            small: true
            enabled: Boolean(root.selectedEdgeId)
            tooltip: qsTr("Request disconnection of the selected link")
            onClicked: root.requestDisconnect(root.selectedEdgeId)
        }
    }

    Rectangle {
        objectName: "nodeGraphStatus"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.linkStatusText ? Tk.Theme.size.controlLg : 0
        visible: height > 0
        color: root.linkStatusState === "rejected" ? Tk.Theme.color.dangerSubtle
            : root.linkStatusState === "accepted" ? Tk.Theme.color.accentSubtle
            : Tk.Theme.color.panelAlt
        border.color: root.linkStatusState === "rejected" ? Tk.Theme.color.danger
            : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        Tk.Label {
            anchors.fill: parent
            anchors.leftMargin: Tk.Theme.space.md
            anchors.rightMargin: Tk.Theme.space.md
            text: root.linkStatusText
            color: root.linkStatusState === "rejected"
                ? Tk.Theme.color.danger : Tk.Theme.color.text
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Tk.Theme.font.caption
            elide: Text.ElideRight
            objectName: "nodeGraphStatusText"
        }
    }

    Rectangle {
        objectName: "nodeGraphFocusRing"
        anchors.fill: parent
        color: "transparent"
        border.color: Tk.Theme.color.focus
        border.width: Tk.Theme.size.focusRing
        visible: root.activeFocus
        z: 4
    }

}
