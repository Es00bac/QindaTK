// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/audio-measurement.md): the host owns
// the parameter. Every input (drag, wheel burst, held key, numeric entry,
// reset) is ONE gesture: gestureStarted -> valuePreview* -> gestureCommitted |
// gestureCanceled. With `binding` (Tk.AudioParameterBinding) the knob shows
// the engine parameter and each gesture is one engine transaction; the knob
// never assigns `value` itself then. objectNames: "knobDial", "knobField",
// "knobValue".
T.Control {
    id: control
    property string parameterId: ""
    property string tooltip: ""
    property real value: 0
    property bool silent: false
    property var mapper: null
    property var binding: null
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
    readonly property bool gestureOpen: _source.length > 0
    property string _source: ""
    property real _startValue: 0
    property bool _startSilent: false
    property real _anchorY: 0
    property real _anchorPosition: 0
    property real _dragFactor: 1
    readonly property bool _dragging: _source === "pointer"
    signal gestureStarted(real value, bool silent)
    signal valuePreview(real value, bool silent)
    signal gestureCommitted(real value, bool silent)
    signal gestureCanceled(real value, bool silent)

    readonly property var activeMapper: binding ? binding.mapper : mapper
    readonly property real clampedValue: Math.max(from, Math.min(to, value))
    // AGENT-GUARD: the mapper's invokables are not tracked by QML; the
    // revision argument re-evaluates the position when the mapping changes
    // (for example a binding whose parameter arrives after the knob was built).
    property int _mappingRevision: 0
    readonly property real normalizedValue: positionOf(_mappingRevision, clampedValue, silent)
    readonly property string valueText: binding ? binding.valueText
        : silent ? "−∞" + unit : Number(clampedValue).toFixed(decimals) + unit
    implicitWidth: Math.max(Tk.Theme.size.controlLg, numericEntry ? Tk.Theme.size.fieldWidth : 0)
    implicitHeight: Tk.Theme.size.controlLg + (numericEntry ? Tk.Theme.size.control : showValue ? Tk.Theme.size.controlSm : 0)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.role: Accessible.Slider
    Accessible.name: tooltip.length ? tooltip : (parameterId.length ? parameterId : qsTr("Audio parameter"))
    Accessible.description: qsTr("%1, %2, %3").arg(valueText).arg(taper).arg(automationState)
    Accessible.onIncreaseAction: control.step(1, Qt.NoModifier)
    Accessible.onDecreaseAction: control.step(-1, Qt.NoModifier)

    // Engine binding: the parameter's own range, units and state.
    Binding { target: control; property: "value"; when: control.binding !== null; value: control.binding ? control.binding.value : 0 }
    Binding { target: control; property: "silent"; when: control.binding !== null; value: control.binding ? control.binding.silent : false }
    Binding { target: control; property: "from"; when: control.binding !== null; value: control.binding ? control.binding.minimum : 0 }
    Binding { target: control; property: "to"; when: control.binding !== null; value: control.binding ? control.binding.maximum : 1 }
    Binding { target: control; property: "defaultValue"; when: control.binding !== null; value: control.binding ? control.binding.resetValue : 0 }
    Binding { target: control; property: "unit"; when: control.binding !== null; value: control.binding && control.binding.unit.length ? " " + control.binding.unit : "" }
    Binding { target: control; property: "taper"; when: control.binding !== null; value: control.binding ? control.binding.taper : "linear" }
    Binding { target: control; property: "automationState"; when: control.binding !== null; value: control.binding ? control.binding.automationState : "" }
    Binding { target: control; property: "parameterId"; when: control.binding !== null; value: control.binding ? control.binding.parameterId : "" }

    function clamp(v) { return Math.max(from, Math.min(to, v)) }
    function positionOf(_revision, v, isSilent) {
        if (activeMapper) return activeMapper.positionFor(v, isSilent)
        return to > from ? (v - from) / (to - from) : 0
    }
    function beginGesture(source) {
        if (_source === "wheel" && source !== "wheel") commitGesture() // another input ends a wheel burst
        if (_source.length) return false
        _source = source
        _startValue = value; _startSilent = silent
        if (binding) binding.begin(value, silent)
        gestureStarted(value, silent)
        return true
    }
    function updateUser(v, isSilent) {
        const next = clamp(v)
        if (next === value && isSilent === silent) return
        if (binding) binding.preview(next, isSilent)
        else { value = next; silent = isSilent }
        valuePreview(value, silent)
    }
    function commitGesture() {
        if (!_source.length) return
        _source = ""
        wheelIdle.stop()
        if (binding) binding.commit(value, silent)
        gestureCommitted(value, silent)
    }
    function cancelGesture() {
        if (!_source.length) return
        _source = ""
        wheelIdle.stop()
        if (binding) binding.cancel()
        else { value = _startValue; silent = _startSilent }
        gestureCanceled(value, silent)
    }
    function updatePosition(position) {
        const p = Math.max(0, Math.min(1, position))
        if (activeMapper) {
            const mapped = activeMapper.valueAt(p)
            updateUser(mapped.value, mapped.silent)
        } else updateUser(from + p * (to - from), false)
    }
    function stepBy(direction, modifiers) {
        const scale = (modifiers & Qt.ShiftModifier) ? 10
                    : (modifiers & Qt.AltModifier) ? fineFactor : 1
        if (activeMapper) updatePosition(normalizedValue + direction * 0.01 * scale)
        else updateUser(value + direction * stepSize * scale, false)
    }
    // A step outside any open gesture is a gesture of its own.
    function step(direction, modifiers) {
        const own = beginGesture("step")
        stepBy(direction, modifiers)
        if (own) commitGesture()
    }
    function reset() {
        // Inside a pointer gesture (double-click) the reset joins it: the
        // release commits once, so a reset is one transaction.
        if (_source === "pointer") { updateUser(defaultValue, false); return }
        const own = beginGesture("reset")
        updateUser(defaultValue, false)
        if (own) commitGesture()
    }
    // Arrow keys: a held key (auto-repeat) is one gesture until release.
    function keyStep(direction, event) {
        if (_source !== "key") beginGesture("key")
        if (_source === "key") stepBy(direction, event.modifiers)
        event.accepted = true
    }
    Keys.onLeftPressed: (event) => keyStep(-1, event)
    Keys.onDownPressed: (event) => keyStep(-1, event)
    Keys.onRightPressed: (event) => keyStep(1, event)
    Keys.onUpPressed: (event) => keyStep(1, event)
    Keys.onReleased: (event) => {
        if (event.isAutoRepeat || _source !== "key") return
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            commitGesture()
            event.accepted = true
        }
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            const own = beginGesture("key-jump")
            updatePosition(event.key === Qt.Key_Home ? 0 : 1)
            if (own) commitGesture()
            event.accepted = true
        }
    }
    Keys.onEscapePressed: (event) => {
        if (_source.length) cancelGesture()
        else event.accepted = false // nothing to cancel: Escape belongs to the window
    }
    Keys.onReturnPressed: (event) => { reset(); event.accepted = true }
    onActiveFocusChanged: if (!activeFocus && (_source === "key" || _source === "wheel")) commitGesture()

    Timer {
        id: wheelIdle
        interval: Tk.Theme.motion.gestureIdle
        onTriggered: control.commitGesture()
    }
    Connections {
        target: control.activeMapper
        ignoreUnknownSignals: true
        function onChanged() { control._mappingRevision++ }
    }

    Item {
        id: dial
        objectName: "knobDial"
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
            // Relative vertical drag, 200 px per full travel; Shift and Alt
            // are finer. The anchor moves when the modifier changes, so the
            // value never jumps.
            function factorFor(modifiers) {
                return modifiers & Qt.ShiftModifier ? 0.1 : modifiers & Qt.AltModifier ? 0.01 : 1
            }
            onPressed: (mouse) => {
                control.forceActiveFocus()
                if (!control.beginGesture("pointer")) return
                control._anchorY = mouse.y
                control._anchorPosition = control.normalizedValue
                control._dragFactor = factorFor(mouse.modifiers)
            }
            onPositionChanged: (mouse) => {
                if (!pressed || !control._dragging) return
                const factor = factorFor(mouse.modifiers)
                if (factor !== control._dragFactor) {
                    control._anchorY = mouse.y
                    control._anchorPosition = control.normalizedValue
                    control._dragFactor = factor
                }
                control.updatePosition(control._anchorPosition + (control._anchorY - mouse.y) * 0.005 * factor)
            }
            onReleased: if (control._dragging) control.commitGesture()
            onCanceled: if (control._dragging) control.cancelGesture()
            onDoubleClicked: control.reset()
            onWheel: (wheel) => {
                wheel.accepted = true
                if (!control._source.length) control.beginGesture("wheel")
                if (control._source !== "wheel") return
                control.stepBy(wheel.angleDelta.y > 0 ? 1 : -1, wheel.modifiers)
                wheelIdle.restart()
            }
        }
    }
    Tk.NumberField {
        id: field
        objectName: "knobField"
        visible: control.numericEntry && !control.silent
        anchors.top: dial.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Tk.Theme.size.fieldWidth
        from: control.from; to: control.to; defaultValue: control.defaultValue
        stepSize: control.stepSize; decimals: control.decimals; suffix: control.unit
        tooltip: control.tooltip; Accessible.name: control.Accessible.name
        onValueModified: (v) => {
            const own = control.beginGesture("text")
            control.updateUser(v, false)
            if (own) control.commitGesture()
        }
    }
    Tk.Mono {
        objectName: "knobValue"
        visible: control.showValue && (!control.numericEntry || control.silent)
        anchors.top: dial.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: control.valueText
        color: Tk.Theme.color.textMuted
        font.pixelSize: Tk.Theme.font.caption
    }
    // AGENT-GUARD: NumberField assigns its own `value` on a typed commit,
    // which would break a plain binding; the Binding element re-asserts the
    // knob's value after every change, so the field never goes stale.
    Binding { target: field; property: "value"; value: control.value }
    onNormalizedValueChanged: dialCanvas.requestPaint()
    onEnabledChanged: dialCanvas.requestPaint()
    Tk.ToolTip { text: control.tooltip; visible: control.tooltip.length > 0 && control.hovered && !control._dragging }
}
