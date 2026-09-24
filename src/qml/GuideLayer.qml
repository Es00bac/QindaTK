// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (WP-041): the host supplies guideData records with stable
// `id`, `orientation` (horizontal/vertical), and document-space `position`.
// This layer paints/maps guides and reports requests; guide persistence,
// snapping, and authoritative movement remain with the host model.
Item {
    id: root
    objectName: "guideLayer"

    property Item surface
    property var guideData: []
    property color guideColor: Tk.Theme.color.accent
    property real lineWidth: Tk.Theme.size.border
    property real hitThickness: Tk.Theme.size.seamTarget
    property real keyboardStep: 1
    property int geometryRevision: 0
    property string tooltip: qsTr("Document guides")

    signal guideAddRequested(string orientation, real documentPosition)
    signal guideMoveRequested(string guideId, real documentPosition)
    signal guideMoveFinished(string guideId, real documentPosition)
    signal guideMoveCanceled(string guideId, real originalPosition)
    signal guideDeleteRequested(string guideId)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: qsTr("Document guides in view coordinates")

    function guidePosition(guide) { return Number(guide.position) }

    function lineGeometry(guide) {
        const revision = geometryRevision
        if (!surface || !guide || (guide.orientation !== "horizontal"
                                   && guide.orientation !== "vertical"))
            return null
        const position = guidePosition(guide)
        if (!Number.isFinite(position)) return null
        const vertical = guide.orientation === "vertical"
        const anchor = surface.mapFromDocument(vertical ? Qt.point(position, 0)
                                                        : Qt.point(0, position))
        const next = surface.mapFromDocument(vertical ? Qt.point(position, 1)
                                                      : Qt.point(1, position))
        let dx = next.x - anchor.x
        let dy = next.y - anchor.y
        const magnitude = Math.hypot(dx, dy)
        if (!Number.isFinite(magnitude) || magnitude <= 0) return null
        dx /= magnitude
        dy /= magnitude
        const intersections = []
        if (Math.abs(dx) > 1e-12) {
            intersections.push({ x: 0, y: anchor.y - anchor.x * dy / dx })
            intersections.push({ x: width, y: anchor.y + (width - anchor.x) * dy / dx })
        }
        if (Math.abs(dy) > 1e-12) {
            intersections.push({ x: anchor.x - anchor.y * dx / dy, y: 0 })
            intersections.push({ x: anchor.x + (height - anchor.y) * dx / dy, y: height })
        }
        const visible = intersections.filter(point => point.x >= -0.001
            && point.x <= width + 0.001 && point.y >= -0.001 && point.y <= height + 0.001)
        if (visible.length < 2) return null
        visible.sort((a, b) => (a.x - b.x) * dx + (a.y - b.y) * dy)
        const first = visible[0]
        const last = visible[visible.length - 1]
        const length = Math.hypot(last.x - first.x, last.y - first.y)
        return { x1: first.x, y1: first.y, x2: last.x, y2: last.y,
                 x: (first.x + last.x) / 2, y: (first.y + last.y) / 2,
                 length: length, angle: Math.atan2(last.y - first.y, last.x - first.x) * 180 / Math.PI }
    }

    function requestAdd(orientation, localX, localY) {
        if (!surface || (orientation !== "horizontal" && orientation !== "vertical"))
            return false
        const surfacePoint = mapToItem(surface, localX, localY)
        const point = surface.mapToDocument(surfacePoint)
        guideAddRequested(orientation, orientation === "vertical" ? point.x : point.y)
        return true
    }

    function moveBy(guide, delta) {
        if (!guide || !guide.id) return
        guideMoveRequested(String(guide.id), guidePosition(guide) + delta)
    }

    Connections {
        target: root.surface
        function onViewChanged() { root.geometryRevision += 1; guides.requestPaint() }
    }
    onGuideDataChanged: guides.requestPaint()
    onGeometryRevisionChanged: guides.requestPaint()
    onGuideColorChanged: guides.requestPaint()
    onLineWidthChanged: guides.requestPaint()

    Canvas {
        id: guides
        objectName: "guideLines"
        anchors.fill: parent
        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = root.guideColor
            context.lineWidth = root.lineWidth
            context.beginPath()
            for (let i = 0; i < root.guideData.length; ++i) {
                const line = root.lineGeometry(root.guideData[i])
                if (!line) continue
                context.moveTo(line.x1, line.y1)
                context.lineTo(line.x2, line.y2)
            }
            context.stroke()
        }
    }

    Repeater {
        model: root.guideData
        delegate: Item {
            id: action
            required property var modelData
            required property int index
            property string guideId: String(modelData.id || "")
            property string orientation: String(modelData.orientation || "")
            property real originalPosition: root.guidePosition(modelData)
            property var geometry: root.lineGeometry(modelData)
            objectName: "guideAction_" + guideId
            visible: geometry !== null
            x: geometry ? geometry.x - width / 2 : 0
            y: geometry ? geometry.y - height / 2 : 0
            width: geometry ? geometry.length : 0
            height: root.hitThickness
            rotation: geometry ? geometry.angle : 0
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.name: String(modelData.label || qsTr("%1 guide").arg(orientation))
            Accessible.description: qsTr("Position %1 document units. Use arrow keys to move; Delete to remove.")
                .arg(originalPosition)
            Accessible.onPressAction: action.forceActiveFocus()
            Accessible.onIncreaseAction: root.moveBy(modelData, root.keyboardStep)
            Accessible.onDecreaseAction: root.moveBy(modelData, -root.keyboardStep)

            Keys.onPressed: function(event) {
                let delta = 0
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
                    delta = -root.keyboardStep
                else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
                    delta = root.keyboardStep
                else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    root.guideDeleteRequested(action.guideId)
                    event.accepted = true
                    return
                } else return
                root.moveBy(modelData, delta)
                event.accepted = true
            }

            Rectangle {
                anchors.fill: parent
                color: Tk.Theme.color.hover
                opacity: mouse.containsMouse ? Tk.Theme.opacity.hidden : 0
            }
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true
                acceptedButtons: Qt.LeftButton
                property real startPosition: action.originalPosition
                onPressed: function(event) {
                    startPosition = action.originalPosition
                    action.forceActiveFocus()
                    event.accepted = true
                }
                onPositionChanged: function(event) {
                    if (!pressed || !root.surface) return
                    const viewPoint = mapToItem(root.surface, event.x, event.y)
                    const documentPoint = root.surface.mapToDocument(viewPoint)
                    root.guideMoveRequested(action.guideId,
                        action.orientation === "vertical" ? documentPoint.x : documentPoint.y)
                }
                onReleased: function(event) {
                    if (!root.surface) return
                    const viewPoint = mapToItem(root.surface, event.x, event.y)
                    const documentPoint = root.surface.mapToDocument(viewPoint)
                    root.guideMoveFinished(action.guideId,
                        action.orientation === "vertical" ? documentPoint.x : documentPoint.y)
                }
                onCanceled: root.guideMoveCanceled(action.guideId, startPosition)
            }
        }
    }
}
