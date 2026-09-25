// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/diagram.md, DiagramDocument::sankeyLayout): band
// placement, widths and node heights come from the document in pixels; this
// view only offsets them by its margin and paints. It never edits the graph.
Item {
    id: root
    objectName: "sankeyDiagram"
    property Tk.DiagramDocument document: null
    property string tooltip: qsTr("Sankey diagram")
    property string selectedEdgeId: ""
    // Relative conservation tolerance for through nodes (flowImbalances).
    property real tolerance: 1e-9
    readonly property real barWidth: Tk.Theme.space.lg
    readonly property real margin: Tk.Theme.space.md
    readonly property var layout: document && document.revision >= 0
        && width > barWidth + margin * 2 && height > margin * 2
        ? document.sankeyLayout(width - margin * 2, height - margin * 2, barWidth, Tk.Theme.space.lg)
        : ({ valid: false })
    readonly property var imbalances: document && document.revision >= 0
        ? document.flowImbalances(tolerance) : []
    readonly property var nodeTitles: {
        const titles = {}
        const source = layout.valid ? layout.nodes : []
        for (let i = 0; i < source.length; ++i) titles[source[i].nodeId] = source[i].title
        return titles
    }

    signal edgeSelected(string edgeId)

    function imbalanceFor(nodeId) {
        for (let i = 0; i < imbalances.length; ++i)
            if (String(imbalances[i].nodeId) === String(nodeId)) return imbalances[i]
        return null
    }
    function formatFlow(value) { return String(Math.round(Number(value) * 100) / 100) }

    implicitWidth: Tk.Theme.size.fieldWidth * 4
    implicitHeight: Tk.Theme.size.fieldWidth * 2
    Accessible.role: Accessible.Chart
    Accessible.name: tooltip
    Accessible.description: imbalances.length
        ? qsTr("%1 nodes do not conserve flow").arg(imbalances.length) : qsTr("Flow is conserved")

    onLayoutChanged: bands.requestPaint()
    onSelectedEdgeIdChanged: bands.requestPaint()

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    HoverHandler { id: hover }
    Tk.ToolTip { text: root.tooltip; visible: hover.hovered && !root.layout.valid }

    Tk.Label {
        objectName: "sankeyEmpty"
        anchors.centerIn: parent
        visible: !root.layout.valid
        text: qsTr("No flow to show")
        muted: true
    }

    Canvas {
        id: bands
        objectName: "sankeyBands"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const links = root.layout.valid ? root.layout.links : []
            for (let i = 0; i < links.length; ++i) {
                const link = links[i]
                if (link.width <= 0) continue
                const x0 = root.margin + link.x0, y0 = root.margin + link.y0
                const x1 = root.margin + link.x1, y1 = root.margin + link.y1
                ctx.beginPath()
                ctx.moveTo(x0, y0)
                if (link.feedback) {
                    // Return lane below every bar (sankeyLayout laneY/loop):
                    // out right, down, back left, up into the target's left.
                    const lane = root.margin + link.laneY
                    ctx.lineTo(x0 + link.loop, y0)
                    ctx.lineTo(x0 + link.loop, lane)
                    ctx.lineTo(x1 - link.loop, lane)
                    ctx.lineTo(x1 - link.loop, y1)
                    ctx.lineTo(x1, y1)
                } else {
                    const middle = (x0 + x1) / 2
                    ctx.bezierCurveTo(middle, y0, middle, y1, x1, y1)
                }
                ctx.lineWidth = Math.max(Tk.Theme.size.border, link.width)
                ctx.lineJoin = "round"
                ctx.strokeStyle = link.feedback ? Tk.Theme.color.warning : Tk.Theme.color.accent
                ctx.globalAlpha = String(link.edgeId) === root.selectedEdgeId
                    ? Tk.Theme.opacity.muted : Tk.Theme.opacity.disabled
                ctx.stroke()
            }
            ctx.globalAlpha = Tk.Theme.opacity.full
        }
    }

    // Hit targets and accessible names for bands, at each band's midpoint.
    Repeater {
        model: root.layout.valid ? root.layout.links : []
        delegate: Item {
            id: band
            required property var modelData
            readonly property string tooltip: qsTr("%1 → %2: %3")
                .arg(root.nodeTitles[modelData.fromNodeId] || "")
                .arg(root.nodeTitles[modelData.toNodeId] || "")
                .arg(root.formatFlow(modelData.flow))
            objectName: "sankeyLink_" + modelData.edgeId
            width: Tk.Theme.size.icon
            height: Math.max(Tk.Theme.size.icon, modelData.width)
            x: root.margin + (modelData.x0 + modelData.x1) / 2 - width / 2
            y: root.margin + (modelData.feedback ? modelData.laneY
                                                 : (modelData.y0 + modelData.y1) / 2) - height / 2
            Accessible.role: Accessible.Graphic
            Accessible.name: tooltip
            HoverHandler { id: bandHover }
            TapHandler {
                onTapped: {
                    root.selectedEdgeId = String(band.modelData.edgeId)
                    root.edgeSelected(root.selectedEdgeId)
                }
            }
            Tk.ToolTip { text: band.tooltip; visible: bandHover.hovered }
        }
    }

    Repeater {
        model: root.layout.valid ? root.layout.nodes : []
        delegate: Rectangle {
            id: bar
            required property var modelData
            readonly property var imbalance: root.imbalanceFor(modelData.nodeId)
            readonly property bool lastColumn: modelData.column === root.layout.columns - 1
                && root.layout.columns > 1
            readonly property string tooltip: imbalance
                ? qsTr("%1: in %2, out %3, imbalance %4").arg(modelData.title)
                    .arg(root.formatFlow(modelData.inFlow)).arg(root.formatFlow(modelData.outFlow))
                    .arg(root.formatFlow(imbalance.imbalance))
                : qsTr("%1: in %2, out %3").arg(modelData.title)
                    .arg(root.formatFlow(modelData.inFlow)).arg(root.formatFlow(modelData.outFlow))
            objectName: "sankeyNode_" + modelData.nodeId
            x: root.margin + modelData.x
            y: root.margin + modelData.y
            width: modelData.width
            height: Math.max(Tk.Theme.size.border, modelData.height)
            color: imbalance ? Tk.Theme.color.warning : Tk.Theme.color.accent
            radius: Tk.Theme.radius.xs
            Accessible.role: Accessible.Graphic
            Accessible.name: tooltip
            HoverHandler { id: barHover }
            Tk.ToolTip { text: bar.tooltip; visible: barHover.hovered }

            Tk.Label {
                objectName: "sankeyLabel_" + bar.modelData.nodeId
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: bar.lastColumn ? undefined : parent.right
                anchors.right: bar.lastColumn ? parent.left : undefined
                // Clear the vertical legs of return lanes in this column.
                anchors.leftMargin: Tk.Theme.space.sm + bar.modelData.labelInsetRight
                anchors.rightMargin: Tk.Theme.space.sm + bar.modelData.labelInsetLeft
                text: qsTr("%1 · %2").arg(bar.modelData.title).arg(root.formatFlow(bar.modelData.value))
                font.pixelSize: Tk.Theme.font.caption
                color: bar.imbalance ? Tk.Theme.color.warning : Tk.Theme.color.text
            }
        }
    }
}
