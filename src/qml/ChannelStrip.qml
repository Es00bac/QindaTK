// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: this strip is a view over host channel/parameter/meter
// snapshots. Gain and mute/solo operations target stable channel/parameter IDs;
// delegate state never owns routing, automation or engine values.
T.Control {
    id: strip
    objectName: "channelStrip"
    property string channelId: ""
    property string channelName: ""
    property string channelKind: "audio"
    property string gainParameterId: ""
    property real gainValue: 0
    property bool gainSilent: false
    property real gainMinimum: -60
    property real gainMaximum: 6
    property var gainMap: null
    property bool muted: false
    property bool soloed: false
    property real panValue: 0
    property real stereoWidth: 1
    property string speakerLayout: "Stereo"
    property bool selected: false
    property var meterModel: null
    property int meterChannel: 0
    property real peakDbfs: -120
    property real rmsDbfs: -120
    property string routeSummary: qsTr("No route details")
    property string commandState: "idle"
    property string commandMessage: ""
    property string tooltip: qsTr("Channel strip")
    property alias panningControl: pan
    property string _serial: "0"
    property string _gainGestureId: ""
    property string _lastGainRequestId: ""
    property string _lastCommandId: ""
    readonly property var measuredLevel: meterModel && meterModel.levels
            && meterChannel >= 0 && meterChannel < meterModel.levels.length
            ? meterModel.levels[meterChannel] : null
    readonly property real shownPeak: measuredLevel ? Number(measuredLevel.displayPeakDbfs) : peakDbfs
    readonly property real shownRms: measuredLevel ? Number(measuredLevel.rmsDbfs) : rmsDbfs
    readonly property real meterPosition: Math.max(0, Math.min(1, (shownPeak + 60) / 60))
    readonly property real sliderValue: gainMap ? gainMap.positionFor(gainValue, gainSilent) : gainValue
    readonly property string gainText: gainSilent ? "−∞ dB" : Number(gainValue).toFixed(1) + " dB"

    signal gainEditRequested(string channelId, string parameterId, string gestureId,
                             string phase, real value, bool silent)
    signal panEditRequested(string channelId, string parameterId, string gestureId,
                            string phase, real value)
    signal channelCommandRequested(string channelId, string requestId, string command, bool enabled)
    signal channelSelected(string channelId)

    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    hoverEnabled: true
    Accessible.role: Accessible.Grouping
    Accessible.name: channelName.length ? channelName : tooltip
    Accessible.description: qsTr("%1, %2, peak %3 dBFS, %4")
            .arg(channelKind).arg(gainText).arg(Number(shownPeak).toFixed(1)).arg(routeSummary)

    function nextId(kind) {
        _serial = String(Number(_serial) + 1)
        return channelId + ":" + kind + ":" + _serial
    }
    function canonical(position) {
        return gainMap ? gainMap.valueAt(position) : ({value: position, silent: false})
    }
    function sendGain(phase, position) {
        let id = _gainGestureId
        if (!id) id = channelId + ":" + gainParameterId + ":" + (Number(_serial) + 1)
        if (!_gainGestureId) _serial = String(Number(_serial) + 1)
        const result = canonical(position)
        _lastGainRequestId = id
        gainEditRequested(channelId, gainParameterId, id, phase, Number(result.value), !!result.silent)
        return id
    }
    function startGain() {
        if (_gainGestureId) return _gainGestureId
        _gainGestureId = nextId(gainParameterId)
        return sendGain("begin", fader.value)
    }
    function updateGain(position) {
        if (!_gainGestureId) startGain()
        return sendGain("preview", position)
    }
    function finishGain(position) {
        if (!_gainGestureId) startGain()
        const id = _gainGestureId
        sendGain("commit", position)
        _gainGestureId = ""
        return id
    }
    function cancelGain() {
        if (!_gainGestureId) return
        sendGain("cancel", fader.value)
        _gainGestureId = ""
    }
    function acknowledgeGainEdit(requestId, state, message) {
        if (!requestId || requestId !== _lastGainRequestId || ["accepted", "rejected", "error"].indexOf(state) < 0) return false
        commandState = String(state)
        commandMessage = String(message || (state === "accepted" ? qsTr("Gain accepted") : qsTr("Gain %1").arg(state)))
        return true
    }
    function requestCommand(command, value) {
        const id = nextId(command)
        _lastCommandId = id
        channelCommandRequested(channelId, id, command, !!value)
        return id
    }
    function acknowledgeCommand(requestId, state, message) {
        if (!requestId || requestId !== _lastCommandId || ["accepted", "rejected", "error"].indexOf(state) < 0) return false
        commandState = String(state)
        commandMessage = String(message || (state === "accepted" ? qsTr("Command accepted") : qsTr("Command %1").arg(state)))
        return true
    }
    Keys.onEscapePressed: function(event) { strip.cancelGain(); event.accepted = true }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.color: strip.selected ? Tk.Theme.color.accent : strip.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: strip.selected || strip.activeFocus ? Tk.Theme.size.focusRing : Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Label { text: strip.channelName; elide: Text.ElideRight; font.pixelSize: Tk.Theme.font.body }
            Tk.Spacer { }
            Tk.Caption { text: strip.channelKind.toUpperCase(); color: Tk.Theme.color.textMuted }
        }
        Tk.Flex {
            width: parent.width
            Tk.Caption { text: qsTr("Peak %1 dBFS").arg(Number(strip.shownPeak).toFixed(1)); color: Tk.Theme.color.textMuted }
            Tk.Spacer { }
            Tk.Caption { text: qsTr("RMS %1").arg(Number(strip.shownRms).toFixed(1)); color: Tk.Theme.color.textMuted }
        }
        Tk.ProgressBar {
            objectName: "channelPeakMeter"
            width: parent.width
            value: strip.meterPosition
            tooltip: qsTr("Measured channel peak")
            Accessible.description: qsTr("Peak %1 dBFS; RMS %2 dBFS")
                    .arg(Number(strip.shownPeak).toFixed(1)).arg(Number(strip.shownRms).toFixed(1))
        }
        Tk.Flex {
            width: parent.width
            Tk.Caption { text: qsTr("Gain"); color: Tk.Theme.color.textMuted }
            Tk.Spacer { }
            Tk.Mono { text: strip.gainText }
        }
        Tk.Slider {
            id: fader
            objectName: "channelGainFader"
            width: parent.width
            from: strip.gainMap ? 0 : strip.gainMinimum
            to: strip.gainMap ? 1 : strip.gainMaximum
            value: strip.sliderValue
            showValue: false
            tooltip: strip.tooltip + qsTr(" · gain")
            onPressedChanged: if (pressed) strip.startGain()
                              else if (strip._gainGestureId) strip.finishGain(value)
            onValueModified: if (pressed) strip.updateGain(value)
                             else strip.finishGain(value)
        }
        Tk.PanningControl {
            id: pan
            objectName: "channelPanning"
            width: parent.width
            channelId: strip.channelId
            panParameterId: strip.channelId + "/pan"
            widthParameterId: strip.channelId + "/width"
            panValue: strip.panValue
            stereoWidth: strip.stereoWidth
            speakerLayout: strip.speakerLayout
            tooltip: strip.channelName + qsTr(" panning")
            onParameterEditRequested: strip.panEditRequested(channelId, parameterId, gestureId, phase, value)
        }
        Tk.Flex {
            width: parent.width
            Tk.IconButton {
                objectName: "channelMute"
                iconName: "volume-x"
                checkable: false
                checked: strip.muted
                tooltip: strip.muted ? qsTr("Unmute %1").arg(strip.channelName) : qsTr("Mute %1").arg(strip.channelName)
                onClicked: strip.requestCommand("mute", !strip.muted)
            }
            Tk.IconButton {
                objectName: "channelSolo"
                iconName: "headphones"
                checkable: false
                checked: strip.soloed
                tooltip: strip.soloed ? qsTr("Unsolo %1").arg(strip.channelName) : qsTr("Solo %1").arg(strip.channelName)
                onClicked: strip.requestCommand("solo", !strip.soloed)
            }
            Tk.Spacer { }
            Tk.Caption { text: strip.routeSummary; elide: Text.ElideRight; color: Tk.Theme.color.textMuted }
        }
        Tk.Caption {
            objectName: "channelCommandStatus"
            visible: strip.commandMessage.length > 0
            text: strip.commandMessage
            color: strip.commandState === "rejected" || strip.commandState === "error"
                   ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
    }
    Tk.ToolTip { visible: strip.hovered && strip.tooltip.length > 0; text: strip.tooltip }
    TapHandler {
        objectName: "channelSelectionArea"
        acceptedButtons: Qt.LeftButton
        onTapped: strip.channelSelected(strip.channelId)
    }
}
