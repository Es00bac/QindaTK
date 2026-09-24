// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the grade host defines parameter spaces, ranges, linkage,
// reset semantics, transactions and color math. Each wheel descriptor supplies
// current x/y values and numeric bounds; this view only previews and requests.
T.Control {
    id: root
    property string gradeId: ""
    property int parameterRevision: 0
    property var wheels: [] // {id,label,x,y,minimumX,maximumX,minimumY,maximumY,stepX,stepY,enabled}
    property string selectedWheelId: ""
    property var editAcknowledgement: ({ status: "", message: "" })
    property string statusText: ""
    property string tooltip: ""
    property bool editingEnabled: true
    property string _gestureId: ""
    property int _serial: 0
    property string _gestureWheelId: ""
    property real _targetX: 0
    property real _targetY: 0
    readonly property string effectiveStatus: root.statusText.length ? root.statusText
        : root._statusMessage.length ? root._statusMessage
        : String(root.editAcknowledgement.message || root.editAcknowledgement.status || "")
    readonly property int maximumWheels: 8

    signal wheelSelectionRequested(string gradeId, string wheelId)
    signal wheelPreviewRequested(string gestureId, string gradeId, string wheelId, real x, real y)
    signal wheelEditFinished(string gestureId, string gradeId, string wheelId, real x, real y)
    signal wheelEditCanceled(string gestureId, string gradeId, string wheelId)
    signal resetRequested(string gradeId, string wheelId)

    objectName: "colorWheels"
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.waveformHeight * 3
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Color grading wheels")
    Accessible.description: root.effectiveStatus.length ? root.effectiveStatus
        : qsTr("Host-defined grading parameters. Selected wheel: %1. Arrow keys adjust it; Home requests reset; Escape cancels an active pointer gesture.")
          .arg(root.selectedWheelId || qsTr("none"))
    activeFocusOnTab: true
    focus: true

    function wheelAt(x, y) {
        const count = Math.min(root.wheels.length, root.maximumWheels)
        if (!count) return null
        const slot = root.width / count
        const index = Math.max(0, Math.min(count - 1, Math.floor(x / Math.max(1, slot))))
        const wheel = root.wheels[index]
        const centerX = slot * (index + 0.5), centerY = root.height / 2
        const radius = Math.min(slot * 0.37, root.height * 0.36)
        const dx = x - centerX, dy = y - centerY
        return dx * dx + dy * dy <= radius * radius ? {wheel:wheel,index:index,centerX:centerX,centerY:centerY,radius:radius} : null
    }
    function wheelById(id) {
        for (let i = 0; i < Math.min(root.wheels.length, root.maximumWheels); ++i)
            if (String(root.wheels[i].id) === String(id)) return root.wheels[i]
        return null
    }
    function beginWheelEdit(wheelId, x, y) {
        const wheel = root.wheelById(wheelId)
        if (!root.enabled || !root.editingEnabled || root._gestureId.length || !wheel || wheel.enabled === false)
            return false
        if (![wheel.minimumX,wheel.maximumX,wheel.minimumY,wheel.maximumY].every(value => isFinite(Number(value)))
                || Number(wheel.maximumX) <= Number(wheel.minimumX)
                || Number(wheel.maximumY) <= Number(wheel.minimumY)) {
            root._statusMessage = qsTr("Wheel parameter ranges unavailable")
            return false
        }
        root._statusMessage = ""
        root._serial += 1
        root._gestureId = "grade-wheel-" + root._serial
        root._gestureWheelId = String(wheelId)
        root.wheelSelectionRequested(root.gradeId, root._gestureWheelId)
        return updateWheelAt(x, y)
    }
    property string _statusMessage: ""
    function updateWheelAt(x, y) {
        if (!root._gestureId.length) return false
        const wheel = root.wheelById(root._gestureWheelId)
        if (!wheel) return false
        const count = Math.min(root.wheels.length, root.maximumWheels)
        let index = -1
        for (let i = 0; i < count; ++i)
            if (String(root.wheels[i].id) === root._gestureWheelId) index = i
        if (index < 0) return false
        const slot = root.width / Math.max(1, count)
        const centerX = slot * (index + 0.5), centerY = root.height / 2
        const radius = Math.min(slot * 0.37, root.height * 0.36)
        const nx = Math.max(-1, Math.min(1, (x - centerX) / Math.max(1, radius)))
        const ny = Math.max(-1, Math.min(1, (centerY - y) / Math.max(1, radius)))
        root._targetX = Number(wheel.minimumX) + (nx + 1) * 0.5 * (Number(wheel.maximumX) - Number(wheel.minimumX))
        root._targetY = Number(wheel.minimumY) + (ny + 1) * 0.5 * (Number(wheel.maximumY) - Number(wheel.minimumY))
        root.wheelPreviewRequested(root._gestureId, root.gradeId, root._gestureWheelId,
                                   root._targetX, root._targetY)
        wheelCanvas.requestPaint()
        return true
    }
    function finishWheelEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId, wheelId = root._gestureWheelId
        root.wheelEditFinished(id, root.gradeId, wheelId, root._targetX, root._targetY)
        root._gestureId = ""
        wheelCanvas.requestPaint()
        return true
    }
    function cancelWheelEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId, wheelId = root._gestureWheelId
        root._gestureId = ""
        root.wheelEditCanceled(id, root.gradeId, wheelId)
        wheelCanvas.requestPaint()
        return true
    }
    // AGENT-CONTRACT (VID-08): numeric entry and keyboard nudges go through
    // requestWheelValue, which emits the same selection/preview/finish
    // signals as a pointer drag with the same x/y, so a host (for example
    // ColorGradeEditSession.previewWheel/finishWheel) cannot tell them apart.
    function requestWheelValue(wheelId, x, y) {
        const wheel = root.wheelById(wheelId)
        if (!root.enabled || !root.editingEnabled || root._gestureId.length || !wheel || wheel.enabled === false)
            return false
        if (![wheel.minimumX, wheel.maximumX, wheel.minimumY, wheel.maximumY, x, y].every(value => isFinite(Number(value))))
            return false
        root._serial += 1
        const cx = Math.max(Number(wheel.minimumX), Math.min(Number(wheel.maximumX), Number(x)))
        const cy = Math.max(Number(wheel.minimumY), Math.min(Number(wheel.maximumY), Number(y)))
        const id = "grade-wheel-value-" + root._serial
        root.wheelSelectionRequested(root.gradeId, String(wheelId))
        root.wheelPreviewRequested(id, root.gradeId, String(wheelId), cx, cy)
        root.wheelEditFinished(id, root.gradeId, String(wheelId), cx, cy)
        return true
    }
    function nudgeSelected(dx, dy) {
        const wheel = root.wheelById(root.selectedWheelId)
        if (!wheel || !isFinite(Number(wheel.stepX)) || !isFinite(Number(wheel.stepY))) return false
        return root.requestWheelValue(root.selectedWheelId, Number(wheel.x) + dx * Number(wheel.stepX),
                                      Number(wheel.y) + dy * Number(wheel.stepY))
    }
    function requestReset(wheelId) {
        const wheel = root.wheelById(wheelId)
        if (!root.enabled || !root.editingEnabled || !wheel) return false
        root.resetRequested(root.gradeId, String(wheelId))
        return true
    }
    function css(c) { return "rgba(" + Math.round(c.r*255) + "," + Math.round(c.g*255) + "," + Math.round(c.b*255) + "," + c.a + ")" }

    background: Rectangle { color: Tk.Theme.color.panelAlt; border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border; radius: Tk.Theme.radius.sm }
    Canvas {
        id: wheelCanvas
        objectName: "colorWheelCanvas"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            ctx.fillStyle = root.css(Tk.Theme.color.surface); ctx.fillRect(0, 0, width, height)
            const count = Math.min(root.wheels.length, root.maximumWheels)
            const slot = width / Math.max(1, count), radius = Math.min(slot * 0.37, height * 0.36)
            const roles = [Tk.Theme.color.accent, Tk.Theme.color.info, Tk.Theme.color.success,
                           Tk.Theme.color.warning, Tk.Theme.color.danger, Tk.Theme.color.focus,
                           Tk.Theme.color.accent, Tk.Theme.color.info]
            ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
            for (let i = 0; i < count; ++i) {
                const wheel = root.wheels[i], cx = slot * (i + 0.5), cy = height / 2
                const selected = String(wheel.id) === root.selectedWheelId || String(wheel.id) === root._gestureWheelId
                ctx.fillStyle = root.css(Tk.Theme.color.panelAlt)
                ctx.beginPath(); ctx.arc(cx, cy, radius, 0, Math.PI * 2); ctx.fill()
                ctx.strokeStyle = root.css(selected ? Tk.Theme.color.accent : Tk.Theme.color.border)
                ctx.lineWidth = Tk.Theme.size.border
                ctx.beginPath(); ctx.arc(cx, cy, radius, 0, Math.PI * 2); ctx.stroke()
                for (let ring = 1; ring <= 2; ++ring) {
                    ctx.strokeStyle = root.css(Tk.Theme.color.divider)
                    ctx.beginPath(); ctx.arc(cx, cy, radius * ring / 3, 0, Math.PI * 2); ctx.stroke()
                }
                ctx.strokeStyle = root.css(Tk.Theme.color.textMuted)
                ctx.beginPath(); ctx.moveTo(cx-radius, cy); ctx.lineTo(cx+radius, cy); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(cx, cy-radius); ctx.lineTo(cx, cy+radius); ctx.stroke()
                const rangeX = Number(wheel.maximumX) - Number(wheel.minimumX)
                const rangeY = Number(wheel.maximumY) - Number(wheel.minimumY)
                if (isFinite(rangeX) && isFinite(rangeY) && rangeX > 0 && rangeY > 0
                        && isFinite(Number(wheel.x)) && isFinite(Number(wheel.y))) {
                    const px = cx + (Number(wheel.x) - Number(wheel.minimumX)) / rangeX * 2 * radius - radius
                    const py = cy - ((Number(wheel.y) - Number(wheel.minimumY)) / rangeY * 2 * radius - radius)
                    ctx.strokeStyle = root.css(roles[i])
                    ctx.lineWidth = Tk.Theme.size.border + Tk.Theme.size.border
                    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(px, py); ctx.stroke()
                    ctx.fillStyle = root.css(roles[i])
                    ctx.beginPath(); ctx.arc(px, py, Tk.Theme.size.controlSm / 4, 0, Math.PI * 2); ctx.fill()
                }
                ctx.fillStyle = root.css(Tk.Theme.color.text)
                ctx.textAlign = "center"; ctx.textBaseline = "top"
                ctx.fillText(String(wheel.label || wheel.id || ""), cx, cy + radius + Tk.Theme.space.xs)
                if (isFinite(Number(wheel.x)) && isFinite(Number(wheel.y))) {
                    ctx.fillStyle = root.css(Tk.Theme.color.textMuted)
                    ctx.fillText(Number(wheel.x).toFixed(2) + ", " + Number(wheel.y).toFixed(2), cx,
                                 cy + radius + Tk.Theme.space.xs + Tk.Theme.font.caption + Tk.Theme.space.xs)
                }
            }
        }
    }
    TapHandler {
        onTapped: function(point) {
            const item = root.wheelAt(point.position.x, point.position.y)
            if (item && item.wheel.enabled !== false)
                root.wheelSelectionRequested(root.gradeId, String(item.wheel.id))
        }
    }
    DragHandler {
        target: null
        enabled: root.editingEnabled
        onActiveChanged: {
            if (active) {
                const item = root.wheelAt(centroid.position.x, centroid.position.y)
                if (item) root.beginWheelEdit(String(item.wheel.id), centroid.position.x, centroid.position.y)
            } else if (root._gestureId.length) root.finishWheelEdit()
        }
        onCentroidChanged: if (active) root.updateWheelAt(centroid.position.x, centroid.position.y)
    }
    Keys.onEscapePressed: event => { root.cancelWheelEdit(); event.accepted = true }
    Keys.onLeftPressed: event => { root.nudgeSelected(-1, 0); event.accepted = true }
    Keys.onRightPressed: event => { root.nudgeSelected(1, 0); event.accepted = true }
    Keys.onUpPressed: event => { root.nudgeSelected(0, 1); event.accepted = true }
    Keys.onDownPressed: event => { root.nudgeSelected(0, -1); event.accepted = true }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home) {
            root.requestReset(root.selectedWheelId)
            event.accepted = true
        }
    }
    function repaint() { wheelCanvas.requestPaint() }
    onWheelsChanged: repaint()
    onParameterRevisionChanged: repaint()
    onSelectedWheelIdChanged: repaint()
    onWidthChanged: repaint()
    onHeightChanged: repaint()
    Tk.Caption {
        objectName: "colorWheelsStatus"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.effectiveStatus.length > 0 || root._statusMessage.length > 0
        text: root._statusMessage.length ? root._statusMessage : root.effectiveStatus
        color: String(root.editAcknowledgement.status || "") === "rejected" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        elide: Text.ElideRight
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
