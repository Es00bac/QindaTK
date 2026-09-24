// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Live typed compositing graph: NodeGraph turns gestures into requests,
// NodeGraphModel validates each edit, keeps history and renders the output.
Item {
    id: fixture
    width: 1120
    height: 650

    Tk.NodeGraphModel { id: graphModel }
    property string outputNodeId: ""
    property string blendNodeId: ""
    property string renderError: ""
    property bool ready: false
    readonly property var blendModes: {
        const types = graphModel.nodeTypes
        for (let i = 0; i < types.length; ++i) {
            const parameters = types[i].type === "blend" ? types[i].parameters : []
            for (let j = 0; j < parameters.length; ++j)
                if (parameters[j].parameterId === "mode") return parameters[j].options
        }
        return []
    }

    function rerender() {
        const result = graphModel.render(outputNodeId, 320, 180)
        renderError = result.ok ? "" : String(result.error)
    }

    function parameterOf(nodeId, parameterId, fallback) {
        const nodes = graphModel.nodes
        for (let i = 0; i < nodes.length; ++i)
            if (nodes[i].nodeId === nodeId && nodes[i].parameters[parameterId] !== undefined)
                return nodes[i].parameters[parameterId]
        return fallback
    }

    function reject() {
        graph.linkStatusState = "rejected"
        graph.linkStatusText = graphModel.lastError
    }

    function select(nodeId, edgeId, groupId) {
        graph.selectedNodeId = nodeId
        graph.selectedEdgeId = edgeId
        graph.selectedGroupId = groupId
    }

    // Document positions follow the density's card size so cards never overlap.
    readonly property real column: graph.nodeWidth + 48
    readonly property real graphExtent: column * 2 + graph.nodeWidth

    Component.onCompleted: {
        // Cards are sized for the blend node's three inputs (see nodeHeight).
        const row = Tk.Theme.size.header + 3 * Tk.Theme.size.row + Tk.Theme.space.sm + 32
        const teal = graphModel.addNode("solid", "Teal plate", 0, 32, { color: "#007f99" })
        const amber = graphModel.addNode("solid", "Amber plate", 0, 32 + row, { color: "#e7a24a" })
        const disc = graphModel.addNode("mask", "Disc mask", 0, 48 + row * 2,
            { shape: "ellipse", x: 100, y: 30, width: 120, height: 120 })
        blendNodeId = graphModel.addNode("blend", "Blend", column, 16 + row, { opacity: 0.9 })
        outputNodeId = graphModel.addNode("output", "Output", column * 2, 16 + row)
        graphModel.connectPorts(teal, "image", blendNodeId, "background")
        graphModel.connectPorts(amber, "image", blendNodeId, "foreground")
        graphModel.connectPorts(disc, "mask", blendNodeId, "mask")
        graphModel.connectPorts(blendNodeId, "image", outputNodeId, "image")
        graphModel.groupNodes([teal, amber], qsTr("Plates"))
        ready = true
        rerender()
    }

    Connections {
        target: graphModel
        function onChanged() { if (fixture.ready) fixture.rerender() }
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Label { text: qsTr("Live image graph"); font.pixelSize: Tk.Theme.font.title }
        Tk.Label {
            text: qsTr("NodeGraph requests every edit; NodeGraphModel checks port types and cycles, keeps groups and undo history, and renders the output node.")
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
                // Fit the three columns; a user zoom replaces this binding.
                zoom: Math.min(1, Math.max(minZoom, (width - panX * 2) / fixture.graphExtent))
                nodes: graphModel.nodes
                edges: graphModel.edges
                groups: graphModel.groups
                nodeTypes: graphModel.nodeTypes
                onNodeSelected: function(nodeId) { fixture.select(nodeId, "", "") }
                onEdgeSelected: function(edgeId) { fixture.select("", edgeId, "") }
                onGroupSelected: function(groupId) { fixture.select("", "", groupId) }
                onNodeSelectionToggled: function(nodeId) {
                    const ids = graph.selectedNodeIds.slice()
                    const at = ids.indexOf(nodeId)
                    if (at >= 0) ids.splice(at, 1)
                    else ids.push(nodeId)
                    graph.selectedNodeIds = ids
                }
                onNodeMoveRequested: function(nodeId, x, y) {
                    if (!graphModel.moveNode(nodeId, x, y)) fixture.reject()
                }
                onGroupMoveRequested: function(groupId, dx, dy) {
                    if (!graphModel.moveGroup(groupId, dx, dy)) fixture.reject()
                }
                onNodeAddRequested: function(type, x, y) {
                    const nodeId = graphModel.addNode(type, "", x, y)
                    if (nodeId) fixture.select(nodeId, "", "")
                    else fixture.reject()
                }
                onNodeRemoveRequested: function(nodeId) {
                    if (!graphModel.removeNode(nodeId)) return fixture.reject()
                    graph.selectedNodeIds = graph.selectedNodeIds.filter(function(id) { return id !== nodeId })
                    fixture.select("", "", "")
                }
                onGroupRequested: function(nodeIds) {
                    const groupId = graphModel.groupNodes(nodeIds, qsTr("Group"))
                    if (!groupId) return fixture.reject()
                    graph.selectedNodeIds = []
                    fixture.select("", "", groupId)
                }
                onUngroupRequested: function(groupId) {
                    if (graphModel.ungroup(groupId)) fixture.select("", "", "")
                    else fixture.reject()
                }
                onLinkConnectRequested: function(requestId, fromNodeId, fromPortId,
                                                 toNodeId, toPortId) {
                    const edgeId = graphModel.connectPorts(fromNodeId, fromPortId,
                                                           toNodeId, toPortId)
                    graph.acknowledgeLink(requestId, edgeId ? "accepted" : "rejected",
                                          edgeId ? qsTr("Connected") : graphModel.lastError)
                }
                onLinkDisconnectRequested: function(requestId, edgeId) {
                    const ok = graphModel.disconnectEdge(edgeId)
                    if (ok) fixture.select("", "", "")
                    graph.acknowledgeLink(requestId, ok ? "accepted" : "rejected",
                                          ok ? qsTr("Disconnected") : graphModel.lastError)
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Flex.basis: 320
                Tk.Flex.shrink: 0
                Tk.Label { text: qsTr("Rendered output") }
                Image {
                    objectName: "nodeGraphRenderedOutput"
                    width: 320
                    height: 180
                    source: graphModel.imageDataUrl
                    fillMode: Image.PreserveAspectFit
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Composited graph output")
                }
                Tk.Label {
                    objectName: "nodeGraphRenderState"
                    width: 320
                    wrapMode: Text.Wrap
                    text: fixture.renderError
                        ? qsTr("Render failed: %1. The last good frame stays; reconnect or undo to recover.")
                            .arg(fixture.renderError)
                        : qsTr("Rendered revision %1").arg(graphModel.revision)
                    color: fixture.renderError ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                }
                Tk.Label { text: qsTr("Blend opacity and mode") }
                Tk.Slider {
                    objectName: "blendOpacitySlider"
                    width: 320
                    from: 0
                    to: 1
                    stepSize: 0.01
                    showValue: true
                    decimals: 2
                    tooltip: qsTr("Blend opacity; one drag is one undo step")
                    value: fixture.parameterOf(fixture.blendNodeId, "opacity", 1)
                    onPressedChanged: pressed ? graphModel.beginEdit() : graphModel.commitEdit()
                    onMoved: graphModel.setParameter(fixture.blendNodeId, "opacity", value)
                }
                Tk.ComboBox {
                    objectName: "blendModeCombo"
                    width: 320
                    model: fixture.blendModes
                    currentIndex: fixture.blendModes.indexOf(
                        fixture.parameterOf(fixture.blendNodeId, "mode", "normal"))
                    tooltip: qsTr("Blend mode")
                    onActivated: function(index) {
                        graphModel.setParameter(fixture.blendNodeId, "mode", fixture.blendModes[index])
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    Tk.Button {
                        objectName: "graphUndoButton"
                        text: qsTr("Undo")
                        iconName: "undo-2"
                        small: true
                        enabled: graphModel.canUndo
                        tooltip: qsTr("Undo the last graph edit")
                        onClicked: graphModel.undo()
                    }
                    Tk.Button {
                        objectName: "graphRedoButton"
                        text: qsTr("Redo")
                        iconName: "redo-2"
                        small: true
                        enabled: graphModel.canRedo
                        tooltip: qsTr("Redo the graph edit")
                        onClicked: graphModel.redo()
                    }
                }
                Tk.Label {
                    text: qsTr("Drag or Shift+arrow to move. Ctrl+click or Ctrl+Space adds to the selection; Ctrl+G groups. Ctrl+F finds nodes. Space then Enter links from the keyboard.")
                    muted: true
                    width: 320
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
