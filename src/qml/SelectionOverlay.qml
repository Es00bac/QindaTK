// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/canvas.md#overlays, WP-041): the canvas host supplies
// document-space selectionCorners, or document-space selectionBounds as a
// fallback, and a CanvasView surface. This item owns only view-space
// presentation and gesture reporting; a geometry adapter owns validation,
// snapping, preview and commit.
Item {
    id: root
    objectName: "selectionOverlay"

    property Item surface
    property rect selectionBounds: Qt.rect(0, 0, 0, 0)
    // Optional ordered document-space corners preserve rotated group bounds.
    property var selectionCorners: []
    property bool handlesEnabled: true
    property string accessibleName: qsTr("Selection transform handles")
    property real handleSize: Tk.Theme.size.controlSm
    property real rotationHandleDistance: Tk.Theme.size.controlLg
    property real outlineWidth: Tk.Theme.size.border
    property int geometryRevision: 0
    readonly property var viewCorners: mappedCorners()
    readonly property rect viewBounds: mappedBounds()

    signal transformRequested(string handle, point startDocumentPoint,
                              point currentDocumentPoint, int modifiers)
    signal transformFinished(string handle)
    signal transformCanceled(string handle)
    signal transformNudgeRequested(string handle, real viewDx, real viewDy, int modifiers)
    signal rotateStepRequested(real degrees, int modifiers)

    Accessible.role: Accessible.Grouping
    Accessible.name: accessibleName
    Accessible.description: qsTr("Selection bounds, resize handles and rotation handle")

    function documentCorners() {
        if (selectionCorners && selectionCorners.length === 4)
            return selectionCorners
        const r = selectionBounds
        return [Qt.point(r.x, r.y), Qt.point(r.x + r.width, r.y),
                Qt.point(r.x + r.width, r.y + r.height), Qt.point(r.x, r.y + r.height)]
    }

    function mappedCorners() {
        const revision = geometryRevision
        if (!surface || (selectionCorners.length !== 4
                         && (selectionBounds.width <= 0 || selectionBounds.height <= 0)))
            return []
        const corners = documentCorners()
        return corners.map(point => surface.mapFromDocument(point))
    }

    function mappedBounds() {
        const points = viewCorners
        if (points.length !== 4)
            return Qt.rect(0, 0, 0, 0)
        const xs = points.map(point => point.x)
        const ys = points.map(point => point.y)
        const left = Math.min.apply(null, xs)
        const top = Math.min.apply(null, ys)
        return Qt.rect(left, top, Math.max.apply(null, xs) - left,
                       Math.max.apply(null, ys) - top)
    }

    function handlePoint(index) {
        const points = viewCorners
        if (points.length !== 4)
            return Qt.point(0, 0)
        if (index % 2 === 0)
            return points[index / 2]
        const before = points[(index - 1) / 2]
        const after = points[((index - 1) / 2 + 1) % 4]
        return Qt.point((before.x + after.x) / 2, (before.y + after.y) / 2)
    }

    function rotationPoint() {
        const points = viewCorners
        if (points.length !== 4) return Qt.point(0, 0)
        const top = Qt.point((points[0].x + points[1].x) / 2,
                             (points[0].y + points[1].y) / 2)
        const center = Qt.point((points[0].x + points[1].x + points[2].x + points[3].x) / 4,
                                (points[0].y + points[1].y + points[2].y + points[3].y) / 4)
        const dx = top.x - center.x
        const dy = top.y - center.y
        const length = Math.hypot(dx, dy)
        return Qt.point(top.x + (length > 0 ? dx / length : 0) * rotationHandleDistance,
                        top.y + (length > 0 ? dy / length : -1) * rotationHandleDistance)
    }

    Connections {
        target: root.surface
        function onViewChanged() { root.geometryRevision += 1 }
    }

    Canvas {
        id: frame
        objectName: "selectionFrame"
        anchors.fill: parent
        visible: root.viewCorners.length === 4
        onPaint: {
            const points = root.viewCorners
            if (points.length !== 4) return
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = Tk.Theme.color.accent
            context.lineWidth = root.outlineWidth
            context.beginPath()
            context.moveTo(points[0].x, points[0].y)
            for (let i = 1; i < points.length; ++i)
                context.lineTo(points[i].x, points[i].y)
            context.closePath()
            context.stroke()
            if (root.handlesEnabled) {
                const top = root.handlePoint(1)
                const rotation = root.rotationPoint()
                context.beginPath()
                context.moveTo(top.x, top.y)
                context.lineTo(rotation.x, rotation.y)
                context.stroke()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    onGeometryRevisionChanged: frame.requestPaint()
    onSelectionBoundsChanged: frame.requestPaint()
    onSelectionCornersChanged: frame.requestPaint()
    onOutlineWidthChanged: frame.requestPaint()
    onRotationHandleDistanceChanged: frame.requestPaint()
    onHandlesEnabledChanged: frame.requestPaint()

    Repeater {
        model: root.handlesEnabled && root.viewCorners.length === 4 ? 8 : 0
        delegate: Item {
            id: handle
            objectName: ["handleTopLeft", "handleTop", "handleTopRight", "handleRight",
                         "handleBottomRight", "handleBottom", "handleBottomLeft",
                         "handleLeft"][index]
            property string handleName: ["topLeft", "top", "topRight", "right",
                                         "bottomRight", "bottom", "bottomLeft",
                                         "left"][index]
            property point center: root.handlePoint(index)
            x: center.x - width / 2
            y: center.y - height / 2
            width: root.handleSize
            height: root.handleSize
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Resize selection %1").arg(handle.handleName)
            Accessible.description: root.accessibleName
            Accessible.onPressAction: root.transformNudgeRequested(handle.handleName, 0, 0, 0)
            HoverHandler { id: resizeHover }
            Tk.ToolTip {
                text: qsTr("Resize selection %1").arg(handle.handleName)
                visible: resizeHover.hovered
            }

            Rectangle {
                anchors.fill: parent
                radius: Tk.Theme.radius.sm
                color: Tk.Theme.color.canvas
                border.color: Tk.Theme.color.accent
                border.width: Tk.Theme.size.border
            }

            Keys.onPressed: function(event) {
                let dx = 0
                let dy = 0
                if (event.key === Qt.Key_Left) dx = -Tk.Theme.size.controlSm
                else if (event.key === Qt.Key_Right) dx = Tk.Theme.size.controlSm
                else if (event.key === Qt.Key_Up) dy = -Tk.Theme.size.controlSm
                else if (event.key === Qt.Key_Down) dy = Tk.Theme.size.controlSm
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                         || event.key === Qt.Key_Space) {
                    root.transformNudgeRequested(handle.handleName, 0, 0, event.modifiers)
                    event.accepted = true
                    return
                } else return
                root.transformNudgeRequested(handle.handleName, dx, dy, event.modifiers)
                event.accepted = true
            }

            MouseArea {
                id: pointer
                anchors.fill: parent
                preventStealing: true
                property point startDocumentPoint
                onPressed: function(mouse) {
                    handle.forceActiveFocus()
                    startDocumentPoint = root.surface.mapToDocument(
                                mapToItem(root.surface, mouse.x, mouse.y))
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const viewPoint = mapToItem(root.surface, mouse.x, mouse.y)
                    root.transformRequested(handle.handleName, startDocumentPoint,
                                            root.surface.mapToDocument(viewPoint), mouse.modifiers)
                }
                onReleased: root.transformFinished(handle.handleName)
                onCanceled: root.transformCanceled(handle.handleName)
            }
        }
    }

    Item {
        id: rotationHandle
        objectName: "handleRotate"
        visible: root.handlesEnabled && root.viewCorners.length === 4
        readonly property point center: root.rotationPoint()
        x: center.x - width / 2
        y: center.y - height / 2
        width: root.handleSize
        height: root.handleSize
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Rotate selection")
        Accessible.description: root.accessibleName
        Accessible.onIncreaseAction: root.rotateStepRequested(15, 0)
        Accessible.onDecreaseAction: root.rotateStepRequested(-15, 0)
        HoverHandler { id: rotationHover }
        Tk.ToolTip { text: qsTr("Rotate selection"); visible: rotationHover.hovered }
        Rectangle {
            anchors.fill: parent
            radius: Tk.Theme.radius.full
            color: Tk.Theme.color.canvas
            border.color: Tk.Theme.color.accent
            border.width: Tk.Theme.size.border
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Down)
                root.rotateStepRequested(-15, event.modifiers)
            else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up)
                root.rotateStepRequested(15, event.modifiers)
            else return
            event.accepted = true
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            property point startDocumentPoint
            onPressed: function(mouse) {
                rotationHandle.forceActiveFocus()
                startDocumentPoint = root.surface.mapToDocument(
                            mapToItem(root.surface, mouse.x, mouse.y))
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const viewPoint = mapToItem(root.surface, mouse.x, mouse.y)
                root.transformRequested("rotate", startDocumentPoint,
                                        root.surface.mapToDocument(viewPoint), mouse.modifiers)
            }
            onReleased: root.transformFinished("rotate")
            onCanceled: root.transformCanceled("rotate")
        }
    }
}
