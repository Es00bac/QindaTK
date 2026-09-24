// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: fixture
    width: 920
    height: 640
    property var graphNodes: [
        { nodeId: "camera", title: "Camera source", x: 36, y: 86,
          inputs: [], outputs: [{ portId: "frame", label: "Frame", type: "video" }] },
        { nodeId: "grade", title: "Color grade", x: 340, y: 142,
          inputs: [{ portId: "image-in", label: "Image", type: "video" }],
          outputs: [{ portId: "image-out", label: "Graded", type: "image" }] },
        { nodeId: "audio", title: "Audio meter", x: 340, y: 360,
          inputs: [{ portId: "audio-in", label: "Audio", type: "audio" }], outputs: [] },
        { nodeId: "viewer", title: "Viewer", x: 650, y: 180,
          inputs: [{ portId: "image-in", label: "Image", type: "image" }], outputs: [] }
    ]
    property var graphEdges: [{ edgeId: "edge-source-grade",
        fromNodeId: "camera", fromPortId: "frame",
        toNodeId: "grade", toPortId: "image-in" }]
    property int nextEdge: 0
    property string selectedNode: "camera"
    property string selectedEdge: ""

    function replaceNodePosition(nodeId, x, y) {
        const next = graphNodes.slice()
        for (let i = 0; i < next.length; ++i) {
            if (next[i].nodeId === nodeId)
                next[i] = Object.assign({}, next[i], { x: x, y: y })
        }
        graphNodes = next
    }

    function findPort(nodeId, portId, side) {
        for (const node of graphNodes) {
            if (node.nodeId !== nodeId) continue
            const ports = side === "output" ? node.outputs : node.inputs
            for (const port of ports)
                if (port.portId === portId) return port
        }
        return null
    }

    function connect(requestId, fromNodeId, fromPortId, toNodeId, toPortId) {
        const output = findPort(fromNodeId, fromPortId, "output")
        const input = findPort(toNodeId, toPortId, "input")
        if (!output || !input || output.type !== input.type) {
            graph.acknowledgeLink(requestId, "rejected",
                qsTr("Host rejected %1 → %2: port types must match.")
                    .arg(output ? output.type : qsTr("missing"))
                    .arg(input ? input.type : qsTr("missing")))
            return
        }
        nextEdge += 1
        graphEdges = graphEdges.concat([{
            edgeId: "edge-" + nextEdge,
            fromNodeId: fromNodeId, fromPortId: fromPortId,
            toNodeId: toNodeId, toPortId: toPortId
        }])
        graph.acknowledgeLink(requestId, "accepted", qsTr("Host accepted connection"))
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("Node graph"); font.pixelSize: Tk.Theme.font.title }
            Tk.Badge { text: qsTr("Host fixture"); variant: "accent" }
        }
        Tk.Label {
            text: qsTr("The host supplied Camera → Color grade. Connect Color grade → Viewer to accept; try Camera → Audio meter to see type rejection.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.NodeGraph {
            id: graph
            Tk.Flex.flex: 1
            nodes: fixture.graphNodes
            edges: fixture.graphEdges
            selectedNodeId: fixture.selectedNode
            selectedEdgeId: fixture.selectedEdge
            onNodeSelected: function(nodeId) { fixture.selectedNode = nodeId }
            onEdgeSelected: function(edgeId) { fixture.selectedEdge = edgeId }
            onNodeMoveRequested: function(nodeId, x, y) {
                fixture.replaceNodePosition(nodeId, x, y)
            }
            onLinkConnectRequested: function(requestId, fromNodeId, fromPortId,
                                             toNodeId, toPortId) {
                fixture.connect(requestId, fromNodeId, fromPortId, toNodeId, toPortId)
            }
            onLinkDisconnectRequested: function(requestId, edgeId) {
                fixture.graphEdges = fixture.graphEdges.filter(
                    function(edge) { return edge.edgeId !== edgeId })
                fixture.selectedEdge = ""
                graph.acknowledgeLink(requestId, "accepted", qsTr("Host removed connection"))
            }
        }
        Tk.Label {
            text: qsTr("Keyboard: select a node, Space to arm its first output, select a target, Enter to request. Select a wire then Delete to disconnect.")
            muted: true
            wrapMode: Text.Wrap
        }
    }
}
