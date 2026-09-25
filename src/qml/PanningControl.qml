// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (WP-062, docs/controls.md, docs/audio-mixer.md): pan, stereo
// width and depth in canonical coordinates: pan -1 (left) .. +1 (right),
// width 0 (mono) .. 2, depth -1 (rear) .. +1 (front), snapped to 0.01; the
// law, lane order and layouts belong to the engine (AudioMixer, D-111).
// `destinationLayout` is the layout the channel feeds: into Quad or 5.1 the
// pad is a spatial position (x pan, y depth) over that layout's speakers;
// otherwise its y is the stereo width. Bound (`mixer` + `channelId`, or
// `panBinding`/`widthBinding`/`depthBinding`): sliders, numeric fields and the
// pad are the same AudioParameterBinding transactions, one per gesture, and a
// pad drag commits both of its parameters as one undo step. Host mode keeps
// the request protocol: begin/preview/commit/cancel by stable IDs,
// acknowledged by the host. objectNames: "panSlider", "panField",
// "stereoWidthSlider", "widthField", "depthSlider", "depthField", "panPad",
// "panPadHandle", "panSpeaker", "panRequestStatus".
T.Control {
    id: control
    objectName: "panningControl"
    property var mixer: null // Tk.AudioMixerModel
    property string channelId: ""
    property string destinationLayout: "Stereo" // "Quad" or "5.1": spatial panning
    property var panBinding: mixer && channelId.length ? mixer.parameterBinding(channelId, "pan") : null
    property var widthBinding: mixer && channelId.length && hasWidth ? mixer.parameterBinding(channelId, "width") : null
    property var depthBinding: mixer && channelId.length && spatial ? mixer.parameterBinding(channelId, "depth") : null
    property string panParameterId: ""
    property string widthParameterId: ""
    property string depthParameterId: ""
    property real panValue: 0
    property real stereoWidth: 1
    property real depthValue: 1
    property string speakerLayout: "Stereo" // the source layout; a mono source has no width
    property bool pad: true
    property string tooltip: qsTr("Pan and stereo width")
    property string _serial: "0"
    // AGENT-GUARD (WP-062): the binding a gesture began on receives all of it.
    // When a binding changes (another channelId, a recycled strip) its open
    // gesture is canceled where it began, so a commit never lands on another
    // channel and the old lane is not left held.
    property var _gestures: ({pan: null, width: null, depth: null}) // kind -> {id, binding}
    property string _lastRequestId: ""
    property string _requestState: "idle"
    property string _requestMessage: ""
    readonly property string requestState: _requestState
    readonly property string requestMessage: _requestMessage
    readonly property bool hasWidth: speakerLayout === "Stereo"
    readonly property bool spatial: destinationLayout === "Quad" || destinationLayout === "5.1"
    readonly property real clampedPan: Math.max(-1, Math.min(1, panValue))
    readonly property real clampedWidth: Math.max(0, Math.min(2, stereoWidth))
    readonly property real clampedDepth: Math.max(-1, Math.min(1, depthValue))
    readonly property real shownPan: panBinding ? panBinding.value : clampedPan
    readonly property real shownWidth: widthBinding ? widthBinding.value : clampedWidth
    readonly property real shownDepth: depthBinding ? depthBinding.value : clampedDepth
    readonly property string panText: Math.abs(shownPan) < 0.005 ? qsTr("C")
        : (shownPan < 0 ? qsTr("L%1") : qsTr("R%1")).arg(Math.round(Math.abs(shownPan) * 100))
    // Speakers drawn on a spatial pad, at canonical (pan, depth) positions.
    readonly property var speakers: destinationLayout === "Quad"
        ? [{name: "L", x: -1, y: 1}, {name: "R", x: 1, y: 1}, {name: "Ls", x: -1, y: -1}, {name: "Rs", x: 1, y: -1}]
        : destinationLayout === "5.1"
          ? [{name: "L", x: -1, y: 1}, {name: "C", x: 0, y: 1}, {name: "R", x: 1, y: 1},
             {name: "Ls", x: -1, y: -1}, {name: "Rs", x: 1, y: -1}]
          : []

    signal parameterEditRequested(string channelId, string parameterId,
                                  string gestureId, string phase, real value)
    signal editAcknowledged(string gestureId, bool accepted, string message)

    implicitWidth: Tk.Theme.size.fieldWidth * 2
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: (hasWidth ? qsTr("%1 pan %2, width %3 percent").arg(speakerLayout).arg(panText)
                                            .arg(Math.round(shownWidth * 100))
                                      : qsTr("%1 pan %2").arg(speakerLayout).arg(panText))
        + (spatial ? qsTr(", depth %1 percent into %2").arg(Math.round(shownDepth * 100)).arg(destinationLayout) : "")

    function snap(v, lo, hi) { return Math.max(lo, Math.min(hi, Math.round(v * 100) / 100)) }
    // Canonical pad coordinates: x is the pan; y is the depth (top front +1,
    // bottom rear -1) on a spatial pad, else the width (top 2, bottom 0).
    function panAt(x) { return snap(-1 + 2 * x / Math.max(1, padArea.width), -1, 1) }
    function widthAt(y) { return snap(2 * (1 - y / Math.max(1, padArea.height)), 0, 2) }
    function depthAt(y) { return snap(1 - 2 * y / Math.max(1, padArea.height), -1, 1) }
    function padKind() { return spatial ? "depth" : hasWidth ? "width" : "" }
    function bindingFor(kind) { return kind === "width" ? widthBinding : kind === "depth" ? depthBinding : panBinding }
    function parameterIdFor(kind) {
        const binding = bindingFor(kind)
        if (binding) return binding.parameterId
        return kind === "width" ? widthParameterId : kind === "depth" ? depthParameterId : panParameterId
    }
    function shownFor(kind) { return kind === "width" ? shownWidth : kind === "depth" ? shownDepth : shownPan }
    function hostFor(kind) { return kind === "width" ? clampedWidth : kind === "depth" ? clampedDepth : clampedPan }
    function gesture(kind) { return _gestures[kind] }
    function setGesture(kind, value) {
        const next = Object.assign({}, _gestures)
        next[kind] = value
        _gestures = next
    }
    function nextId(parameterId) {
        _serial = String(Number(_serial) + 1)
        return channelId + ":" + parameterId + ":" + _serial
    }
    function report(binding, ok) {
        _requestState = ok ? "accepted" : "rejected"
        _requestMessage = ok ? qsTr("%1: %2").arg(binding.name).arg(binding.valueText) : binding.lastError
    }
    function begin(kind) {
        const parameterId = parameterIdFor(kind)
        const id = nextId(parameterId)
        const binding = bindingFor(kind)
        setGesture(kind, {id: id, binding: binding})
        _lastRequestId = id
        if (binding) {
            binding.begin(shownFor(kind), false)
            return id
        }
        _requestState = "pending"
        _requestMessage = qsTr("Edit pending")
        parameterEditRequested(channelId, parameterId, id, "begin", hostFor(kind))
        return id
    }
    function update(kind, value) {
        let open = gesture(kind)
        if (!open && bindingFor(kind)) return // dropped with its binding: the drag no longer edits
        if (!open) { begin(kind); open = gesture(kind) }
        _lastRequestId = open.id
        if (open.binding) open.binding.preview(Number(value), false)
        else parameterEditRequested(channelId, parameterIdFor(kind), open.id, "preview", Number(value))
    }
    function finish(kind, value) {
        let open = gesture(kind)
        if (!open) { begin(kind); open = gesture(kind) }
        _lastRequestId = open.id
        setGesture(kind, null)
        if (open.binding) report(open.binding, open.binding.commit(Number(value), false))
        else parameterEditRequested(channelId, parameterIdFor(kind), open.id, "commit", Number(value))
    }
    // Cancels one open gesture on the binding (or host) it began with.
    function drop(kind) {
        const open = gesture(kind)
        if (!open) return
        setGesture(kind, null)
        if (open.binding) open.binding.cancel()
        else if (!bindingFor(kind)) parameterEditRequested(channelId, parameterIdFor(kind), open.id, "cancel", hostFor(kind))
    }
    function cancel() {
        drop("pan")
        drop("width")
        drop("depth")
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
    function padValue(y) { return spatial ? depthAt(y) : widthAt(y) }
    // The pad: one gesture over pan and its y parameter.
    function padBegin(x, y) {
        begin("pan")
        if (padKind().length) begin(padKind())
        padMove(x, y)
    }
    function padMove(x, y) {
        if (!gesture("pan") && !(padKind().length && gesture(padKind()))) return // dropped
        update("pan", panAt(x))
        if (padKind().length) update(padKind(), padValue(y))
    }
    function padEnd(x, y) {
        const kind = padKind()
        const panOpen = gesture("pan")
        const otherOpen = kind.length ? gesture(kind) : null
        if (!panOpen && !otherOpen) return // dropped (the binding changed)
        if (panOpen && otherOpen && panOpen.binding && otherOpen.binding) {
            setGesture("pan", null)
            setGesture(kind, null)
            report(panOpen.binding, panOpen.binding.commitWith(panAt(x), otherOpen.binding, padValue(y)))
            return
        }
        if (panOpen) finish("pan", panAt(x))
        if (otherOpen) finish(kind, padValue(y))
    }
    function sliderPressed(kind, pressed, value) {
        if (pressed) begin(kind)
        else if (gesture(kind)) finish(kind, value)
    }
    function sliderMoved(kind, pressed, value) {
        if (pressed) update(kind, value)
        else finish(kind, value)
    }
    onPanBindingChanged: { const open = gesture("pan"); if (open && open.binding && open.binding !== panBinding) drop("pan") }
    onWidthBindingChanged: { const open = gesture("width"); if (open && open.binding && open.binding !== widthBinding) drop("width") }
    onDepthBindingChanged: { const open = gesture("depth"); if (open && open.binding && open.binding !== depthBinding) drop("depth") }
    Component.onDestruction: {
        for (const kind of ["pan", "width", "depth"]) {
            const open = _gestures[kind]
            if (open && open.binding) open.binding.cancel()
        }
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
                value: Math.round(control.shownPan * 100)
                width: Tk.Theme.size.labelWidth / 2
                small: true
                from: -100; to: 100; stepSize: 1; decimals: 0; suffix: "%"
                stepButtons: false
                tooltip: qsTr("Pan in percent: -100 left, 0 centre, 100 right")
                Accessible.name: qsTr("%1 pan").arg(control.tooltip)
                onValueModified: (v) => control.finish("pan", v / 100)
            }
        }
        Tk.Slider {
            id: panSlider
            objectName: "panSlider"
            width: parent.width
            from: -1; to: 1; stepSize: 0.01
            showValue: false
            tooltip: control.tooltip + qsTr(" · pan")
            onPressedChanged: control.sliderPressed("pan", pressed, value)
            onValueModified: (moved) => control.sliderMoved("pan", panSlider.pressed, moved)
        }
        Tk.Flex {
            width: parent.width
            visible: control.hasWidth
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Width · %1").arg(control.speakerLayout); color: Tk.Theme.color.textMuted; Tk.Flex.grow: 1 }
            Tk.NumberField {
                id: widthField
                objectName: "widthField"
                value: Math.round(control.shownWidth * 100)
                width: Tk.Theme.size.labelWidth / 2
                small: true
                from: 0; to: 200; stepSize: 1; decimals: 0; suffix: "%"
                stepButtons: false
                tooltip: qsTr("Stereo width in percent: 0 mono, 100 as recorded, 200 widest")
                Accessible.name: qsTr("%1 width").arg(control.tooltip)
                onValueModified: (v) => control.finish("width", v / 100)
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
            onPressedChanged: control.sliderPressed("width", pressed, value)
            onValueModified: (moved) => control.sliderMoved("width", widthSlider.pressed, moved)
        }
        Tk.Flex {
            width: parent.width
            visible: control.spatial
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Depth · %1").arg(control.destinationLayout); color: Tk.Theme.color.textMuted; Tk.Flex.grow: 1 }
            Tk.NumberField {
                id: depthField
                objectName: "depthField"
                value: Math.round(control.shownDepth * 100)
                width: Tk.Theme.size.labelWidth / 2
                small: true
                from: -100; to: 100; stepSize: 1; decimals: 0; suffix: "%"
                stepButtons: false
                tooltip: qsTr("Depth in percent: 100 front, 0 middle, -100 rear")
                Accessible.name: qsTr("%1 depth").arg(control.tooltip)
                onValueModified: (v) => control.finish("depth", v / 100)
            }
        }
        Tk.Slider {
            id: depthSlider
            objectName: "depthSlider"
            width: parent.width
            visible: control.spatial
            from: -1; to: 1; stepSize: 0.01
            showValue: false
            tooltip: control.tooltip + qsTr(" · depth (rear to front)")
            onPressedChanged: control.sliderPressed("depth", pressed, value)
            onValueModified: (moved) => control.sliderMoved("depth", depthSlider.pressed, moved)
        }
        Item {
            id: padArea
            objectName: "panPad"
            visible: control.pad
            width: parent.width
            // Tk.Flex sizes a child from its implicit size; a plain `height` is ignored.
            // The same height on a spatial pad: its sliders and depth row already
            // make the strip taller, and a 5.1 strip must still fit a short mixer.
            implicitHeight: Tk.Theme.size.controlLg * 2
            Accessible.role: Accessible.Graphic
            Accessible.name: control.spatial ? qsTr("Position pad over %1 speakers").arg(control.destinationLayout)
                                             : qsTr("Pan position and width pad")
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
            Repeater {
                model: control.speakers
                delegate: Rectangle {
                    id: speaker
                    required property var modelData
                    objectName: "panSpeaker"
                    width: Tk.Theme.size.grip
                    height: Tk.Theme.size.grip
                    radius: Tk.Theme.radius.sm
                    x: (speaker.modelData.x + 1) / 2 * (padArea.width - width)
                    y: (1 - speaker.modelData.y) / 2 * (padArea.height - height)
                    color: Tk.Theme.color.surface
                    border.width: Tk.Theme.size.border
                    border.color: Tk.Theme.color.textMuted
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Speaker %1").arg(speaker.modelData.name)
                    Tk.ToolTip { visible: speakerHover.hovered; text: speaker.modelData.name }
                    HoverHandler { id: speakerHover }
                }
            }
            Rectangle {
                objectName: "panPadHandle"
                width: Tk.Theme.size.grip
                height: Tk.Theme.size.grip
                radius: Tk.Theme.radius.full
                x: (control.shownPan + 1) / 2 * padArea.width - width / 2
                y: (control.spatial ? (1 - control.shownDepth) / 2 : 1 - (control.hasWidth ? control.shownWidth : 1) / 2)
                   * padArea.height - height / 2
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
    // AGENT-GUARD: the sliders assign their own value while edited (the
    // NumberFields never do, D-198, and bind plainly above); these re-assert
    // the engine value whenever no edit holds them. A press
    // (pointer or arrow key) must leave the slider where it is: the default
    // restoreMode would put back the slider's value from before the first
    // binding (0), and the key step would start from there.
    Binding { target: panSlider; property: "value"; value: control.shownPan; when: !panSlider.pressed; restoreMode: Binding.RestoreNone }
    Binding { target: widthSlider; property: "value"; value: control.shownWidth; when: !widthSlider.pressed; restoreMode: Binding.RestoreNone }
    Binding { target: depthSlider; property: "value"; value: control.shownDepth; when: !depthSlider.pressed; restoreMode: Binding.RestoreNone }
    Tk.ToolTip { visible: control.hovered && control.tooltip.length > 0; text: control.tooltip }
}
