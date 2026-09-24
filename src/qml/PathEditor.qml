// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Overlay this on a CanvasView. The document owns every node edit and undo;
// the item only previews a drag until pointer release.
Item {
    id: root
    objectName: "pathEditor"
    property var document
    property var surface
    property string objectId: ""
    property int contourIndex: 0
    property string tooltip: qsTr("Edit path nodes and Bézier handles")
    property var nodes: []
    property var objectMatrix: [1, 0, 0, 1, 0, 0]
    property int geometryRevision: 0
    signal editRejected(string message)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function refresh() {
        nodes = document && objectId ? document.pathContourNodes(objectId, contourIndex) : []
        const info = document && objectId ? document.objectInfo(objectId) : null
        objectMatrix = info && info.matrix ? info.matrix : [1, 0, 0, 1, 0, 0]
        guides.requestPaint()
    }
    function mapLocal(x, y) {
        const revision = geometryRevision
        const m = objectMatrix
        const docPoint = Qt.point(m[0] * x + m[2] * y + m[4],
                                  m[1] * x + m[3] * y + m[5])
        return surface ? surface.mapFromDocument(docPoint) : Qt.point(0, 0)
    }
    function handlePoint(index, part) {
        const node = nodes[index]
        // AGENT-GUARD: the Repeater follows nodes.length, so when a shorter
        // node list arrives (fewer star points, a removed node, another
        // selected object) trailing handles re-evaluate once before removal.
        if (!node) return Qt.point(0, 0)
        const anchor = mapLocal(node.x, node.y)
        if (part === "anchor") return anchor
        const handle = mapLocal(node[part + "X"], node[part + "Y"])
        if (node[part + "X"] === node.x && node[part + "Y"] === node.y)
            return Qt.point(anchor.x + (part === "in" ? -1 : 1)
                            * Tk.Theme.size.controlSm * 1.5, anchor.y)
        return handle
    }
    function localFromView(viewPoint) {
        const docPoint = surface.mapToDocument(viewPoint)
        const m = objectMatrix
        const det = m[0] * m[3] - m[1] * m[2]
        if (Math.abs(det) < 1e-12) return null
        const x = docPoint.x - m[4]
        const y = docPoint.y - m[5]
        return Qt.point((m[3] * x - m[2] * y) / det,
                        (-m[1] * x + m[0] * y) / det)
    }
    function finish(index, part, viewPoint) {
        if (!document || !surface) return
        const local = localFromView(viewPoint)
        if (!local || !document.movePathContourPart(objectId, contourIndex, index, part, local.x, local.y))
            editRejected(document ? document.lastError : qsTr("No document"))
        refresh()
    }

    onDocumentChanged: refresh()
    onObjectIdChanged: refresh()
    onContourIndexChanged: refresh()
    onSurfaceChanged: geometryRevision++
    onNodesChanged: guides.requestPaint()
    Connections {
        target: root.document
        function onChanged() { root.refresh() }
    }
    Connections {
        target: root.surface
        function onViewChanged() { root.geometryRevision++; guides.requestPaint() }
    }

    Canvas {
        id: guides
        anchors.fill: parent
        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = Tk.Theme.color.accent
            context.lineWidth = Tk.Theme.size.border
            for (let i = 0; i < root.nodes.length; ++i) {
                const node = root.nodes[i]
                const anchor = root.mapLocal(node.x, node.y)
                for (const part of ["in", "out"]) {
                    const handle = root.handlePoint(i, part)
                    context.beginPath()
                    context.moveTo(anchor.x, anchor.y)
                    context.lineTo(handle.x, handle.y)
                    context.stroke()
                }
            }
        }
    }

    component PathHandle: Rectangle {
        required property int nodeIndex
        required property string part
        required property real localX
        required property real localY
        property bool dragging: false
        property point previewPoint: Qt.point(0, 0)
        property point pressPoint: Qt.point(0, 0)
        readonly property point basePoint: root.handlePoint(nodeIndex, part)
        readonly property point shownPoint: dragging ? previewPoint : basePoint
        objectName: "path" + part + "_" + nodeIndex
        width: Tk.Theme.size.controlSm
        height: width
        radius: part === "anchor" ? Tk.Theme.radius.sm : width / 2
        x: shownPoint.x - width / 2
        y: shownPoint.y - height / 2
        color: part === "anchor" ? Tk.Theme.color.accent : Tk.Theme.color.canvas
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.accent
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Path node %1 %2 handle").arg(nodeIndex + 1).arg(part)
        property string tooltip: Accessible.name

        MouseArea {
            id: pointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.CrossCursor
            onPressed: function(mouse) {
                parent.dragging = true
                parent.pressPoint = mapToItem(root, mouse.x, mouse.y)
                parent.previewPoint = parent.pressPoint
                forceActiveFocus()
            }
            onPositionChanged: function(mouse) {
                if (pressed) parent.previewPoint = mapToItem(root, mouse.x, mouse.y)
            }
            onReleased: function(mouse) {
                if (!parent.dragging) return
                const point = mapToItem(root, mouse.x, mouse.y)
                parent.dragging = false
                if (Math.hypot(point.x - parent.pressPoint.x, point.y - parent.pressPoint.y)
                    < Tk.Theme.size.border * 2) return
                root.finish(parent.nodeIndex, parent.part, point)
            }
            onCanceled: parent.dragging = false
            Keys.onEscapePressed: function(event) { parent.dragging = false; event.accepted = true }
        }
    }

    Repeater {
        model: root.nodes.length
        delegate: Item {
            id: row
            width: 0
            height: 0
            required property int index
            readonly property var node: root.nodes[index]
            PathHandle { nodeIndex: row.index; part: "in"; localX: row.node.inX; localY: row.node.inY }
            PathHandle { nodeIndex: row.index; part: "out"; localX: row.node.outX; localY: row.node.outY }
            PathHandle { nodeIndex: row.index; part: "anchor"; localX: row.node.x; localY: row.node.y }
        }
    }
}
