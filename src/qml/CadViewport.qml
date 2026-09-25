// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/cad.md): IDs and precise coordinates belong to the
// document. This viewport owns only gestures, selection and camera pose.
Item {
    id: root
    objectName: "cadViewport"
    clip: true
    focus: true
    activeFocusOnTab: enabled
    property Tk.CadDocument document: null
    property string mode: "select" // select | point | line | circle | arc
    property string selectedEntityId: ""
    property string pendingPointId: ""
    property int arcStage: 0
    property point arcStartWorld: Qt.point(0, 0)
    property string dragPointId: ""
    property point dragWorld: Qt.point(0, 0)
    property real zoom: 3 // screen pixels per millimeter
    property real panX: Tk.Theme.space.xl * 2
    property real panY: Tk.Theme.space.xl * 2
    property string tooltip: qsTr("CAD sketch viewport")
    readonly property real unitFactor: document && document.units === "in" ? 25.4 : 1
    readonly property real gridStep: document && document.units === "in" ? 0.5 : 10
    readonly property string statusText: document && document.lastError ? document.lastError
        : pendingPointId ? (mode === "line" ? qsTr("Choose line endpoint")
            : mode === "arc" ? (arcStage === 1 ? qsTr("Choose arc start")
                                              : qsTr("Choose arc end"))
            : qsTr("Choose circle radius"))
        : qsTr("Points %1 · Lines %2 · Circles %3 · Arcs %4 · Constraints %5")
            .arg(document ? document.points.length : 0)
            .arg(document ? document.segments.length : 0)
            .arg(document ? document.circles.length : 0)
            .arg(document ? document.arcs.length : 0)
            .arg(document ? document.constraints.length : 0)
    implicitWidth: Tk.Theme.size.fieldWidth * 3
    implicitHeight: Tk.Theme.size.fieldWidth * 2
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: statusText
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    function screenX(value) { return panX + Number(value) * unitFactor * zoom }
    function screenY(value) { return panY + Number(value) * unitFactor * zoom }
    function worldX(value) { return (value - panX) / (unitFactor * zoom) }
    function worldY(value) { return (value - panY) / (unitFactor * zoom) }
    function pointById(id) {
        const source = document ? document.points : []
        for (let i = 0; i < source.length; ++i)
            if (String(source[i].entityId) === String(id)) return source[i]
        return null
    }
    function shownPoint(id, x, y) {
        return id === dragPointId ? dragWorld : Qt.point(Number(x), Number(y))
    }
    function snapAt(position, excludeId) {
        if (!document) return null
        return document.snap(worldX(position.x), worldY(position.y),
            Tk.Theme.size.icon / (zoom * unitFactor), gridStep, excludeId || "")
    }
    function hitPoint(position) {
        const source = document ? document.points : []
        let best = Tk.Theme.size.icon
        let result = ""
        for (let i = 0; i < source.length; ++i) {
            const p = source[i]
            const d = Math.hypot(screenX(p.x) - position.x, screenY(p.y) - position.y)
            if (d <= best) { best = d; result = String(p.entityId) }
        }
        return result
    }
    function hitEntity(position) {
        const point = hitPoint(position)
        if (point) return point
        const lines = document ? document.segments : []
        let best = Tk.Theme.size.iconSm
        let result = ""
        for (let i = 0; i < lines.length; ++i) {
            const s = lines[i]
            const ax = screenX(s.x1), ay = screenY(s.y1)
            const bx = screenX(s.x2), by = screenY(s.y2)
            const dx = bx - ax, dy = by - ay
            const t = Math.max(0, Math.min(1,
                ((position.x - ax) * dx + (position.y - ay) * dy) / (dx * dx + dy * dy)))
            const d = Math.hypot(position.x - ax - t * dx, position.y - ay - t * dy)
            if (d <= best) { best = d; result = String(s.entityId) }
        }
        const circles = document ? document.circles : []
        for (let i = 0; i < circles.length; ++i) {
            const c = circles[i]
            const d = Math.abs(Math.hypot(position.x - screenX(c.x),
                                          position.y - screenY(c.y)) - c.radius * unitFactor * zoom)
            if (d <= best) { best = d; result = String(c.entityId) }
        }
        const arcs = document ? document.arcs : []
        for (let i = 0; i < arcs.length; ++i) {
            const arc = arcs[i]
            const dx = position.x - screenX(arc.x), dy = position.y - screenY(arc.y)
            const angle = ((Math.atan2(dy, dx) * 180 / Math.PI - arc.startDegrees) % 360 + 360) % 360
            const d = Math.abs(Math.hypot(dx, dy) - arc.radius * unitFactor * zoom)
            if (angle <= arc.sweepDegrees && d <= best) {
                best = d; result = String(arc.entityId)
            }
        }
        return result
    }
    function ensurePoint(snap) {
        return snap.pointId ? String(snap.pointId) : document.addPoint(snap.x, snap.y)
    }
    function handleTap(position) {
        if (!document) return
        forceActiveFocus()
        if (mode === "select") { selectedEntityId = hitEntity(position); return }
        const location = snapAt(position, "")
        if (!location) return
        if (mode === "point") {
            selectedEntityId = ensurePoint(location)
        } else if (mode === "line") {
            const id = ensurePoint(location)
            if (pendingPointId) {
                selectedEntityId = document.addSegment(pendingPointId, id)
                pendingPointId = ""
            } else pendingPointId = id
        } else if (mode === "circle") {
            if (pendingPointId) {
                const center = pointById(pendingPointId)
                if (center) selectedEntityId = document.addCircle(pendingPointId,
                    Math.hypot(location.x - center.x, location.y - center.y))
                pendingPointId = ""
            } else pendingPointId = ensurePoint(location)
        } else if (mode === "arc") {
            if (!pendingPointId) {
                pendingPointId = ensurePoint(location)
                arcStage = 1
            } else if (arcStage === 1) {
                arcStartWorld = Qt.point(location.x, location.y)
                arcStage = 2
            } else {
                const center = pointById(pendingPointId)
                if (center) {
                    const start = Math.atan2(arcStartWorld.y - center.y,
                                             arcStartWorld.x - center.x) * 180 / Math.PI
                    const end = Math.atan2(location.y - center.y,
                                           location.x - center.x) * 180 / Math.PI
                    const sweep = ((end - start) % 360 + 360) % 360
                    selectedEntityId = document.addArc(pendingPointId,
                        Math.hypot(arcStartWorld.x - center.x, arcStartWorld.y - center.y),
                        start, sweep)
                }
                pendingPointId = ""
                arcStage = 0
            }
        }
    }
    function zoomAt(value, position) {
        const oldX = worldX(position.x), oldY = worldY(position.y)
        zoom = Math.max(0.25, Math.min(15, value))
        panX = position.x - oldX * unitFactor * zoom
        panY = position.y - oldY * unitFactor * zoom
    }

    onModeChanged: { pendingPointId = ""; arcStage = 0 }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            pendingPointId = ""; arcStage = 0; dragPointId = ""; event.accepted = true
        } else if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
                   && selectedEntityId && document) {
            if (document.removeEntity(selectedEntityId)) selectedEntityId = ""
            event.accepted = true
        }
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Canvas {
        id: drawing
        objectName: "cadViewportDrawing"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const step = root.gridStep * root.unitFactor * root.zoom
            if (step >= Tk.Theme.space.md) {
                ctx.beginPath()
                ctx.strokeStyle = Tk.Theme.color.border
                ctx.lineWidth = Tk.Theme.size.border
                for (let x = ((root.panX % step) + step) % step; x < width; x += step) {
                    ctx.moveTo(x, 0); ctx.lineTo(x, height)
                }
                for (let y = ((root.panY % step) + step) % step; y < height; y += step) {
                    ctx.moveTo(0, y); ctx.lineTo(width, y)
                }
                ctx.stroke()
            }
            const circles = root.document ? root.document.circles : []
            for (let i = 0; i < circles.length; ++i) {
                const c = circles[i]
                const p = root.shownPoint(String(c.centerPointId), c.x, c.y)
                ctx.beginPath()
                ctx.arc(root.screenX(p.x), root.screenY(p.y), c.radius * root.unitFactor * root.zoom,
                        0, Math.PI * 2)
                ctx.strokeStyle = String(c.entityId) === root.selectedEntityId
                    ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                ctx.lineWidth = Tk.Theme.size.border * 2
                ctx.stroke()
            }
            const arcs = root.document ? root.document.arcs : []
            for (let i = 0; i < arcs.length; ++i) {
                const arc = arcs[i]
                const p = root.shownPoint(String(arc.centerPointId), arc.x, arc.y)
                ctx.beginPath()
                ctx.arc(root.screenX(p.x), root.screenY(p.y), arc.radius * root.unitFactor * root.zoom,
                    arc.startDegrees * Math.PI / 180,
                    (arc.startDegrees + arc.sweepDegrees) * Math.PI / 180)
                ctx.strokeStyle = String(arc.entityId) === root.selectedEntityId
                    ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                ctx.lineWidth = Tk.Theme.size.border * 2
                ctx.stroke()
            }
            const lines = root.document ? root.document.segments : []
            for (let i = 0; i < lines.length; ++i) {
                const s = lines[i]
                const a = root.shownPoint(String(s.startPointId), s.x1, s.y1)
                const b = root.shownPoint(String(s.endPointId), s.x2, s.y2)
                ctx.beginPath()
                ctx.moveTo(root.screenX(a.x), root.screenY(a.y))
                ctx.lineTo(root.screenX(b.x), root.screenY(b.y))
                ctx.strokeStyle = String(s.entityId) === root.selectedEntityId
                    ? Tk.Theme.color.accent : Tk.Theme.color.text
                ctx.lineWidth = Tk.Theme.size.border * 2
                ctx.stroke()
            }
            const points = root.document ? root.document.points : []
            for (let i = 0; i < points.length; ++i) {
                const p = points[i]
                const shown = root.shownPoint(String(p.entityId), p.x, p.y)
                ctx.beginPath()
                ctx.arc(root.screenX(shown.x), root.screenY(shown.y),
                        Tk.Theme.size.iconSm / 3, 0, Math.PI * 2)
                ctx.fillStyle = String(p.entityId) === root.selectedEntityId
                    ? Tk.Theme.color.accent : Tk.Theme.color.focus
                ctx.fill()
            }
        }
        Connections {
            target: root.document
            function onChanged() { drawing.requestPaint() }
        }
        Connections {
            target: root
            function onDocumentChanged() { drawing.requestPaint() }
            function onPanXChanged() { drawing.requestPaint() }
            function onPanYChanged() { drawing.requestPaint() }
            function onZoomChanged() { drawing.requestPaint() }
            function onSelectedEntityIdChanged() { drawing.requestPaint() }
            function onDragWorldChanged() { drawing.requestPaint() }
            function onDragPointIdChanged() { drawing.requestPaint() }
        }
    }

    Repeater {
        model: root.document ? root.document.segments : []
        delegate: Item {
            id: dimension
            required property var modelData
            readonly property string constraintId: modelData.distanceConstraintId
                || modelData.angleConstraintId || ""
            readonly property bool editingDimension: root.mode === "select"
                && root.selectedEntityId === String(modelData.entityId) && constraintId.length > 0
            visible: root.zoom * root.unitFactor > 1
            x: root.screenX((modelData.x1 + modelData.x2) / 2) + Tk.Theme.space.xs
            y: root.screenY((modelData.y1 + modelData.y2) / 2) + Tk.Theme.space.xs
            width: editingDimension ? editor.implicitWidth : readout.implicitWidth
            height: editingDimension ? editor.implicitHeight : readout.implicitHeight
            Tk.Label {
                id: readout
                visible: !dimension.editingDimension
                text: Number(dimension.modelData.length).toFixed(2) + " " + root.document.units
                color: Tk.Theme.color.accent
                font.pixelSize: Tk.Theme.font.caption
            }
            Tk.NumberField {
                id: editor
                objectName: "cadDimensionEditor_" + dimension.modelData.entityId
                visible: dimension.editingDimension
                small: true
                stepButtons: false
                scrub: false
                decimals: 3
                from: dimension.modelData.distanceConstraintId ? 0 : -180
                to: dimension.modelData.distanceConstraintId ? 1000000 : 180
                value: dimension.modelData.distanceConstraintId
                    ? dimension.modelData.distanceValue : dimension.modelData.angleValue
                suffix: dimension.modelData.distanceConstraintId ? root.document.units : "°"
                tooltip: qsTr("Edit constrained dimension")
                // A refused edit changes nothing: the field keeps showing the
                // bound model value (NumberField never writes `value`).
                onValueModified: function(next) { root.document.setDimension(dimension.constraintId, next) }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint) { root.handleTap(eventPoint.position) }
    }
    DragHandler {
        id: pointDrag
        target: null
        enabled: root.mode === "select"
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active) {
                root.dragPointId = root.hitPoint(centroid.pressPosition)
                const p = root.pointById(root.dragPointId)
                if (p) root.dragWorld = Qt.point(p.x, p.y)
            } else if (root.dragPointId && root.document) {
                const position = Qt.point(root.screenX(root.dragWorld.x),
                                          root.screenY(root.dragWorld.y))
                const targetPoint = root.snapAt(position, root.dragPointId)
                if (targetPoint) root.document.movePoint(root.dragPointId,
                                                         targetPoint.x, targetPoint.y)
                root.dragPointId = ""
            }
        }
        onTranslationChanged: {
            if (!active || !root.dragPointId) return
            const p = root.pointById(root.dragPointId)
            if (p) root.dragWorld = Qt.point(p.x + translation.x / (root.zoom * root.unitFactor),
                                             p.y + translation.y / (root.zoom * root.unitFactor))
        }
    }
    DragHandler {
        id: panDrag
        target: null
        acceptedButtons: Qt.MiddleButton
        property real startX: 0
        property real startY: 0
        onActiveChanged: if (active) { startX = root.panX; startY = root.panY }
        onTranslationChanged: if (active) {
            root.panX = startX + translation.x
            root.panY = startY + translation.y
        }
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            const factor = event.angleDelta.y > 0 ? 1.15 : 1 / 1.15
            root.zoomAt(root.zoom * factor, event.point.position)
            event.accepted = true
        }
    }
    Tk.ToolTip { text: root.tooltip; visible: hover.hovered }
    HoverHandler { id: hover }
}
