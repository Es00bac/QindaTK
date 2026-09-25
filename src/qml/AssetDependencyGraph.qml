// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The selected asset's neighbourhood drawn with the shared NodeGraph: inputs
// on the left, the asset in the middle, dependents on the right. Selecting a
// node re-centres on it. Read-only: move and link requests are ignored.
// AGENT-CONTRACT (EXT-10): graph state comes from Tk.AssetDependencyIndex
// (dependencies, dependents, asset); positions are computed here from theme
// sizes and are view state, never written back to the index.
Item {
    id: root
    objectName: "assetDependencyGraph"
    property Tk.AssetDependencyIndex index: null
    property string selectedId: ""
    // Bumped by the owner on index changes, so the snapshot rebuilds.
    property int revision: 0
    property int maxPerSide: 12
    property string tooltip: qsTr("Dependency graph")
    readonly property alias graphView: graph
    // {nodes, edges, hiddenInputs, hiddenDependents}
    readonly property var neighbourhood: build(revision, selectedId)
    signal assetSelected(string assetId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function build(revision, id) {
        const empty = {nodes: [], edges: [], hiddenInputs: 0, hiddenDependents: 0}
        if (revision < 0 || !index || !id || index.asset(id).id === undefined) return empty
        const inputs = index.dependencies(id)
        const users = index.dependents(id)
        const shownInputs = inputs.slice(0, maxPerSide)
        const shownUsers = users.filter(u => shownInputs.indexOf(u) < 0).slice(0, maxPerSide)
        const columnStep = graph.nodeWidth + Tk.Theme.space.xl * 2
        const rowStep = graph.nodeHeight + Tk.Theme.space.lg
        const tallest = Math.max(shownInputs.length, shownUsers.length, 1)
        const nodes = []
        const node = (assetId, column, row, rows) => {
            const asset = index.asset(assetId)
            const offset = (tallest - rows) * rowStep / 2
            nodes.push({nodeId: assetId,
                        title: (asset.label || assetId) + (asset.available ? "" : qsTr(" (missing)")),
                        x: column * columnStep, y: offset + row * rowStep,
                        inputs: [{portId: "in", label: qsTr("uses"), type: "asset", optional: true}],
                        outputs: [{portId: "out", label: qsTr("used by"), type: "asset"}]})
        }
        shownInputs.forEach((input, row) => node(input, 0, row, shownInputs.length))
        if (shownInputs.indexOf(id) < 0) node(id, 1, 0, 1)
        shownUsers.forEach((user, row) => node(user, 2, row, shownUsers.length))
        const edges = []
        for (const input of shownInputs)
            edges.push({edgeId: input + "→" + id, fromNodeId: input, fromPortId: "out",
                        toNodeId: id, toPortId: "in"})
        for (const user of users)
            if (nodes.some(n => n.nodeId === user))
                edges.push({edgeId: id + "→" + user, fromNodeId: id, fromPortId: "out",
                            toNodeId: user, toPortId: "in"})
        return {nodes: nodes, edges: edges,
                hiddenInputs: inputs.length - shownInputs.length,
                hiddenDependents: users.length - shownUsers.length
                                  - users.filter(u => shownInputs.indexOf(u) >= 0).length}
    }

    // Fits the neighbourhood below NodeGraph's toolbar whenever it or the
    // size changes; wheel and drag still zoom and pan until the next change.
    function fit() {
        const nodes = neighbourhood.nodes
        if (nodes.length === 0 || width <= 0 || height <= 0) return
        let right = 0, bottom = 0
        for (const node of nodes) {
            right = Math.max(right, node.x + graph.nodeWidth)
            bottom = Math.max(bottom, node.y + graph.nodeHeight)
        }
        const margin = Tk.Theme.space.lg
        const top = Tk.Theme.size.control + 2 * Tk.Theme.space.xs + margin
        const fitted = Math.min(1, (width - 2 * margin) / right, (height - top - margin) / bottom)
        graph.zoom = Math.max(graph.minZoom, fitted)
        graph.panY = top
        if (fitted >= graph.minZoom) {
            graph.panX = Math.max(margin, (width - right * graph.zoom) / 2)
            return
        }
        // Too narrow even at the smallest zoom: keep the selected asset centred.
        const selected = nodes.find(node => node.nodeId === selectedId)
        graph.panX = width / 2 - ((selected ? selected.x : 0) + graph.nodeWidth / 2) * graph.zoom
    }
    onNeighbourhoodChanged: fit()
    onWidthChanged: fit()
    onHeightChanged: fit()

    Tk.NodeGraph {
        id: graph
        objectName: "dependencyNodeGraph"
        anchors.fill: parent
        nodes: root.neighbourhood.nodes
        edges: root.neighbourhood.edges
        selectedNodeId: root.selectedId
        tooltip: root.tooltip
        onNodeSelected: function(nodeId) { if (nodeId) root.assetSelected(nodeId) }
    }
    Tk.Caption {
        objectName: "dependencyGraphHidden"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Tk.Theme.space.sm
        visible: root.neighbourhood.hiddenInputs + root.neighbourhood.hiddenDependents > 0
        text: qsTr("%1 more inputs · %2 more dependents; use the list for all of them")
              .arg(root.neighbourhood.hiddenInputs).arg(root.neighbourhood.hiddenDependents)
        color: Tk.Theme.color.textMuted
    }
}
