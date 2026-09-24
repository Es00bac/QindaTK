// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: panning values/law/layout come from the host. Gesture phases
// target stable channel/parameter IDs; only host acknowledgments mark them
// accepted or rejected, and this control never evaluates audio panning.
T.Control {
    id: control
    objectName: "panningControl"
    property string channelId: ""
    property string panParameterId: ""
    property string widthParameterId: ""
    property real panValue: 0
    property real stereoWidth: 1
    property string speakerLayout: "Stereo"
    property string tooltip: qsTr("Pan and stereo width")
    property string _serial: "0"
    property string _panGestureId: ""
    property string _widthGestureId: ""
    property string _lastRequestId: ""
    property string _requestState: "idle"
    property string _requestMessage: ""
    readonly property string requestState: _requestState
    readonly property string requestMessage: _requestMessage
    readonly property real clampedPan: Math.max(-1, Math.min(1, panValue))
    readonly property real clampedWidth: Math.max(0, Math.min(2, stereoWidth))

    signal parameterEditRequested(string channelId, string parameterId,
                                  string gestureId, string phase, real value)
    signal editAcknowledged(string gestureId, bool accepted, string message)

    implicitWidth: Tk.Theme.size.fieldWidth * 2
    implicitHeight: Tk.Theme.size.row * 4 + Tk.Theme.space.sm * 2
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 pan %2, width %3")
            .arg(speakerLayout).arg(Math.round(clampedPan * 100)).arg(Math.round(clampedWidth * 100))

    function nextId(parameterId) {
        _serial = String(Number(_serial) + 1)
        return channelId + ":" + parameterId + ":" + _serial
    }
    function begin(parameterId, widthEdit) {
        const id = nextId(parameterId)
        if (widthEdit) _widthGestureId = id
        else _panGestureId = id
        _lastRequestId = id
        _requestState = "pending"
        _requestMessage = qsTr("Edit pending")
        parameterEditRequested(channelId, parameterId, id, "begin", widthEdit ? clampedWidth : clampedPan)
        return id
    }
    function update(parameterId, value, widthEdit) {
        let id = widthEdit ? _widthGestureId : _panGestureId
        if (!id) id = begin(parameterId, widthEdit)
        _lastRequestId = id
        parameterEditRequested(channelId, parameterId, id, "preview", Number(value))
    }
    function finish(parameterId, value, widthEdit) {
        let id = widthEdit ? _widthGestureId : _panGestureId
        if (!id) id = begin(parameterId, widthEdit)
        _lastRequestId = id
        parameterEditRequested(channelId, parameterId, id, "commit", Number(value))
        if (widthEdit) _widthGestureId = ""
        else _panGestureId = ""
    }
    function cancel() {
        if (_panGestureId) parameterEditRequested(channelId, panParameterId, _panGestureId, "cancel", clampedPan)
        if (_widthGestureId) parameterEditRequested(channelId, widthParameterId, _widthGestureId, "cancel", clampedWidth)
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
    Keys.onEscapePressed: function(event) { control.cancel(); event.accepted = true }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            Tk.Caption { text: qsTr("Pan"); color: Tk.Theme.color.textMuted }
            Tk.Spacer { }
            Tk.Mono { text: Math.round(control.clampedPan * 100) + "%"; Accessible.ignored: true }
        }
        Tk.Slider {
            id: panSlider
            objectName: "panSlider"
            width: parent.width
            from: -1; to: 1; stepSize: 0.01
            value: control.clampedPan
            showValue: false
            tooltip: control.tooltip + qsTr(" · pan")
            onPressedChanged: if (pressed) control.begin(control.panParameterId, false)
                              else if (control._panGestureId) control.finish(control.panParameterId, value, false)
            onValueModified: if (pressed) control.update(control.panParameterId, value, false)
                             else control.finish(control.panParameterId, value, false)
        }
        Tk.Flex {
            width: parent.width
            Tk.Caption { text: qsTr("Stereo width · %1").arg(control.speakerLayout); color: Tk.Theme.color.textMuted }
            Tk.Spacer { }
            Tk.Mono { text: Math.round(control.clampedWidth * 100) + "%"; Accessible.ignored: true }
        }
        Tk.Slider {
            id: widthSlider
            objectName: "stereoWidthSlider"
            width: parent.width
            from: 0; to: 2; stepSize: 0.01
            value: control.clampedWidth
            tooltip: control.tooltip + qsTr(" · stereo width")
            onPressedChanged: if (pressed) control.begin(control.widthParameterId, true)
                              else if (control._widthGestureId) control.finish(control.widthParameterId, value, true)
            onValueModified: if (pressed) control.update(control.widthParameterId, value, true)
                             else control.finish(control.widthParameterId, value, true)
        }
        Tk.Caption {
            objectName: "panRequestStatus"
            visible: control._requestMessage.length > 0
            text: control._requestMessage
            color: control._requestState === "rejected" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
    }
    Tk.ToolTip { visible: control.hovered && control.tooltip.length > 0; text: control.tooltip }
}
