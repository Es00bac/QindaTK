// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// The owner stores the canonical parameter. `silent` is an explicit domain
// state (for example -inf dB); no finite attenuation is substituted for it.
T.Control {
    id: control
    property string parameterId: ""
    property string tooltip: ""
    property real value: 0
    property var mapper: null
    property real from: -60
    property real to: 6
    property real defaultValue: 0
    property real stepSize: 0.1
    property real positionStep: 0.01
    property real unityValue: 0
    property var scaleMarks: [-60, -30, -18, -12, -6, 0, 6]
    property string unit: "dB"
    property string taper: "decibel"
    property string automationState: "read"
    property bool silent: false
    property bool bipolar: false
    property bool vertical: true
    property bool numericEntry: true
    property real _startValue: 0
    property bool _startSilent: false
    property bool _dragging: false
    signal gestureStarted(real value, bool silent)
    signal valuePreview(real value, bool silent)
    signal gestureCommitted(real value, bool silent)
    signal gestureCanceled(real value, bool silent)

    readonly property real normalizedValue: mapper ? mapper.positionFor(value, silent)
        : silent ? 0 : Math.max(0, Math.min(1, (value - from) / Math.max(0.000001, to - from)))
    readonly property string valueText: silent ? "−∞ " + unit : Number(value).toFixed(1) + " " + unit
    implicitWidth: vertical ? Tk.Theme.size.controlLg * 3 : Tk.Theme.size.fieldWidth
    implicitHeight: vertical ? Tk.Theme.size.controlLg * 8 + (numericEntry ? Tk.Theme.size.control : 0)
                             : Tk.Theme.size.controlSm * 2
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Slider
    Accessible.name: tooltip.length ? tooltip : parameterId.length ? parameterId : qsTr("Audio fader")
    Accessible.description: qsTr("%1, unity at %2 %3, %4").arg(valueText).arg(unityValue).arg(unit).arg(automationState)
    Accessible.onIncreaseAction: control.step(1)
    Accessible.onDecreaseAction: control.step(-1)
    function clamp(v) { return Math.max(from, Math.min(to, v)) }
    function setUserValue(v) {
        const next = clamp(v)
        if (next !== value || silent) {
            value = next; silent = false; valuePreview(value, false)
        }
    }
    function setUserPosition(position) {
        const p = Math.max(0, Math.min(1, position))
        if (mapper) {
            const mapped = mapper.valueAt(p)
            if (mapped.value !== value || mapped.silent !== silent) {
                value = mapped.value; silent = mapped.silent; valuePreview(value, silent)
            }
        } else setUserValue(from + p * (to - from))
    }
    function step(direction, modifiers) {
        const scale = (modifiers & Qt.ShiftModifier) ? 10 : (modifiers & Qt.AltModifier) ? 0.1 : 1
        gestureStarted(value, silent)
        if (mapper) setUserPosition(normalizedValue + direction * positionStep * scale)
        else setUserValue(value + direction * stepSize * scale)
        gestureCommitted(value, silent)
    }
    function reset() { gestureStarted(value, silent); value = clamp(defaultValue); silent = false; valuePreview(value, false); gestureCommitted(value, false) }
    function normalizedFromPosition(pos) { return vertical ? 1 - pos : pos }
    Keys.onUpPressed: (event) => { step(1); event.accepted = true }
    Keys.onRightPressed: (event) => { step(1); event.accepted = true }
    Keys.onDownPressed: (event) => { step(-1); event.accepted = true }
    Keys.onLeftPressed: (event) => { step(-1); event.accepted = true }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            gestureStarted(value, silent)
            setUserPosition(event.key === Qt.Key_Home ? 0 : 1)
            gestureCommitted(value, silent)
            event.accepted = true
        }
    }
    Keys.onEscapePressed: (event) => {
        if (_dragging) { _dragging = false; value = _startValue; silent = _startSilent; gestureCanceled(value, silent); event.accepted = true }
    }
    Keys.onReturnPressed: (event) => { reset(); event.accepted = true }

    Item {
        id: trackBox
        anchors.fill: parent
        anchors.bottomMargin: control.numericEntry ? Tk.Theme.size.control : 0
        Rectangle {
            id: track
            anchors.centerIn: parent
            width: control.vertical ? Tk.Theme.size.controlSm : parent.width - Tk.Theme.space.md
            height: control.vertical ? parent.height - Tk.Theme.size.controlLg : Tk.Theme.size.controlSm
            radius: Tk.Theme.radius.sm
            color: Tk.Theme.color.inputBg
            border.width: Tk.Theme.size.border; border.color: Tk.Theme.color.border
        }
        Rectangle {
            x: track.x
            y: control.vertical ? thumb.y + thumb.height / 2 : track.y
            width: control.vertical ? track.width : Math.max(0, thumb.x - track.x + thumb.width / 2)
            height: control.vertical ? Math.max(0, track.bottom - y) : track.height
            color: Tk.Theme.alpha(Tk.Theme.color.accent, 0.45)
        }
        Repeater {
            model: control.scaleMarks
            delegate: Item {
                required property real modelData
                readonly property real position: (modelData - control.from) / (control.to - control.from)
                x: control.vertical ? track.x + track.width + Tk.Theme.space.sm : track.x + position * track.width
                y: control.vertical ? track.y + (1 - position) * track.height : track.y + track.height + Tk.Theme.space.sm
                Rectangle {
                    x: control.vertical ? 0 : -Tk.Theme.space.xs / 2
                    y: control.vertical ? -Tk.Theme.space.xs / 2 : 0
                    width: control.vertical ? Tk.Theme.space.sm : Tk.Theme.space.xs
                    height: control.vertical ? Tk.Theme.space.xs : Tk.Theme.space.sm
                    color: modelData === control.unityValue ? Tk.Theme.color.accent : Tk.Theme.color.borderStrong
                }
                Tk.Mono {
                    x: control.vertical ? Tk.Theme.space.md : -implicitWidth / 2
                    y: control.vertical ? -implicitHeight / 2 : Tk.Theme.space.sm
                    text: Number(modelData).toFixed(0)
                    color: modelData === control.unityValue ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                }
            }
        }
        Rectangle {
            id: unityMark
            visible: control.unityValue >= control.from && control.unityValue <= control.to
            x: control.vertical ? track.x - Tk.Theme.space.xs : track.x + (control.unityValue-control.from)/(control.to-control.from)*track.width
            y: control.vertical ? track.y + (1-(control.unityValue-control.from)/(control.to-control.from))*track.height : track.y - Tk.Theme.space.xs
            width: control.vertical ? track.width + Tk.Theme.space.md : Tk.Theme.space.xs
            height: control.vertical ? Tk.Theme.space.xs : track.height + Tk.Theme.space.md
            color: Tk.Theme.color.warning
            z: 1
        }
        Rectangle {
            id: thumb
            width: Tk.Theme.size.grip; height: Tk.Theme.size.controlSm
            x: control.vertical ? track.x + (track.width-width)/2
                 : track.x + control.normalizedValue * (track.width-width)
            y: control.vertical ? track.y + (1-control.normalizedValue) * (track.height-height)
                 : track.y + (track.height-height)/2
            radius: Tk.Theme.radius.sm
            color: control._dragging ? Tk.Theme.color.accent : Tk.Theme.color.panelAlt
            border.width: Tk.Theme.size.border; border.color: control.enabled ? Tk.Theme.color.borderStrong : Tk.Theme.color.textMuted
            z: 2
            Rectangle { anchors.fill: parent; anchors.margins: -Tk.Theme.size.focusRing; color: "transparent"; border.width: Tk.Theme.size.focusRing; border.color: Tk.Theme.color.focus; visible: control.visualFocus }
        }
        MouseArea {
            anchors.fill: parent
            onPressed: (mouse) => {
                control.forceActiveFocus(); control._dragging = true
                control._startValue = control.value; control._startSilent = control.silent
                control.gestureStarted(control.value, control.silent)
                const p = control.normalizedFromPosition(control.vertical ? mouse.y / height : mouse.x / width)
                control.setUserPosition(p)
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const p = control.normalizedFromPosition(control.vertical ? mouse.y / height : mouse.x / width)
                control.setUserPosition(p)
            }
            onReleased: { control._dragging = false; control.gestureCommitted(control.value, control.silent) }
            onCanceled: {
                if (control._dragging) { control._dragging = false; control.value = control._startValue; control.silent = control._startSilent; control.gestureCanceled(control.value, control.silent) }
            }
            onDoubleClicked: control.reset()
            onWheel: (wheel) => { control.step(wheel.angleDelta.y > 0 ? 1 : -1, wheel.modifiers); wheel.accepted = true }
        }
    }
    Tk.NumberField {
        visible: control.numericEntry && !control.silent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        value: control.value
        from: control.from; to: control.to; defaultValue: control.defaultValue
        stepSize: control.stepSize; decimals: 1; suffix: " " + control.unit
        tooltip: control.tooltip; Accessible.name: control.Accessible.name
        onValueModified: (v) => { control.gestureStarted(control.value, control.silent); control.value=v; control.silent=false; control.valuePreview(v, false); control.gestureCommitted(v, false) }
    }
    Tk.Mono {
        visible: control.numericEntry && control.silent
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        text: control.valueText
        color: Tk.Theme.color.textMuted
        font.pixelSize: Tk.Theme.font.caption
        horizontalAlignment: Text.AlignHCenter
        Accessible.name: control.valueText
    }
    Tk.ToolTip { text: control.tooltip; visible: control.tooltip.length > 0 && control.hovered && !control._dragging }
}
