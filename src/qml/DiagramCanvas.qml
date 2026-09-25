// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/diagram.md): the document owns identities, edits,
// history and routes; this view forwards gestures and renders the current
// snapshot. Selection, snapping and viewport pose are local view state.
Item {
    id: root
    objectName: "diagramCanvas"
    property Tk.DiagramDocument document: null
    property bool flowMode: false
    property bool inspectorVisible: true
    // Document-unit grid for card moves; 0 places cards exactly where dropped.
    property real snapStep: 0
    // Stencils offered by the toolbar's Add menu (docs/diagram.md):
    // {type, title, inputs: [{label, type}], outputs: [{label, type}]}.
    property var stencils: []
    property string tooltip: qsTr("Diagram canvas")
    property alias selectedNodeId: graph.selectedNodeId
    property alias selectedNodeIds: graph.selectedNodeIds
    property alias selectedEdgeId: graph.selectedEdgeId
    property alias selectedGroupId: graph.selectedGroupId
    property alias zoom: graph.zoom
    property alias panX: graph.panX
    property alias panY: graph.panY
    readonly property alias graphView: graph
    // AGENT-CONTRACT (DiagramDocument::routeEdges, NodeGraphGeometry.edgePoint):
    // these must match the card geometry NodeGraph draws, or routes and
    // port anchors disagree.
    readonly property var routeMetrics: ({
        nodeWidth: graph.nodeWidth,
        nodeHeight: graph.nodeHeight,
        headerHeight: Tk.Theme.size.header,
        rowHeight: Tk.Theme.size.row,
        stub: Tk.Theme.size.icon,
        clearance: Tk.Theme.space.xl
    })
    readonly property var selectedNode: document && document.revision >= 0 && selectedNodeId
        ? graph.nodeById(selectedNodeId) : null
    readonly property int selectionCount: graph.selectionIds().length
    readonly property var arrangeCommands: [
        { command: "left", icon: "align-start-vertical", tip: qsTr("Align left edges") },
        { command: "centerX", icon: "align-center-vertical", tip: qsTr("Align horizontal centres") },
        { command: "right", icon: "align-end-vertical", tip: qsTr("Align right edges") },
        { command: "top", icon: "align-start-horizontal", tip: qsTr("Align top edges") },
        { command: "centerY", icon: "align-center-horizontal", tip: qsTr("Align vertical centres") },
        { command: "bottom", icon: "align-end-horizontal", tip: qsTr("Align bottom edges") },
        { command: "horizontal", icon: "align-horizontal-space-between",
          tip: qsTr("Distribute horizontally (three or more cards)") },
        { command: "vertical", icon: "align-vertical-space-between",
          tip: qsTr("Distribute vertically (three or more cards)") }
    ]
    readonly property var selectedEdge: document && document.revision >= 0 && selectedEdgeId
        ? root.edgeById(selectedEdgeId) : null

    function autoLayout() {
        return document && document.autoLayout(graph.nodeWidth + Tk.Theme.space.xl * 2,
                                               graph.nodeHeight + Tk.Theme.space.xl,
                                               Tk.Theme.space.lg, Tk.Theme.space.lg)
    }
    function exportSvg(path) { return document !== null && document.exportSvg(path, routeMetrics) }
    function edgeById(edgeId) {
        const source = document ? document.edges : []
        for (let i = 0; i < source.length; ++i)
            if (String(source[i].edgeId) === String(edgeId)) return source[i]
        return null
    }
    function snapped(value) { return snapStep > 0 ? Math.round(value / snapStep) * snapStep : value }
    function clearSelection() {
        graph.selectedNodeId = ""
        graph.selectedNodeIds = []
        graph.selectedEdgeId = ""
        graph.selectedGroupId = ""
    }
    // Reports a rejected request on the graph's status line.
    function report(ok, done) {
        graph.status(ok ? "info" : "error", ok ? done : document ? document.lastError : qsTr("No diagram"))
        return ok
    }
    // One stencil drop is one transaction: the card and all its ports, or nothing.
    function addStencil(type, x, y) {
        const stencil = (stencils || []).find(function(s) { return String(s.type) === String(type) })
        if (!document || !stencil) return ""
        document.beginTransaction()
        const id = document.addNode(String(stencil.title || stencil.type), snapped(x), snapped(y),
                                    String(stencil.type))
        let ok = id !== ""
        for (const side of ["inputs", "outputs"])
            for (const port of (stencil[side] || []))
                ok = ok && document.addPort(id, String(port.label), side === "inputs" ? "input" : "output",
                                            String(port.type || "any")) !== ""
        if (ok) document.commitTransaction()
        else document.cancelTransaction()
        report(ok, qsTr("Card added"))
        if (!ok) return ""
        clearSelection()
        graph.selectedNodeId = id
        return id
    }
    function addPort(direction) {
        if (!selectedNode) return ""
        const count = (direction === "input" ? selectedNode.inputs : selectedNode.outputs).length
        const id = document.addPort(selectedNodeId, (direction === "input" ? qsTr("In %1")
                                                                       : qsTr("Out %1")).arg(count + 1),
                                    direction, "any")
        report(id !== "", qsTr("Port added"))
        return id
    }
    function selectionIds() { return graph.selectionIds() }
    function alignSelection(edge) {
        return document !== null && report(document.alignNodes(selectionIds(), edge), qsTr("Aligned"))
    }
    function distributeSelection(axis) {
        return document !== null
            && report(document.distributeNodes(selectionIds(), axis), qsTr("Distributed"))
    }
    function groupById(groupId) {
        const source = document ? document.groups : []
        for (let i = 0; i < source.length; ++i)
            if (String(source[i].groupId) === String(groupId)) return source[i]
        return null
    }
    function groupSelection(nodeIds) {
        if (!document) return ""
        const id = document.addGroup(nodeIds, qsTr("Group %1").arg(document.groups.length + 1))
        report(id !== "", qsTr("Grouped"))
        if (id) { clearSelection(); graph.selectedGroupId = id }
        return id
    }
    function ungroup(groupId) {
        const ok = document !== null && report(document.removeGroup(groupId), qsTr("Ungrouped"))
        if (ok) graph.selectedGroupId = ""
        return ok
    }
    function moveGroup(groupId, dx, dy) {
        const group = groupById(groupId)
        return group !== null && document.moveNodes(group.nodeIds, snapped(dx), snapped(dy))
    }
    // AGENT-NOTE (docs/diagram.md): as in NodeGraph, selectedNodeIds is the
    // multi-selection and selectedNodeId the focused card. Acting on a member
    // of selectedNodeIds acts on every member; any other card acts alone.
    function groupFor(nodeId) {
        const ids = (graph.selectedNodeIds || []).map(String)
            .filter(function(id) { return graph.nodeById(id) !== null })
        return ids.indexOf(String(nodeId)) >= 0 ? ids : [String(nodeId)]
    }
    function moveNode(nodeId, x, y) {
        const node = document ? graph.nodeById(nodeId) : null
        if (!node) return false
        const targetX = snapped(x)
        const targetY = snapped(y)
        const ids = groupFor(nodeId)
        if (ids.length > 1)
            return document.moveNodes(ids, targetX - Number(node.x), targetY - Number(node.y))
        return document.moveNode(nodeId, targetX, targetY)
    }
    function removeNode(nodeId) {
        if (!document) return false
        const removed = document.removeNodes(groupFor(nodeId))
        if (removed) clearSelection()
        return removed
    }
    function commitTitle(text) {
        if (!selectedNode) return false
        const ok = document.setNodeTitle(selectedNodeId, text)
        titleEditor.text = selectedNode ? selectedNode.title : ""
        return ok
    }
    function commitFlow(value) {
        if (!selectedEdge) return false
        const ok = document.setEdgeFlow(selectedEdgeId, value)
        flowEditor.value = selectedEdge ? selectedEdge.flow : 0
        return ok
    }

    implicitWidth: Tk.Theme.size.fieldWidth * 3
    implicitHeight: Tk.Theme.size.fieldWidth * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    onSelectedNodeChanged: if (!titleEditor.activeFocus) titleEditor.text = selectedNode ? selectedNode.title : ""
    onSelectedEdgeChanged: if (!flowEditor.activeFocus) flowEditor.value = selectedEdge ? selectedEdge.flow : 0

    // NodeGraph leaves Ctrl+Z/Ctrl+Y unhandled, so they propagate here.
    Keys.onPressed: function(event) {
        if (!(event.modifiers & Qt.ControlModifier) || !root.document) return
        if (event.key === Qt.Key_Z)
            event.accepted = (event.modifiers & Qt.ShiftModifier) ? root.document.redo()
                                                                   : root.document.undo()
        else if (event.key === Qt.Key_Y)
            event.accepted = root.document.redo()
    }

    Tk.NodeGraph {
        id: graph
        objectName: "diagramGraph"
        anchors.fill: parent
        tooltip: root.tooltip
        wireRouting: "orthogonal"
        flowMode: root.flowMode
        nodes: root.document ? root.document.nodes : []
        edges: root.document ? root.document.edges : []
        groups: root.document ? root.document.groups : []
        nodeTypes: root.stencils
        edgeRoutes: root.document && root.document.revision >= 0
            ? root.document.routeEdges(root.routeMetrics) : ({})

        onNodeSelected: function(nodeId) {
            graph.selectedNodeId = nodeId
            graph.selectedEdgeId = ""
            graph.selectedGroupId = ""
        }
        onGroupSelected: function(groupId) {
            root.clearSelection()
            graph.selectedGroupId = groupId
        }
        onNodeAddRequested: function(type, x, y) { root.addStencil(type, x, y) }
        onGroupRequested: function(nodeIds) { root.groupSelection(nodeIds) }
        onUngroupRequested: function(groupId) { root.ungroup(groupId) }
        onGroupMoveRequested: function(groupId, dx, dy) { root.moveGroup(groupId, dx, dy) }
        onNodeSelectionToggled: function(nodeId) {
            const ids = (graph.selectedNodeIds || []).map(String)
            const at = ids.indexOf(String(nodeId))
            if (at >= 0) {
                ids.splice(at, 1)
                // NodeGraph.selectionIds() counts the focused card, so it
                // must leave with its toggle.
                if (graph.selectedNodeId === String(nodeId))
                    graph.selectedNodeId = ids.length ? ids[ids.length - 1] : ""
            } else {
                ids.push(String(nodeId))
                graph.selectedNodeId = String(nodeId)
            }
            graph.selectedNodeIds = ids
            graph.selectedEdgeId = ""
        }
        onEdgeSelected: function(edgeId) {
            graph.selectedEdgeId = edgeId
            graph.selectedNodeId = ""
            graph.selectedNodeIds = []
        }
        onNodeMoveRequested: function(nodeId, x, y) { root.moveNode(nodeId, x, y) }
        onNodeRemoveRequested: function(nodeId) { root.removeNode(nodeId) }
        onLinkConnectRequested: function(requestId, fromNodeId, fromPortId, toNodeId, toPortId) {
            const id = root.document
                ? root.document.connectPorts(fromNodeId, fromPortId, toNodeId, toPortId) : ""
            graph.acknowledgeLink(requestId, id ? "accepted" : "rejected",
                                  id ? qsTr("Connection added")
                                     : root.document ? root.document.lastError : qsTr("No diagram"))
        }
        onLinkDisconnectRequested: function(requestId, edgeId) {
            const removed = root.document && root.document.disconnectEdge(edgeId)
            if (removed && graph.selectedEdgeId === edgeId) graph.selectedEdgeId = ""
            graph.acknowledgeLink(requestId, removed ? "accepted" : "rejected",
                                  removed ? qsTr("Connection removed")
                                          : root.document ? root.document.lastError : qsTr("No diagram"))
        }
    }

    // Property strip for the single selected card or wire. Edits go through
    // the document, so each commit is one undo step.
    Rectangle {
        objectName: "diagramInspector"
        visible: root.inspectorVisible && (root.selectedNode !== null || root.selectedEdge !== null)
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: Tk.Theme.space.md
        anchors.bottomMargin: Tk.Theme.size.controlLg + Tk.Theme.space.md
        width: inspectorRow.implicitWidth
        height: inspectorRow.implicitHeight
        radius: Tk.Theme.radius.md
        color: Tk.Theme.color.panel
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        z: 3

        Tk.Flex {
            id: inspectorRow
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.sm

            Tk.Label {
                objectName: "diagramInspectorLabel"
                text: root.selectedEdge ? qsTr("Flow")
                    : root.selectionCount >= 2 ? qsTr("%1 cards").arg(root.selectionCount) : qsTr("Title")
                muted: true
            }
            Tk.TextField {
                id: titleEditor
                objectName: "diagramTitleEditor"
                visible: root.selectedNode !== null && root.selectionCount < 2
                small: true
                implicitWidth: Tk.Theme.size.fieldWidth * 1.5
                tooltip: qsTr("Card title")
                Accessible.name: tooltip
                onAccepted: root.commitTitle(text)
            }
            Tk.NumberField {
                id: flowEditor
                objectName: "diagramFlowEditor"
                visible: root.selectedEdge !== null
                small: true
                stepButtons: false
                scrub: false
                decimals: 2
                from: 0
                to: 1000000000
                tooltip: qsTr("Connection flow")
                onValueModified: function(next) { root.commitFlow(next) }
            }
            Tk.Button {
                objectName: "diagramAddInput"
                visible: root.selectedNode !== null && root.selectionCount < 2
                small: true
                iconName: "plus"
                text: qsTr("Input")
                tooltip: qsTr("Add an input port to this card")
                onClicked: root.addPort("input")
            }
            Tk.Button {
                objectName: "diagramAddOutput"
                visible: root.selectedNode !== null && root.selectionCount < 2
                small: true
                iconName: "plus"
                text: qsTr("Output")
                tooltip: qsTr("Add an output port to this card")
                onClicked: root.addPort("output")
            }
            Repeater {
                model: root.selectionCount >= 2 ? root.arrangeCommands : []
                delegate: Tk.IconButton {
                    required property var modelData
                    objectName: "diagramArrange_" + modelData.command
                    small: true
                    iconName: modelData.icon
                    tooltip: modelData.tip
                    enabled: modelData.command !== "horizontal" && modelData.command !== "vertical"
                        || root.selectionCount >= 3
                    onClicked: modelData.command === "horizontal" || modelData.command === "vertical"
                        ? root.distributeSelection(modelData.command) : root.alignSelection(modelData.command)
                }
            }
        }
    }
}
