// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import "NodeGraphGeometry.js" as GraphGeometry
import "NodeGraphKeys.js" as GraphKeys
pragma ComponentBehavior: Bound

// Hosts validate and republish snapshots after each graph edit request.
// AGENT-CONTRACT (docs/node-graph.md): NodeGraphModel is the reference host;
// selection, groups and topology stay host-owned, search is view-local.
Item {
    id: root
    objectName: "nodeGraph"
    clip: true
    focus: true
    activeFocusOnTab: enabled

    property var nodes: [] // {nodeId,title,x,y,inputs:[{portId,label,type,optional}],outputs:[...]}
    property var edges: [] // {edgeId,fromNodeId,fromPortId,toNodeId,toPortId}
    property var groups: [] // {groupId,title,nodeIds:[...]}
    property var nodeTypes: [] // {type,title} offered by the Add menu
    property string selectedNodeId: ""
    property var selectedNodeIds: [] // extra host-owned selection used for grouping
    property string selectedEdgeId: ""
    property string selectedGroupId: ""
    property string searchText: ""
    readonly property var searchMatches: GraphGeometry.searchNodes(nodes, searchText)
    property string wireRouting: "curved" // curved | orthogonal
    property bool flowMode: false
    property real zoom: 1.0
    property real minZoom: 0.45
    property real maxZoom: 1.8
    property real panX: Tk.Theme.space.xl
    property real panY: Tk.Theme.space.xl
    property string tooltip: qsTr("Node graph")
    property string linkStatusText: ""
    property string linkStatusState: ""
    readonly property real nudgeStep: Tk.Theme.space.lg
    readonly property real nodeWidth: Tk.Theme.size.panelMinWidth
    readonly property int nodeHeight: Tk.Theme.size.header + Math.max(1, GraphGeometry.maxPortCount(nodes)) * Tk.Theme.size.row + Tk.Theme.space.sm

    signal nodeSelected(string nodeId)
    signal nodeSelectionToggled(string nodeId)
    signal edgeSelected(string edgeId)
    signal groupSelected(string groupId)
    signal nodeMoveRequested(string nodeId, real x, real y)
    signal groupMoveRequested(string groupId, real dx, real dy)
    signal nodeAddRequested(string type, real x, real y)
    signal nodeRemoveRequested(string nodeId)
    signal groupRequested(var nodeIds)
    signal ungroupRequested(string groupId)
    signal linkConnectRequested(string requestId, string fromNodeId,
                                string fromPortId, string toNodeId, string toPortId)
    signal linkDisconnectRequested(string requestId, string edgeId)

    property string pendingNode: ""
    property string pendingPort: ""
    property int requestSerial: 0
    property string latestLinkRequestId: ""
    property string keyboardConnectSourceNodeId: ""
    property string keyboardConnectSourcePortId: ""
    property bool dragCanceled: false
    property string draggingGroupId: ""
    property point groupDragDelta: Qt.point(0, 0)
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 nodes, %2 connections, %3 groups, %4 percent zoom")
        .arg(nodes ? nodes.length : 0).arg(edges ? edges.length : 0)
        .arg(groups ? groups.length : 0).arg(Math.round(zoom * 100))
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.onIncreaseAction: root.setZoom(root.zoom * 1.15)
    Accessible.onDecreaseAction: root.setZoom(root.zoom / 1.15)

    function nodeById(nodeId) { return GraphGeometry.nodeById(nodes, nodeId) }
    function portById(nodeId, portId, side) { return GraphGeometry.portById(nodes, nodeId, portId, side) }
    function portColor(type) { return Tk.Theme.color[GraphGeometry.portRole(type)] }
    function flowWidth(value) { return Math.max(Tk.Theme.size.border,
        Math.min(Tk.Theme.size.icon, Tk.Theme.size.border * (1 + Math.sqrt(Math.max(0, Number(value || 0)))))) }
    function visibleNode(record) { return GraphGeometry.visibleNode(record, nodeWidth,
        nodeHeight, zoom, panX, panY, width, height) }
    function isNodeSelected(nodeId) {
        return String(nodeId) === selectedNodeId || (selectedNodeIds || []).map(String).indexOf(String(nodeId)) >= 0
    }
    function selectionIds() {
        const ids = (selectedNodeIds || []).map(String)
        if (selectedNodeId && ids.indexOf(selectedNodeId) < 0) ids.push(selectedNodeId)
        return ids.filter(function(id) { return nodeById(id) !== null })
    }
    function groupOffset(nodeId) {
        return draggingGroupId && !dragCanceled && GraphGeometry.groupContains(groups, draggingGroupId, nodeId)
            ? groupDragDelta : Qt.point(0, 0)
    }
    function groupBounds(group) { return GraphGeometry.groupBounds(nodes, group, nodeWidth,
        nodeHeight, Tk.Theme.space.md, Tk.Theme.size.header) }
    function status(state, text) {
        linkStatusState = state
        linkStatusText = text
    }

    function beginOrFinishLink(nodeId, portId, side) {
        if (side === "output") {
            pendingNode = String(nodeId)
            pendingPort = String(portId)
            status("pending", qsTr("Choose an input port to request a connection"))
            return
        }
        if (!pendingNode || !pendingPort) {
            status("info", qsTr("Choose an output port first"))
            return
        }
        requestSerial += 1
        latestLinkRequestId = "node-link-" + requestSerial
        const fromNode = pendingNode, fromPort = pendingPort
        pendingNode = ""
        pendingPort = ""
        linkConnectRequested(latestLinkRequestId, fromNode, fromPort, String(nodeId), String(portId))
    }

    function armKeyboardLink() {
        const node = nodeById(selectedNodeId)
        if (!node || !(node.outputs || []).length) return false
        keyboardConnectSourceNodeId = selectedNodeId
        keyboardConnectSourcePortId = String(node.outputs[0].portId)
        pendingNode = keyboardConnectSourceNodeId
        pendingPort = keyboardConnectSourcePortId
        status("pending", qsTr("Connection armed from %1. Select a target node and press Enter.")
            .arg(node.title || node.nodeId))
        return true
    }

    function finishKeyboardLink() {
        if (!keyboardConnectSourceNodeId || !nodeById(selectedNodeId)) return false
        const port = portById(keyboardConnectSourceNodeId, keyboardConnectSourcePortId, "output")
        const target = GraphGeometry.targetInput(nodes, edges, selectedNodeId, port ? port.type : "")
        keyboardConnectSourceNodeId = ""
        keyboardConnectSourcePortId = ""
        if (target) beginOrFinishLink(selectedNodeId, target, "input")
        return true
    }

    function cancelInteraction() {
        pendingNode = ""
        pendingPort = ""
        keyboardConnectSourceNodeId = ""
        keyboardConnectSourcePortId = ""
        dragCanceled = true
        status("info", qsTr("Canceled"))
    }

    // Called one event-loop turn after a card or group drag ends.
    function finishDrag(kind, id, dx, dy) {
        const canceled = dragCanceled
        dragCanceled = false
        if (kind === "group") {
            draggingGroupId = ""
            groupDragDelta = Qt.point(0, 0)
        }
        if (canceled || (dx === 0 && dy === 0)) return false
        if (kind === "group") {
            if (!GraphGeometry.groupById(groups, id)) return false
            groupMoveRequested(id, dx / zoom, dy / zoom)
            return true
        }
        const node = nodeById(id)
        if (!node) return false
        nodeMoveRequested(id, Number(node.x || 0) + dx / zoom, Number(node.y || 0) + dy / zoom)
        return true
    }

    function requestDisconnect(edgeId) {
        if (!edgeId) return
        requestSerial += 1
        latestLinkRequestId = "node-link-" + requestSerial
        linkDisconnectRequested(latestLinkRequestId, String(edgeId))
    }

    function requestRemoveSelection() {
        if (selectedEdgeId) {
            requestDisconnect(selectedEdgeId)
            return true
        }
        if (!nodeById(selectedNodeId)) return false
        nodeRemoveRequested(selectedNodeId)
        return true
    }

    function requestGroup() {
        const ids = selectionIds()
        if (ids.length) groupRequested(ids)
        return ids.length > 0
    }

    function requestUngroup() {
        if (!GraphGeometry.groupById(groups, selectedGroupId)) return false
        ungroupRequested(selectedGroupId)
        return true
    }

    function requestAddNode(type) {
        nodeAddRequested(String(type), (width / 2 - panX) / zoom - nodeWidth / 2,
                         (height / 2 - panY) / zoom - nodeHeight / 2)
    }

    // A selected group takes precedence over the selected node.
    function nudge(dx, dy) {
        if (GraphGeometry.groupById(groups, selectedGroupId)) {
            groupMoveRequested(selectedGroupId, dx, dy)
            return true
        }
        const node = nodeById(selectedNodeId)
        if (!node) return false
        nodeMoveRequested(selectedNodeId, Number(node.x || 0) + dx, Number(node.y || 0) + dy)
        return true
    }

    function selectAdjacent(delta) {
        const count = (nodes || []).length
        if (!count) return false
        const index = GraphGeometry.nodeIndex(nodes, selectedNodeId)
        nodeSelected(String(nodes[Math.max(0, Math.min(count - 1, index < 0 ? 0 : index + delta))].nodeId))
        return true
    }

    function revealNode(nodeId) {
        const node = nodeById(nodeId)
        if (!node) return false
        panX = width / 2 - (Number(node.x || 0) + nodeWidth / 2) * zoom
        panY = height / 2 - (Number(node.y || 0) + nodeHeight / 2) * zoom
        nodeSelected(String(node.nodeId))
        return true
    }

    function findNext(backwards) {
        const matches = searchMatches
        if (!matches.length) {
            status("info", searchText ? qsTr("No nodes match “%1”").arg(searchText)
                                      : qsTr("Type a node title or type to search"))
            return true
        }
        const current = matches.indexOf(selectedNodeId)
        const next = current < 0 ? (backwards ? matches.length - 1 : 0)
            : (current + (backwards ? matches.length - 1 : 1)) % matches.length
        revealNode(matches[next])
        status("info", qsTr("Match %1 of %2").arg(next + 1).arg(matches.length))
        return true
    }

    function focusSearch() {
        toolbar.searchField.forceActiveFocus()
        return true
    }

    function acknowledgeLink(requestId, state, message) {
        if (String(requestId) !== latestLinkRequestId) return false
        status(String(state), String(message || (state === "accepted"
            ? qsTr("Connection accepted") : qsTr("Connection rejected"))))
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

    function edgePoint(nodeId, portId, side) { return GraphGeometry.edgePoint(nodes,
        nodeWidth, Tk.Theme.size.header, Tk.Theme.size.row, zoom, panX, panY, nodeId, portId, side) }

    // A pending link whose source node disappeared must not reach the host.
    onNodesChanged: if (pendingNode && !nodeById(pendingNode)) {
        pendingNode = ""
        pendingPort = ""
        keyboardConnectSourceNodeId = ""
        keyboardConnectSourcePortId = ""
        status("info", qsTr("The connection source was removed"))
    }
    Keys.onPressed: function(event) { event.accepted = GraphKeys.handle(root, event) }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    HoverHandler { id: graphHover }
    Tk.ToolTip { text: root.tooltip; visible: graphHover.hovered }

    Repeater {
        model: root.groups || []
        delegate: Tk.NodeGraphGroup { graph: root }
    }

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
                GraphGeometry.paintEdge(ctx, a, b, root.wireRouting, Tk.Theme.size.icon, Tk.Theme.space.xl)
                ctx.strokeStyle = String(edge.edgeId) === root.selectedEdgeId ? Tk.Theme.color.focus : root.portColor(port ? port.type : "")
                ctx.lineWidth = (root.flowMode ? root.flowWidth(edge.flow) : Tk.Theme.size.border) + (String(edge.edgeId) === root.selectedEdgeId ? Tk.Theme.size.border : 0)
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
            function onFlowModeChanged() { wireLayer.requestPaint() }
            function onSelectedEdgeIdChanged() { wireLayer.requestPaint() }
        }
    }

    // Connection preview from the armed output port to the pointer.
    Canvas {
        id: linkPreview
        objectName: "nodeGraphLinkPreview"
        anchors.fill: parent
        visible: Boolean(root.pendingNode && root.pendingPort)
        onVisibleChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!visible) return
            const a = root.edgePoint(root.pendingNode, root.pendingPort, "output")
            const b = graphHover.hovered ? graphHover.point.position
                : Qt.point(a.x + Tk.Theme.space.xl * 2, a.y)
            ctx.beginPath()
            GraphGeometry.paintEdge(ctx, a, b, root.wireRouting, Tk.Theme.size.icon, Tk.Theme.space.xl)
            ctx.strokeStyle = Tk.Theme.color.focus
            ctx.lineWidth = Tk.Theme.size.border
            ctx.stroke()
        }
        Connections {
            target: graphHover
            function onPointChanged() { if (linkPreview.visible) linkPreview.requestPaint() }
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
            readonly property point groupShift: root.groupOffset(modelData.nodeId)
            readonly property Tk.NodeGraphNode card: item as Tk.NodeGraphNode
            readonly property bool moving: card !== null && card.dragging && !root.dragCanceled
            active: root.visibleNode(modelData)
            x: root.panX + Number(modelData.x || 0) * root.zoom + groupShift.x
                + (moving ? card.dragDelta.x : 0)
            y: root.panY + Number(modelData.y || 0) * root.zoom + groupShift.y
                + (moving ? card.dragDelta.y : 0)
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
        Tk.NodeGraphNode { graph: root }
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
                // WheelEvent x/y are local to the graph (it has no `point`).
                root.zoomAt(root.zoom * factor, Qt.point(event.x, event.y))
            } else {
                root.panX -= event.angleDelta.x
                root.panY -= event.angleDelta.y
            }
            event.accepted = true
        }
    }

    Tk.NodeGraphToolbar {
        id: toolbar
        graph: root
        anchors.right: parent.right
        anchors.top: parent.top
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
