// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: fixture
    width: 1120
    height: 650

    Tk.NodeGraphModel { id: model }
    property string outputNodeId: ""

    Component.onCompleted: {
        const teal = model.addNode("solid", "Teal source", 28, 88,
                                   { color: "#007f99" })
        const amber = model.addNode("solid", "Amber source", 28, 310,
                                    { color: "#e7a24a" })
        const blend = model.addNode("blend", "Blend", 260, 188,
                                    { opacity: 0.5 })
        outputNodeId = model.addNode("output", "Output", 490, 188)
        model.connectPorts(teal, "image", blend, "background")
        model.connectPorts(amber, "image", blend, "foreground")
        model.connectPorts(blend, "image", outputNodeId, "image")
        model.render(outputNodeId, 320, 180)
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Label { text: qsTr("Live image graph"); font.pixelSize: Tk.Theme.font.title }
        Tk.Label {
            text: qsTr("The toolkit model supplies nodes, typed links, edit history and rendered pixels to the reusable editor.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.flex: 1
            Tk.NodeGraph {
                id: graph
                Tk.Flex.flex: 1
                nodes: model.nodes
                edges: model.edges
                onNodeMoveRequested: function(nodeId, x, y) { model.moveNode(nodeId, x, y) }
                onLinkConnectRequested: function(requestId, fromNodeId, fromPortId,
                                                 toNodeId, toPortId) {
                    const edgeId = model.connectPorts(fromNodeId, fromPortId,
                                                      toNodeId, toPortId)
                    graph.acknowledgeLink(requestId, edgeId ? "accepted" : "rejected",
                                          edgeId ? qsTr("Connected") : model.lastError)
                    if (edgeId) model.render(fixture.outputNodeId, 320, 180)
                }
                onLinkDisconnectRequested: function(requestId, edgeId) {
                    const ok = model.disconnectEdge(edgeId)
                    graph.acknowledgeLink(requestId, ok ? "accepted" : "rejected",
                                          ok ? qsTr("Disconnected") : model.lastError)
                    if (ok) model.render(fixture.outputNodeId, 320, 180)
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                width: 320
                Tk.Label { text: qsTr("Rendered output") }
                Image {
                    objectName: "nodeGraphRenderedOutput"
                    width: 320
                    height: 180
                    source: model.imageDataUrl
                    fillMode: Image.PreserveAspectFit
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Blended teal and amber image")
                }
                Tk.Label {
                    text: qsTr("Drag nodes or reconnect ports; accepted edits update the model.")
                    muted: true
                    width: 320
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
