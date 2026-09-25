// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (WP-062, docs/controls.md, docs/audio-mixer.md): pan and
// stereo width in canonical coordinates, pan -1 (left) .. +1 (right) and width
// 0 (mono) .. 2, snapped to 0.01; the law, lane order and layouts belong to
// the engine (AudioMixer, D-111). Bound (`mixer` + `channelId`, or
// `panBinding`/`widthBinding`): the sliders, the numeric fields and the pad are
// the same AudioParameterBinding transactions, one per gesture, and a pad drag
// commits pan and width as one undo step. Host mode keeps the request
// protocol: begin/preview/commit/cancel by stable IDs, acknowledged by the
// host. objectNames: "panSlider", "panField", "stereoWidthSlider",
// "widthField", "panPad", "panPadHandle", "panRequestStatus".
T.Control {
    id: control
    objectName: "panningControl"
    property var mixer: null // Tk.AudioMixerModel
    property string channelId: ""
    property var panBinding: mixer && channelId.length ? mixer.parameterBinding(channelId, "pan") : null
    property var widthBinding: mixer && channelId.length && hasWidth ? mixer.parameterBinding(channelId, "width") : null
    property string panParameterId: ""
    property string widthParameterId: ""
    property real panValue: 0
    property real stereoWidth: 1
    property string speakerLayout: "Stereo" // the source layout; a mono source has no width
    property bool pad: true
    property string tooltip: qsTr("Pan and stereo width")
    property string _serial: "0"
    property string _panGestureId: ""
    property string _widthGestureId: ""
    property string _lastRequestId: ""
    property string _requestState: "idle"
    property string _requestMessage: ""
    readonly property string requestState: _requestState
    readonly property string requestMessage: _requestMessage
    readonly property bool hasWidth: speakerLayout === "Stereo"
    readonly property real clampedPan: Math.max(-1, Math.min(1, panValue))
    readonly property real clampedWidth: Math.max(0, Math.min(2, stereoWidth))
    readonly property real shownPan: panBinding ? panBinding.value : clampedPan
    readonly property real shownWidth: widthBinding ? widthBinding.value : clampedWidth
    readonly property string panText: Math.abs(shownPan) < 0.005 ? qsTr("C")
        : (shownPan < 0 ? qsTr("L%1") : qsTr("R%1")).arg(Math.round(Math.abs(shownPan) * 100))

    signal parameterEditRequested(string channelId, string parameterId,
                                  string gestureId, string phase, real value)
    signal editAcknowledged(string gestureId, bool accepted, string message)

    implicitWidth: Tk.Theme.size.fieldWidth * 2
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: hasWidth ? qsTr("%1 pan %2, width %3 percent").arg(speakerLayout).arg(panText)
                                           .arg(Math.round(shownWidth * 100))
                                     : qsTr("%1 pan %2").arg(speakerLayout).arg(panText)

    function snap(v, lo, hi) { return Math.max(lo, Math.min(hi, Math.round(v * 100) / 100)) }
    // Canonical pad coordinates: x is the pan position, y the width (top 2, bottom 0).
    function panAt(x) { return snap(-1 + 2 * x / Math.max(1, padArea.width), -1, 1) }
    function widthAt(y) { return snap(2 * (1 - y / Math.max(1, padArea.height)), 0, 2) }
    function panId() { return panBinding ? panBinding.parameterId : panParameterId }
    function widthId() { return widthBinding ? widthBinding.parameterId : widthParameterId }
    function bindingFor(widthEdit) { return widthEdit ? widthBinding : panBinding }
    function nextId(parameterId) {
        _serial = String(Number(_serial) + 1)
        return channelId + ":" + parameterId + ":" + _serial
    }
    function report(binding, ok) {
        _requestState = ok ? "accepted" : "rejected"
        _requestMessage = ok ? qsTr("%1: %2").arg(binding.name).arg(binding.valueText) : binding.lastError
    }
    function begin(parameterId, widthEdit) {
        const id = nextId(parameterId)
        if (widthEdit) _widthGestureId = id
        else _panGestureId = id
        _lastRequestId = id
        const binding = bindingFor(widthEdit)
        if (binding) {
            binding.begin(widthEdit ? shownWidth : shownPan, false)
            return id
        }
        _requestState = "pending"
        _requestMessage = qsTr("Edit pending")
        parameterEditRequested(channelId, parameterId, id, "begin", widthEdit ? clampedWidth : clampedPan)
        return id
    }
    function update(parameterId, value, widthEdit) {
        let id = widthEdit ? _widthGestureId : _panGestureId
        if (!id) id = begin(parameterId, widthEdit)
        _lastRequestId = id
        const binding = bindingFor(widthEdit)
        if (binding) binding.preview(Number(value), false)
        else parameterEditRequested(channelId, parameterId, id, "preview", Number(value))
    }
    function finish(parameterId, value, widthEdit) {
        let id = widthEdit ? _widthGestureId : _panGestureId
        if (!id) id = begin(parameterId, widthEdit)
        _lastRequestId = id
        if (widthEdit) _widthGestureId = ""
        else _panGestureId = ""
        const binding = bindingFor(widthEdit)
        if (binding) report(binding, binding.commit(Number(value), false))
        else parameterEditRequested(channelId, parameterId, id, "commit", Number(value))
    }
    function cancel() {
        if (_panGestureId) {
            if (panBinding) panBinding.cancel()
            else parameterEditRequested(channelId, panParameterId, _panGestureId, "cancel", clampedPan)
        }
        if (_widthGestureId) {
            if (widthBinding) widthBinding.cancel()
            else parameterEditRequested(channelId, widthParameterId, _widthGestureId, "cancel", clampedWidth)
        }
        _panGestureId = ""
        _widthGestureId = ""
        _requestState = "cancelled"
        _requestMessage = qsTr("Edit canceled")
    }
    function acknowledgeEdit(gestureId, accepted, message) {
        if (!gestureId || gestureId !== _lastRequestId) return false
        _requestState = accepted ? "accepted" : "rejected"
        _requestMessage = String(message || (accepted ? qsTr("Accepted") : qsTr("Rejected")))
        editAcknowledged(String(gestureId), !!accepted, _requestMessage)
        return true
    }
    // The pad: one gesture over both parameters.
    function padBegin(x, y) {
        begin(panId(), false)
        if (hasWidth) begin(widthId(), true)
        padMove(x, y)
    }
    function padMove(x, y) {
        update(panId(), panAt(x), false)
        if (hasWidth) update(widthId(), widthAt(y), true)
    }
    function padEnd(x, y) {
        const pan = panAt(x)
        const width = widthAt(y)
        if (panBinding && hasWidth && widthBinding) {
            _panGestureId = ""
            _widthGestureId = ""
            report(panBinding, panBinding.commitWith(pan, widthBinding, width))
            return
        }
        finish(panId(), pan, false)
        if (hasWidth) finish(widthId(), width, true)
    }
    Keys.onEscapePressed: function(event) { control.cancel(); event.accepted = true }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        id: body
        anchors.fill: parent
        anchors.margins: control.padding
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Pan %1").arg(control.panText); color: Tk.Theme.color.textMuted; Tk.Flex.grow: 1 }
            Tk.NumberField {
                id: panField
                objectName: "panField"
                width: Tk.Theme.size.labelWidth / 2
                small: true
                from: -100; to: 100; stepSize: 1; decimals: 0; suffix: "%"
                stepButtons: false
                tooltip: qsTr("Pan in percent: -100 left, 0 centre, 100 right")
                Accessible.name: qsTr("%1 pan").arg(control.tooltip)
                onValueModified: (v) => control.finish(control.panId(), v / 100, false)
            }
        }
        Tk.Slider {
            id: panSlider
            objectName: "panSlider"
            width: parent.width
            from: -1; to: 1; stepSize: 0.01
            showValue: false
            tooltip: control.tooltip + qsTr(" · pan")
            onPressedChanged: if (pressed) control.begin(control.panId(), false)
                              else if (control._panGestureId) control.finish(control.panId(), value, false)
            onValueModified: (moved) => {
                if (panSlider.pressed) control.update(control.panId(), moved, false)
                else control.finish(control.panId(), moved, false)
            }
        }
        Tk.Flex {
            width: parent.width
            visible: control.hasWidth
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Width · %1").arg(control.speakerLayout); color: Tk.Theme.color.textMuted; Tk.Flex.grow: 1 }
            Tk.NumberField {
                id: widthField
                objectName: "widthField"
                width: Tk.Theme.size.labelWidth / 2
                small: true
                from: 0; to: 200; stepSize: 1; decimals: 0; suffix: "%"
                stepButtons: false
                tooltip: qsTr("Stereo width in percent: 0 mono, 100 as recorded, 200 widest")
                Accessible.name: qsTr("%1 width").arg(control.tooltip)
                onValueModified: (v) => control.finish(control.widthId(), v / 100, true)
            }
        }
        Tk.Slider {
            id: widthSlider
            objectName: "stereoWidthSlider"
            width: parent.width
            visible: control.hasWidth
            from: 0; to: 2; stepSize: 0.01
            showValue: false
            tooltip: control.tooltip + qsTr(" · stereo width")
            onPressedChanged: if (pressed) control.begin(control.widthId(), true)
                              else if (control._widthGestureId) control.finish(control.widthId(), value, true)
            onValueModified: (moved) => {
                if (widthSlider.pressed) control.update(control.widthId(), moved, true)
                else control.finish(control.widthId(), moved, true)
            }
        }
        Item {
            id: padArea
            objectName: "panPad"
            visible: control.pad
            width: parent.width
            // Tk.Flex sizes a child from its implicit size; a plain `height` is ignored.
            implicitHeight: Tk.Theme.size.controlLg * 2
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Pan position and width pad")
            Accessible.description: control.Accessible.description
            Rectangle {
                anchors.fill: parent
                color: Tk.Theme.color.inputBg
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.border
                radius: Tk.Theme.radius.sm
            }
            Rectangle { x: parent.width / 2; width: Tk.Theme.size.border; height: parent.height; color: Tk.Theme.color.border }
            Rectangle { y: parent.height / 2; width: parent.width; height: Tk.Theme.size.border; color: Tk.Theme.color.border }
            Rectangle {
                objectName: "panPadHandle"
                width: Tk.Theme.size.grip
                height: Tk.Theme.size.grip
                radius: Tk.Theme.radius.full
                x: (control.shownPan + 1) / 2 * padArea.width - width / 2
                y: (1 - (control.hasWidth ? control.shownWidth : 1) / 2) * padArea.height - height / 2
                color: Tk.Theme.color.accent
            }
            MouseArea {
                anchors.fill: parent
                onPressed: (mouse) => { control.forceActiveFocus(); control.padBegin(mouse.x, mouse.y) }
                onPositionChanged: (mouse) => { if (pressed) control.padMove(mouse.x, mouse.y) }
                onReleased: (mouse) => control.padEnd(mouse.x, mouse.y)
                onCanceled: control.cancel()
            }
        }
        Tk.Caption {
            objectName: "panRequestStatus"
            visible: control._requestMessage.length > 0
            width: parent.width
            elide: Text.ElideRight
            text: control._requestMessage
            color: control._requestState === "rejected" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
    }
    // AGENT-GUARD: the sliders and fields assign their own value while edited;
    // these re-assert the engine value whenever no edit holds them. A press
    // (pointer or arrow key) must leave the slider where it is: the default
    // restoreMode would put back the slider's value from before the first
    // binding (0), and the key step would start from there.
    Binding { target: panSlider; property: "value"; value: control.shownPan; when: !panSlider.pressed; restoreMode: Binding.RestoreNone }
    Binding { target: widthSlider; property: "value"; value: control.shownWidth; when: !widthSlider.pressed; restoreMode: Binding.RestoreNone }
    Binding { target: panField; property: "value"; value: Math.round(control.shownPan * 100) }
    Binding { target: widthField; property: "value"; value: Math.round(control.shownWidth * 100) }
    Tk.ToolTip { visible: control.hovered && control.tooltip.length > 0; text: control.tooltip }
}
