// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/diagram.md): the document owns identities, edits and
// history; this view forwards gestures and renders the current snapshot.
Item {
    id: root
    objectName: "diagramCanvas"
    property Tk.DiagramDocument document: null
    property string tooltip: qsTr("Diagram canvas")
    property alias selectedNodeId: graph.selectedNodeId
    property alias selectedEdgeId: graph.selectedEdgeId
    property alias zoom: graph.zoom
    property alias panX: graph.panX
    property alias panY: graph.panY
    readonly property alias graphView: graph

    implicitWidth: Tk.Theme.size.fieldWidth * 3
    implicitHeight: Tk.Theme.size.fieldWidth * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    Tk.NodeGraph {
        id: graph
        objectName: "diagramGraph"
        anchors.fill: parent
        tooltip: root.tooltip
        wireRouting: "orthogonal"
        nodes: root.document ? root.document.nodes : []
        edges: root.document ? root.document.edges : []

        onNodeSelected: function(nodeId) {
            graph.selectedNodeId = nodeId
            graph.selectedEdgeId = ""
        }
        onEdgeSelected: function(edgeId) {
            graph.selectedEdgeId = edgeId
            graph.selectedNodeId = ""
        }
        onNodeMoveRequested: function(nodeId, x, y) {
            if (root.document) root.document.moveNode(nodeId, x, y)
        }
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
}
