// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: CurveEditor presents host-owned stable-ID knots and sampled
// normalized points. It emits selection and edit lifecycle requests; the host
// owns canonical coordinates, interpolation, constraints, transactions, and
// undo. A host may replace `knots` during a preview; the editor keeps pointer
// ownership at the root so delegate replacement cannot lose an active drag.
Item {
    id: root
    objectName: "curveEditor"
    property var knots: []       // [{id, x, y, label?}], normalized x/y in 0..1
    property var samples: []     // [{x, y}], already evaluated by the host
    property string tooltip: qsTr("Curve editor")
    property string accessibleName: qsTr("Curve editor")
    property string selectedKnotId: ""
    property real plotPadding: Tk.Theme.size.controlSm / 2
    readonly property rect plotRect: Qt.rect(plotPadding, plotPadding,
                                              Math.max(0, width - 2 * plotPadding),
                                              Math.max(0, height - 2 * plotPadding))
    signal knotSelected(string id)
    signal movePreview(string id, real x, real y)
    signal moveFinished(string id)
    signal moveCancelled(string id)
    property string _activeId: ""
    property string _pressedId: ""
    property bool _suppressPress: false
    property real _dragOffsetX: 0
    property real _dragOffsetY: 0

    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: root.accessibleName
    Accessible.description: root.tooltip
    activeFocusOnTab: true
    focus: true
    Keys.onEscapePressed: {
        if (root._activeId.length || root._pressedId.length) {
            if (root._activeId.length) root.moveCancelled(root._activeId)
            root._activeId = ""
            root._suppressPress = root._pressedId.length > 0
            event.accepted = true
        }
    }

    function normalizedAt(px, py) {
        const r = root.plotRect
        return Qt.point(r.width > 0 ? Math.max(0, Math.min(1, (px - r.x) / r.width)) : 0,
                         r.height > 0 ? Math.max(0, Math.min(1, 1 - (py - r.y) / r.height)) : 0)
    }
    function pixelAt(x, y) {
        const r = root.plotRect
        return Qt.point(r.x + x * r.width, r.y + (1 - y) * r.height)
    }
    function selectKnot(id) {
        if (!root._knot(id)) return false
        root.selectedKnotId = id
        root.knotSelected(id)
        return true
    }
    function requestMove(id, x, y) {
        if (!root._knot(id) || !isFinite(x) || !isFinite(y)) return false
        root._activeId = id
        root.movePreview(id, Math.max(0, Math.min(1, x)), Math.max(0, Math.min(1, y)))
        return true
    }
    function finishMove(id) {
        if (root._activeId !== id) return false
        root.moveFinished(id)
        root._activeId = ""
        return true
    }
    function nudge(id, x, y) {
        if (!root.requestMove(id, x, y)) return false
        return root.finishMove(id)
    }
    function cancelMove(id) {
        if (root._activeId !== id) return false
        root.moveCancelled(id)
        root._activeId = ""
        return true
    }
    function _knot(id) {
        for (let i = 0; i < root.knots.length; ++i)
            if (String(root.knots[i].id) === String(id)) return root.knots[i]
        return null
    }
    function hitKnot(px, py) {
        const radius = Tk.Theme.size.controlSm / 2
        for (let i = root.knots.length - 1; i >= 0; --i) {
            const point = root.pixelAt(Number(root.knots[i].x), Number(root.knots[i].y))
            if (Math.abs(px - point.x) <= radius && Math.abs(py - point.y) <= radius)
                return { id: String(root.knots[i].id), index: i, point: point }
        }
        return null
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.panel
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.controlBorder
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    Canvas {
        id: graph
        objectName: "curvePlot"
        x: root.plotRect.x; y: root.plotRect.y
        width: root.plotRect.width; height: root.plotRect.height
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = Tk.Theme.color.divider
            ctx.lineWidth = Tk.Theme.size.border
            ctx.beginPath()
            ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.stroke()
            const points = root.samples || []
            if (points.length) {
                ctx.beginPath()
                for (let i = 0; i < points.length; ++i) {
                    const px = Math.max(0, Math.min(1, Number(points[i].x))) * width
                    const py = (1 - Math.max(0, Math.min(1, Number(points[i].y)))) * height
                    if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                }
                ctx.strokeStyle = Tk.Theme.color.accent
                ctx.lineWidth = Tk.Theme.size.border + Tk.Theme.size.border
                ctx.stroke()
            }
        }
        Connections { target: root; function onSamplesChanged() { graph.requestPaint() } }
        Connections { target: root; function onWidthChanged() { graph.requestPaint() } }
        Connections { target: root; function onHeightChanged() { graph.requestPaint() } }
    }
    Repeater {
        id: knotRepeater
        model: root.knots || []
        delegate: Item {
            id: knot
            required property var modelData
            readonly property string knotId: String(modelData.id)
            readonly property point center: root.pixelAt(Number(modelData.x), Number(modelData.y))
            x: center.x - Tk.Theme.size.controlSm / 2
            y: center.y - Tk.Theme.size.controlSm / 2
            width: Tk.Theme.size.controlSm; height: Tk.Theme.size.controlSm
            objectName: "curveKnot_" + knotId
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.name: modelData.label || qsTr("Curve knot")
            Accessible.description: qsTr("x %1, y %2").arg(Number(modelData.x).toFixed(3)).arg(Number(modelData.y).toFixed(3))
            Accessible.onIncreaseAction: root.nudge(knotId, Number(modelData.x), Number(modelData.y) + 0.01)
            Accessible.onDecreaseAction: root.nudge(knotId, Number(modelData.x), Number(modelData.y) - 0.01)
            Keys.onLeftPressed: { root.nudge(knotId, Number(modelData.x) - 0.01, Number(modelData.y)); event.accepted = true }
            Keys.onRightPressed: { root.nudge(knotId, Number(modelData.x) + 0.01, Number(modelData.y)); event.accepted = true }
            Keys.onUpPressed: { root.nudge(knotId, Number(modelData.x), Number(modelData.y) + 0.01); event.accepted = true }
            Keys.onDownPressed: { root.nudge(knotId, Number(modelData.x), Number(modelData.y) - 0.01); event.accepted = true }
            Keys.onReturnPressed: root.finishMove(knotId)
            Keys.onEnterPressed: root.finishMove(knotId)
            Rectangle {
                anchors.centerIn: parent
                width: Tk.Theme.size.controlSm / 2; height: width
                radius: Tk.Theme.radius.full
                color: Tk.Theme.color.accent
                border.color: root.selectedKnotId === knot.knotId || knot.activeFocus
                               ? Tk.Theme.color.text : Tk.Theme.color.controlActiveBorder
                border.width: Tk.Theme.size.border
            }
            Tk.ToolTip { visible: knotHover.hovered && root.tooltip.length > 0; text: root.tooltip }
            HoverHandler { id: knotHover }
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: function(mouse) {
            root._suppressPress = false
            const hit = root.hitKnot(mouse.x, mouse.y)
            if (!hit) {
                mouse.accepted = false
                return
            }
            root._pressedId = hit.id
            root._dragOffsetX = mouse.x - hit.point.x
            root._dragOffsetY = mouse.y - hit.point.y
            root.selectKnot(hit.id)
            const item = knotRepeater.itemAt(hit.index)
            if (item) item.forceActiveFocus()
            mouse.accepted = true
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root._pressedId || root._suppressPress) return
            const point = root.normalizedAt(mouse.x - root._dragOffsetX,
                                            mouse.y - root._dragOffsetY)
            root.requestMove(root._pressedId, point.x, point.y)
        }
        onReleased: {
            if (root._pressedId && !root._suppressPress) root.finishMove(root._pressedId)
            root._pressedId = ""
            root._suppressPress = false
        }
        onCanceled: {
            if (root._pressedId && !root._suppressPress) root.cancelMove(root._pressedId)
            root._pressedId = ""
            root._suppressPress = false
        }
    }
}
