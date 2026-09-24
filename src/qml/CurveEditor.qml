// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: CurveEditor presents host-owned stable-ID knots and sampled
// normalized points. It emits selection and edit lifecycle requests; the host
// owns canonical coordinates, interpolation, constraints, transactions, and
// undo. Pointer ownership stays on the root through knot-array replacement.
Item {
    id: root
    objectName: "curveEditor"
    property var knots: [] // {id, x, y, label?, inHandle?, outHandle?}
    property var samples: [] // host-evaluated normalized {x, y} points
    property string tooltip: qsTr("Curve editor")
    property string accessibleName: qsTr("Curve editor")
    property string selectedKnotId: ""
    property real plotPadding: Tk.Theme.size.controlSm / 2
    // The identity diagonal suits transfer curves; a time-axis host such as
    // AutomationLane turns it off (WP-049).
    property bool referenceLine: true
    readonly property rect plotRect: Qt.rect(plotPadding, plotPadding,
                                              Math.max(0, width - 2 * plotPadding),
                                              Math.max(0, height - 2 * plotPadding))
    readonly property var tangentHandles: {
        const result = []
        const source = root.knots || []
        for (let i = 0; i < source.length; ++i) {
            const knot = source[i]
            for (const side of ["in", "out"]) {
                const handle = knot[side + "Handle"]
                if (handle && isFinite(Number(handle.x)) && isFinite(Number(handle.y)))
                    result.push({ id: String(knot.id), label: knot.label || qsTr("Curve knot"),
                                  side: side, x: Number(handle.x), y: Number(handle.y),
                                  knotX: Number(knot.x), knotY: Number(knot.y) })
            }
        }
        return result
    }
    signal knotSelected(string id)
    signal movePreview(string id, real x, real y)
    signal moveFinished(string id)
    signal moveCancelled(string id)
    signal tangentPreviewRequested(string knotId, string side, real x, real y)
    signal tangentEditFinished(string knotId, string side, real x, real y)
    signal tangentEditCanceled(string knotId, string side, real x, real y)
    property string _activeId: ""
    property string _pressedId: ""
    property string _pressedKind: ""
    property string _pressedSide: ""
    property bool _suppressPress: false
    property bool _hasPreview: false
    property real _dragOffsetX: 0
    property real _dragOffsetY: 0
    property real _originX: 0
    property real _originY: 0
    property real _lastX: 0
    property real _lastY: 0

    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: root.accessibleName
    Accessible.description: root.tooltip
    activeFocusOnTab: true
    focus: true
    Keys.onEscapePressed: function(event) { cancelPress(event) }

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
    function requestTangent(id, side, x, y) {
        if (!id || (side !== "in" && side !== "out") || !isFinite(x) || !isFinite(y)) return false
        const px = Math.max(0, Math.min(1, x))
        const py = Math.max(0, Math.min(1, y))
        root._activeId = id
        root._lastX = px
        root._lastY = py
        root._hasPreview = true
        root.tangentPreviewRequested(id, side, px, py)
        return true
    }
    function finishTangent(id, side, x, y) {
        if (root._activeId !== id || !root._hasPreview) return false
        root.tangentEditFinished(id, side, x, y)
        root._activeId = ""
        root._hasPreview = false
        return true
    }
    function cancelTangent(id, side, x, y) {
        if (root._activeId !== id || !root._hasPreview) return false
        root.tangentEditCanceled(id, side, x, y)
        root._activeId = ""
        root._hasPreview = false
        return true
    }
    function cancelPress(event) {
        if (root._pressedKind === "tangent" && root._hasPreview)
            root.cancelTangent(root._pressedId, root._pressedSide, root._originX, root._originY)
        else if (root._pressedKind === "knot" && root._activeId.length)
            root.cancelMove(root._pressedId)
        if (root._pressedKind.length) {
            root._suppressPress = true
            root._pressedId = ""
            root._pressedKind = ""
            root._pressedSide = ""
            root._hasPreview = false
            event.accepted = true
        }
    }
    function _knot(id) {
        for (let i = 0; i < (root.knots || []).length; ++i)
            if (String(root.knots[i].id) === String(id)) return root.knots[i]
        return null
    }
    function hitTangent(px, py) {
        const radius = Tk.Theme.size.controlSm / 2
        for (let i = root.tangentHandles.length - 1; i >= 0; --i) {
            const handle = root.tangentHandles[i]
            const point = root.pixelAt(handle.x, handle.y)
            if (Math.abs(px - point.x) <= radius && Math.abs(py - point.y) <= radius)
                return { id: handle.id, side: handle.side, point: point,
                         x: handle.x, y: handle.y, index: i }
        }
        return null
    }
    function hitKnot(px, py) {
        const radius = Tk.Theme.size.controlSm / 2
        for (let i = (root.knots || []).length - 1; i >= 0; --i) {
            const point = root.pixelAt(Number(root.knots[i].x), Number(root.knots[i].y))
            if (Math.abs(px - point.x) <= radius && Math.abs(py - point.y) <= radius)
                return { id: String(root.knots[i].id), index: i, point: point }
        }
        return null
    }
    function keyboardTangent(id, side, dx, dy) {
        const knot = root._knot(id)
        const handle = knot ? knot[side + "Handle"] : null
        if (!handle) return false
        const x = Math.max(0, Math.min(1, Number(handle.x) + dx))
        const y = Math.max(0, Math.min(1, Number(handle.y) + dy))
        root.requestTangent(id, side, x, y)
        root.finishTangent(id, side, x, y)
        return true
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
            if (root.referenceLine) {
                ctx.strokeStyle = Tk.Theme.color.divider
                ctx.lineWidth = Tk.Theme.size.border
                ctx.beginPath()
                ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.stroke()
            }
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
        Connections { target: root; function onKnotsChanged() { graph.requestPaint() } }
        Connections { target: root; function onWidthChanged() { graph.requestPaint() } }
        Connections { target: root; function onHeightChanged() { graph.requestPaint() } }
    }
    Repeater {
        id: tangentRepeater
        model: root.tangentHandles
        delegate: Item {
            id: tangent
            required property var modelData
            readonly property point center: root.pixelAt(modelData.x, modelData.y)
            readonly property point knotCenter: root.pixelAt(modelData.knotX, modelData.knotY)
            x: center.x - Tk.Theme.size.controlSm / 2
            y: center.y - Tk.Theme.size.controlSm / 2
            width: Tk.Theme.size.controlSm; height: Tk.Theme.size.controlSm
            objectName: "curveTangent_" + modelData.id + "_" + modelData.side
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("%1 %2 tangent").arg(modelData.label).arg(modelData.side)
            Accessible.description: qsTr("Normalized handle x %1, y %2")
                                      .arg(Number(modelData.x).toFixed(3)).arg(Number(modelData.y).toFixed(3))
            Accessible.onIncreaseAction: root.keyboardTangent(modelData.id, modelData.side, 0, 0.01)
            Accessible.onDecreaseAction: root.keyboardTangent(modelData.id, modelData.side, 0, -0.01)
            Keys.onLeftPressed: function(event) { root.keyboardTangent(modelData.id, modelData.side, -0.01, 0); event.accepted = true }
            Keys.onRightPressed: function(event) { root.keyboardTangent(modelData.id, modelData.side, 0.01, 0); event.accepted = true }
            Keys.onUpPressed: function(event) { root.keyboardTangent(modelData.id, modelData.side, 0, 0.01); event.accepted = true }
            Keys.onDownPressed: function(event) { root.keyboardTangent(modelData.id, modelData.side, 0, -0.01); event.accepted = true }
            Keys.onEscapePressed: function(event) { root.cancelPress(event) }
            Rectangle {
                objectName: "curveTangentLine_" + tangent.modelData.id + "_" + tangent.modelData.side
                x: (tangent.knotCenter.x + tangent.center.x) / 2 - width / 2 - tangent.x
                y: (tangent.knotCenter.y + tangent.center.y) / 2 - height / 2 - tangent.y
                width: Math.sqrt(Math.pow(tangent.center.x - tangent.knotCenter.x, 2)
                                 + Math.pow(tangent.center.y - tangent.knotCenter.y, 2))
                height: Tk.Theme.size.border
                transformOrigin: Item.Center
                rotation: Math.atan2(tangent.center.y - tangent.knotCenter.y,
                                     tangent.center.x - tangent.knotCenter.x) * 180 / Math.PI
                color: Tk.Theme.color.controlActiveBorder
                Accessible.ignored: true
            }
            Rectangle {
                anchors.centerIn: parent
                width: Tk.Theme.size.controlSm / 2; height: width
                radius: Tk.Theme.radius.full
                color: Tk.Theme.color.controlActiveBg
                border.color: tangent.activeFocus || root.selectedKnotId === modelData.id
                              ? Tk.Theme.color.focus : Tk.Theme.color.accent
                border.width: Tk.Theme.size.border
            }
            Tk.ToolTip { visible: tangentHover.hovered && root.tooltip.length > 0; text: root.tooltip }
            HoverHandler { id: tangentHover }
        }
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
            Keys.onLeftPressed: function(event) { root.nudge(knotId, Number(modelData.x) - 0.01, Number(modelData.y)); event.accepted = true }
            Keys.onRightPressed: function(event) { root.nudge(knotId, Number(modelData.x) + 0.01, Number(modelData.y)); event.accepted = true }
            Keys.onUpPressed: function(event) { root.nudge(knotId, Number(modelData.x), Number(modelData.y) + 0.01); event.accepted = true }
            Keys.onDownPressed: function(event) { root.nudge(knotId, Number(modelData.x), Number(modelData.y) - 0.01); event.accepted = true }
            Keys.onEscapePressed: function(event) { root.cancelPress(event) }
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
            const tangent = root.hitTangent(mouse.x, mouse.y)
            const knot = tangent ? null : root.hitKnot(mouse.x, mouse.y)
            if (!tangent && !knot) { mouse.accepted = false; return }
            const id = tangent ? tangent.id : knot.id
            root._pressedId = id
            root._pressedKind = tangent ? "tangent" : "knot"
            root._pressedSide = tangent ? tangent.side : ""
            root._hasPreview = false
            const point = tangent ? tangent.point : knot.point
            root._dragOffsetX = mouse.x - point.x
            root._dragOffsetY = mouse.y - point.y
            if (tangent) { root._originX = tangent.x; root._originY = tangent.y }
            root.selectKnot(id)
            const item = tangent ? tangentRepeater.itemAt(tangent.index) : knotRepeater.itemAt(knot.index)
            if (item) item.forceActiveFocus()
            mouse.accepted = true
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root._pressedId || root._suppressPress) return
            const point = root.normalizedAt(mouse.x - root._dragOffsetX,
                                            mouse.y - root._dragOffsetY)
            if (root._pressedKind === "tangent")
                root.requestTangent(root._pressedId, root._pressedSide, point.x, point.y)
            else root.requestMove(root._pressedId, point.x, point.y)
        }
        onReleased: {
            if (root._pressedKind === "tangent" && !root._suppressPress && root._hasPreview)
                root.finishTangent(root._pressedId, root._pressedSide, root._lastX, root._lastY)
            else if (root._pressedKind === "knot" && !root._suppressPress && root._activeId.length)
                root.finishMove(root._pressedId)
            root._pressedId = ""; root._pressedKind = ""; root._pressedSide = ""
            root._suppressPress = false; root._hasPreview = false
        }
        onCanceled: {
            if (root._pressedKind === "tangent" && !root._suppressPress && root._hasPreview)
                root.cancelTangent(root._pressedId, root._pressedSide, root._originX, root._originY)
            else if (root._pressedKind === "knot" && !root._suppressPress && root._activeId.length)
                root.cancelMove(root._pressedId)
            root._pressedId = ""; root._pressedKind = ""; root._pressedSide = ""
            root._suppressPress = false; root._hasPreview = false
        }
    }
}
