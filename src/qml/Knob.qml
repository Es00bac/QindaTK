// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Host owns the parameter and its transaction. Preview updates are transient;
// only gestureCommitted should enter the host's undo/automation history.
T.Control {
    id: control
    property string parameterId: ""
    property string tooltip: ""
    property real value: 0
    property bool silent: false
    property var mapper: null
    property real from: 0
    property real to: 1
    property real defaultValue: 0
    property real stepSize: (to - from) / 100
    property int decimals: 2
    property string unit: ""
    property string taper: "linear" // host-defined: linear, logarithmic or custom
    property string automationState: "read"
    property bool bipolar: false
    property bool showValue: true
    property bool numericEntry: true
    property real fineFactor: 0.1
    property real _startValue: 0
    property bool _startSilent: false
    property real _lastY: 0
    property bool _dragging: false
    signal gestureStarted(real value, bool silent)
    signal valuePreview(real value, bool silent)
    signal gestureCommitted(real value, bool silent)
    signal gestureCanceled(real value, bool silent)

    readonly property real clampedValue: Math.max(from, Math.min(to, value))
    readonly property real normalizedValue: mapper ? mapper.positionFor(clampedValue, silent)
        : (to > from ? (clampedValue - from) / (to - from) : 0)
    readonly property string valueText: silent ? "−∞" + unit : Number(clampedValue).toFixed(decimals) + unit
    implicitWidth: Math.max(Tk.Theme.size.controlLg, numericEntry ? Tk.Theme.size.fieldWidth : 0)
    implicitHeight: Tk.Theme.size.controlLg + (numericEntry ? Tk.Theme.size.control : showValue ? Tk.Theme.size.controlSm : 0)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.role: Accessible.Slider
    Accessible.name: tooltip.length ? tooltip : (parameterId.length ? parameterId : qsTr("Audio parameter"))
    Accessible.description: qsTr("%1, %2, %3").arg(valueText).arg(taper).arg(automationState)
    Accessible.onIncreaseAction: control.step(1, Qt.NoModifier)
    Accessible.onDecreaseAction: control.step(-1, Qt.NoModifier)
    function clamp(v) { return Math.max(from, Math.min(to, v)) }
    function updateUser(v, isSilent) {
        const next = clamp(v)
        if (next === value && isSilent === silent) return
        value = next; silent = isSilent
        valuePreview(next, isSilent)
    }
    function updatePosition(position) {
        const p = Math.max(0, Math.min(1, position))
        if (mapper) {
            const mapped = mapper.valueAt(p)
            updateUser(mapped.value, mapped.silent)
        } else updateUser(from + p * (to - from), false)
    }
    function step(direction, modifiers) {
        const scale = (modifiers & Qt.ShiftModifier) ? 10
                    : (modifiers & Qt.AltModifier) ? fineFactor : 1
        gestureStarted(value, silent)
        if (mapper) updatePosition(normalizedValue + direction * 0.01 * scale)
        else updateUser(value + direction * stepSize * scale, false)
        gestureCommitted(value, silent)
    }
    function reset() { gestureStarted(value, silent); updateUser(defaultValue, false); gestureCommitted(value, false) }
    Keys.onLeftPressed: (event) => { step(-1, event.modifiers); event.accepted = true }
    Keys.onDownPressed: (event) => { step(-1, event.modifiers); event.accepted = true }
    Keys.onRightPressed: (event) => { step(1, event.modifiers); event.accepted = true }
    Keys.onUpPressed: (event) => { step(1, event.modifiers); event.accepted = true }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            gestureStarted(value, silent)
            updatePosition(event.key === Qt.Key_Home ? 0 : 1)
            gestureCommitted(value, silent)
            event.accepted = true
        }
    }
    Keys.onEscapePressed: (event) => {
        if (_dragging) { _dragging = false; value = _startValue; silent = false; gestureCanceled(value, silent); event.accepted = true }
    }
    Keys.onReturnPressed: (event) => { reset(); event.accepted = true }

    Item {
        id: dial
        width: Tk.Theme.size.controlLg
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        Canvas {
            id: dialCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width, h = height, cx = w / 2, cy = h / 2
                const radius = Math.min(w, h) * 0.39
                const start = Math.PI * 0.75, end = Math.PI * 2.25
                ctx.lineWidth = Tk.Theme.space.xs
                ctx.lineCap = "round"
                ctx.strokeStyle = Tk.Theme.color.borderStrong
                ctx.beginPath(); ctx.arc(cx, cy, radius, start, end); ctx.stroke()
                ctx.strokeStyle = control.enabled ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                ctx.beginPath(); ctx.arc(cx, cy, radius, start, start + (end - start) * control.normalizedValue); ctx.stroke()
                const angle = start + (end - start) * control.normalizedValue
                ctx.strokeStyle = Tk.Theme.color.text
                ctx.lineWidth = Tk.Theme.size.border + 1
                ctx.beginPath(); ctx.moveTo(cx + Math.cos(angle) * radius * 0.52, cy + Math.sin(angle) * radius * 0.52)
                ctx.lineTo(cx + Math.cos(angle) * radius * 0.9, cy + Math.sin(angle) * radius * 0.9); ctx.stroke()
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.52; height: width; radius: width / 2
            color: control._dragging ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.panelAlt
            border.width: Tk.Theme.size.border; border.color: Tk.Theme.color.border
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: (mouse) => {
                control.forceActiveFocus(); control._dragging = true
                control._startValue = control.value; control._startSilent = control.silent
                control._lastY = mouse.y
                control.gestureStarted(control.value, control.silent)
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const multiplier = mouse.modifiers & Qt.ShiftModifier ? 0.1
                                 : mouse.modifiers & Qt.AltModifier ? 0.01 : 1
                control.updatePosition(control.normalizedValue + (control._lastY - mouse.y) * 0.005 * multiplier)
                control._lastY = mouse.y
            }
            onReleased: { control._dragging = false; control.gestureCommitted(control.value, control.silent) }
            onCanceled: {
                if (control._dragging) { control._dragging = false; control.value = control._startValue; control.silent = control._startSilent; control.gestureCanceled(control.value, control.silent) }
            }
            onDoubleClicked: control.reset()
            onWheel: (wheel) => {
                control.step(wheel.angleDelta.y > 0 ? 1 : -1, wheel.modifiers)
                wheel.accepted = true
            }
        }
    }
    Tk.NumberField {
        visible: control.numericEntry && !control.silent
        anchors.top: dial.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Tk.Theme.size.fieldWidth
        value: control.value
        from: control.from; to: control.to; defaultValue: control.defaultValue
        stepSize: control.stepSize; decimals: control.decimals; suffix: control.unit
        tooltip: control.tooltip; Accessible.name: control.Accessible.name
        onValueModified: (v) => {
            control.gestureStarted(control.value, control.silent)
            control.updateUser(v, false)
            control.gestureCommitted(control.value, false)
        }
    }
    Tk.Mono {
        visible: control.showValue && (!control.numericEntry || control.silent)
        anchors.top: dial.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: control.valueText
        color: Tk.Theme.color.textMuted
        font.pixelSize: Tk.Theme.font.caption
    }
    onValueChanged: dialCanvas.requestPaint()
    Tk.ToolTip { text: control.tooltip; visible: control.tooltip.length > 0 && control.hovered && !control._dragging }
}
