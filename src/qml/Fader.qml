// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/audio-measurement.md): the owner
// stores the canonical parameter. `silent` is an explicit domain state (for
// example -inf dB); no finite attenuation is substituted for it. Every input
// is ONE gesture (see Knob); with `binding` (Tk.AudioParameterBinding) each
// gesture is one engine transaction and the fader never assigns `value`.
// Pointer drags are relative: pressing never jumps the level. Scale marks and
// the unity mark sit where the mapper puts those values under the thumb
// centre. objectNames: "faderTrack", "faderThumb", "faderScaleLabel",
// "faderUnityMark", "faderField", "faderValue".
T.Control {
    id: control
    property string parameterId: ""
    property string tooltip: ""
    property real value: 0
    property var mapper: null
    property var binding: null
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
    property bool silenceMark: false // the mark at `from` reads −∞ (a gain's silence floor)
    property bool bipolar: false
    property bool vertical: true
    property bool numericEntry: true
    readonly property bool gestureOpen: _source.length > 0
    property string _source: ""
    property real _startValue: 0
    property bool _startSilent: false
    property real _anchor: 0
    property real _anchorPosition: 0
    property real _dragFactor: 1
    readonly property bool _dragging: _source === "pointer"
    // AGENT-GUARD (WP-062): the binding a gesture began on receives all of it.
    // A recycled delegate gets another binding; the open gesture is canceled
    // on the old one instead of committing into another channel's parameter.
    property var _gestureBinding: null
    signal gestureStarted(real value, bool silent)
    signal valuePreview(real value, bool silent)
    signal gestureCommitted(real value, bool silent)
    signal gestureCanceled(real value, bool silent)

    readonly property var activeMapper: binding ? binding.mapper : mapper
    readonly property real normalizedValue: markPosition(value, silent)
    readonly property string valueText: binding ? binding.valueText
        : silent ? "−∞ " + unit : Number(value).toFixed(1) + " " + unit
    readonly property real travel: Math.max(1, vertical ? track.height - thumb.height : track.width - thumb.width)
    implicitWidth: vertical ? Tk.Theme.size.controlLg * 3 : Tk.Theme.size.fieldWidth
    implicitHeight: vertical ? Tk.Theme.size.controlLg * 8 + (numericEntry ? Tk.Theme.size.control : 0)
                             : Tk.Theme.size.controlSm * 2
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.role: Accessible.Slider
    Accessible.name: tooltip.length ? tooltip : parameterId.length ? parameterId : qsTr("Audio fader")
    Accessible.description: qsTr("%1, unity at %2 %3, %4").arg(valueText).arg(unityValue).arg(unit).arg(automationState)
    Accessible.onIncreaseAction: control.step(1, Qt.NoModifier)
    Accessible.onDecreaseAction: control.step(-1, Qt.NoModifier)

    // Engine binding: the parameter's own range, units and state.
    Binding { target: control; property: "value"; when: control.binding !== null; value: control.binding ? control.binding.value : 0 }
    Binding { target: control; property: "silent"; when: control.binding !== null; value: control.binding ? control.binding.silent : false }
    Binding { target: control; property: "from"; when: control.binding !== null; value: control.binding ? control.binding.minimum : -60 }
    Binding { target: control; property: "to"; when: control.binding !== null; value: control.binding ? control.binding.maximum : 6 }
    Binding { target: control; property: "defaultValue"; when: control.binding !== null; value: control.binding ? control.binding.resetValue : 0 }
    Binding { target: control; property: "unit"; when: control.binding !== null; value: control.binding ? control.binding.unit : "" }
    Binding { target: control; property: "taper"; when: control.binding !== null; value: control.binding ? control.binding.taper : "linear" }
    Binding { target: control; property: "automationState"; when: control.binding !== null; value: control.binding ? control.binding.automationState : "" }
    Binding { target: control; property: "parameterId"; when: control.binding !== null; value: control.binding ? control.binding.parameterId : "" }
    Binding { target: control; property: "silenceMark"; when: control.binding !== null; value: control.binding ? control.binding.silenceFloor : false }

    function clamp(v) { return Math.max(from, Math.min(to, v)) }
    // Travel position (0..1) of a canonical value: the mapper's when one is
    // supplied, so marks, the unity line and the thumb share one calibration.
    function markPosition(v, isSilent) {
        if (activeMapper) return activeMapper.positionFor(v, !!isSilent)
        if (isSilent) return 0
        return Math.max(0, Math.min(1, (v - from) / Math.max(0.000001, to - from)))
    }
    // Pixel centre of a travel position along the track (the thumb's centre).
    function travelCentre(position) {
        return vertical ? track.y + thumb.height / 2 + (1 - position) * travel
                        : track.x + thumb.width / 2 + position * travel
    }
    function beginGesture(source) {
        if (_source === "wheel" && source !== "wheel") commitGesture() // another input ends a wheel burst
        if (_source.length) return false
        _source = source
        _startValue = value; _startSilent = silent
        _gestureBinding = binding
        if (_gestureBinding) _gestureBinding.begin(value, silent)
        gestureStarted(value, silent)
        return true
    }
    function updateUser(v, isSilent) {
        const next = clamp(v)
        if (next === value && isSilent === silent) return
        if (_gestureBinding) _gestureBinding.preview(next, isSilent)
        else if (!binding) { value = next; silent = isSilent }
        valuePreview(value, silent)
    }
    function commitGesture() {
        if (!_source.length) return
        _source = ""
        wheelIdle.stop()
        const target = _gestureBinding
        _gestureBinding = null
        if (target) target.commit(value, silent)
        gestureCommitted(value, silent)
    }
    function cancelGesture() {
        if (!_source.length) return
        _source = ""
        wheelIdle.stop()
        const target = _gestureBinding
        _gestureBinding = null
        if (target) target.cancel()
        else if (!binding) { value = _startValue; silent = _startSilent }
        gestureCanceled(value, silent)
    }
    function setUserPosition(position) {
        const p = Math.max(0, Math.min(1, position))
        if (activeMapper) {
            const mapped = activeMapper.valueAt(p)
            updateUser(mapped.value, mapped.silent)
        } else updateUser(from + p * (to - from), false)
    }
    function stepBy(direction, modifiers) {
        const scale = (modifiers & Qt.ShiftModifier) ? 10 : (modifiers & Qt.AltModifier) ? 0.1 : 1
        if (activeMapper) setUserPosition(normalizedValue + direction * positionStep * scale)
        else updateUser(value + direction * stepSize * scale, false)
    }
    // A step outside any open gesture is a gesture of its own.
    function step(direction, modifiers) {
        const own = beginGesture("step")
        stepBy(direction, modifiers)
        if (own) commitGesture()
    }
    function reset() {
        // A double-click reset joins the open pointer gesture: one transaction.
        if (_source === "pointer") { updateUser(defaultValue, false); return }
        const own = beginGesture("reset")
        updateUser(defaultValue, false)
        if (own) commitGesture()
    }
    function keyStep(direction, event) {
        if (_source !== "key") beginGesture("key")
        if (_source === "key") stepBy(direction, event.modifiers)
        event.accepted = true
    }
    onBindingChanged: if (_source.length && _gestureBinding && _gestureBinding !== binding) cancelGesture()
    Component.onDestruction: if (_source.length && _gestureBinding) _gestureBinding.cancel()
    Keys.onUpPressed: (event) => keyStep(1, event)
    Keys.onRightPressed: (event) => keyStep(1, event)
    Keys.onDownPressed: (event) => keyStep(-1, event)
    Keys.onLeftPressed: (event) => keyStep(-1, event)
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
            setUserPosition(event.key === Qt.Key_Home ? 0 : 1)
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

    Item {
        id: trackBox
        anchors.fill: parent
        anchors.bottomMargin: control.numericEntry ? Tk.Theme.size.control : 0
        Rectangle {
            id: track
            objectName: "faderTrack"
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
            height: control.vertical ? Math.max(0, track.y + track.height - y) : track.height
            color: Tk.Theme.alpha(Tk.Theme.color.accent, 0.45)
            visible: !control.silent
        }
        Repeater {
            model: control.scaleMarks
            delegate: Item {
                id: mark
                required property real modelData
                readonly property real centre: control.travelCentre(control.markPosition(modelData, false))
                readonly property bool unity: modelData === control.unityValue
                visible: modelData >= control.from && modelData <= control.to
                x: control.vertical ? track.x + track.width + Tk.Theme.space.sm : mark.centre
                y: control.vertical ? mark.centre : track.y + track.height + Tk.Theme.space.sm
                Rectangle {
                    x: control.vertical ? 0 : -Tk.Theme.space.xs / 2
                    y: control.vertical ? -Tk.Theme.space.xs / 2 : 0
                    width: control.vertical ? Tk.Theme.space.sm : Tk.Theme.space.xs
                    height: control.vertical ? Tk.Theme.space.xs : Tk.Theme.space.sm
                    color: mark.unity ? Tk.Theme.color.accent : Tk.Theme.color.borderStrong
                }
                Tk.Mono {
                    objectName: "faderScaleLabel"
                    x: control.vertical ? Tk.Theme.space.md : -implicitWidth / 2
                    y: control.vertical ? -implicitHeight / 2 : Tk.Theme.space.sm
                    text: control.silenceMark && mark.modelData <= control.from ? "−∞"
                                                                              : Number(mark.modelData).toFixed(0)
                    color: mark.unity ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    Accessible.ignored: true
                }
            }
        }
        Rectangle {
            id: unityMark
            objectName: "faderUnityMark"
            readonly property real centre: control.travelCentre(control.markPosition(control.unityValue, false))
            visible: control.unityValue >= control.from && control.unityValue <= control.to
            x: control.vertical ? track.x - Tk.Theme.space.xs : unityMark.centre - width / 2
            y: control.vertical ? unityMark.centre - height / 2 : track.y - Tk.Theme.space.xs
            width: control.vertical ? track.width + Tk.Theme.space.md : Tk.Theme.space.xs
            height: control.vertical ? Tk.Theme.space.xs : track.height + Tk.Theme.space.md
            color: Tk.Theme.color.warning
            z: 1
        }
        Rectangle {
            id: thumb
            objectName: "faderThumb"
            width: Tk.Theme.size.grip; height: Tk.Theme.size.controlSm
            x: control.vertical ? track.x + (track.width - width) / 2 : control.travelCentre(control.normalizedValue) - width / 2
            y: control.vertical ? control.travelCentre(control.normalizedValue) - height / 2 : track.y + (track.height - height) / 2
            radius: Tk.Theme.radius.sm
            color: control._dragging ? Tk.Theme.color.accent : Tk.Theme.color.panelAlt
            border.width: Tk.Theme.size.border; border.color: control.enabled ? Tk.Theme.color.borderStrong : Tk.Theme.color.textMuted
            z: 2
            Rectangle { anchors.fill: parent; anchors.margins: -Tk.Theme.size.focusRing; color: "transparent"; border.width: Tk.Theme.size.focusRing; border.color: Tk.Theme.color.focus; visible: control.visualFocus }
        }
        MouseArea {
            anchors.fill: parent
            // Relative drag along the track: travel pixels per full range,
            // Shift ×0.1, Alt ×0.01; the anchor moves when the modifier
            // changes so the level never jumps.
            function factorFor(modifiers) {
                return modifiers & Qt.ShiftModifier ? 0.1 : modifiers & Qt.AltModifier ? 0.01 : 1
            }
            function along(mouse) { return control.vertical ? -mouse.y : mouse.x }
            onPressed: (mouse) => {
                control.forceActiveFocus()
                if (!control.beginGesture("pointer")) return
                control._anchor = along(mouse)
                control._anchorPosition = control.normalizedValue
                control._dragFactor = factorFor(mouse.modifiers)
            }
            onPositionChanged: (mouse) => {
                if (!pressed || !control._dragging) return
                const factor = factorFor(mouse.modifiers)
                if (factor !== control._dragFactor) {
                    control._anchor = along(mouse)
                    control._anchorPosition = control.normalizedValue
                    control._dragFactor = factor
                }
                control.setUserPosition(control._anchorPosition + (along(mouse) - control._anchor) / control.travel * factor)
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
        objectName: "faderField"
        visible: control.numericEntry && !control.silent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        from: control.from; to: control.to; defaultValue: control.defaultValue
        stepSize: control.stepSize; decimals: 1; suffix: control.unit.length ? " " + control.unit : ""
        tooltip: control.tooltip; Accessible.name: control.Accessible.name
        onValueModified: (v) => {
            const own = control.beginGesture("text")
            control.updateUser(v, false)
            if (own) control.commitGesture()
        }
    }
    // AGENT-GUARD: NumberField assigns its own `value` on a typed commit; the
    // Binding element re-asserts the fader's value so the field never goes stale.
    Binding { target: field; property: "value"; value: control.value }
    Tk.Mono {
        objectName: "faderValue"
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
