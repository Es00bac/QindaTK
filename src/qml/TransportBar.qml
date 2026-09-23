// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: every visual state and exact readout comes from the
// acknowledged TransportModel snapshot. This bar contains no media timer.
// Space toggles play/stop; R record, L loop, P punch, Home locate zero;
// Left/Right seek by one TimeMap second using exact signed sample strings.
Tk.ToolBar {
    id: bar
    property Tk.TransportModel transport: null
    property string displayMode: "timecode" // timecode, frames, samples, ticks
    property bool dropFrame: false
    property string loopStartSample: transport ? transport.loopStartSample : "0"
    property string loopEndSample: transport ? transport.loopEndSample : "0"
    property string punchStartSample: transport ? transport.punchStartSample : "0"
    property string punchEndSample: transport ? transport.punchEndSample : "0"
    tooltip: qsTr("Transport")
    readonly property string seekStepSamples: transport && transport.timeMap
                                              ? String(transport.timeMap.sampleRate) : "1"
    readonly property string readoutText: {
        if (!transport) return qsTr("No transport")
        if (displayMode === "samples") return transport.positionSample
        // Reading a NOTIFY property makes snapshot and TimeMap changes
        // invalidate this binding; calling a C++ method alone does not.
        if (!transport.timeMap) return "—"
        return transport.formatPosition(displayMode, dropFrame)
    }
    readonly property bool transportAvailable: !!transport

    compact: true
    activeFocusOnTab: true
    Accessible.role: Accessible.ToolBar
    Accessible.name: tooltip
    Accessible.description: transport && transport.deviceError ? transport.deviceError
                            : transport && transport.lastError ? transport.lastError : ""

    function togglePlayback() {
        if (!transport) return
        if (transport.playing) transport.requestStop()
        else transport.requestPlay()
    }
    function toggleRecording() {
        if (transport) transport.requestRecord(!transport.recording)
    }
    function toggleLoop() {
        if (transport) transport.requestLoop(!transport.loopEnabled, loopStartSample, loopEndSample)
    }
    function togglePunch() {
        if (transport) transport.requestPunch(!transport.punchEnabled, punchStartSample, punchEndSample)
    }
    Keys.onSpacePressed: function(event) {
        if (!transport || event.isAutoRepeat) return
        bar.togglePlayback()
        event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (!transport || event.isAutoRepeat) return
        switch (event.key) {
        case Qt.Key_R: bar.toggleRecording(); event.accepted = true; break
        case Qt.Key_L: bar.toggleLoop(); event.accepted = true; break
        case Qt.Key_P: bar.togglePunch(); event.accepted = true; break
        case Qt.Key_Home: transport.requestSeek("0"); event.accepted = true; break
        case Qt.Key_Left: transport.requestSeekRelative("-" + seekStepSamples); event.accepted = true; break
        case Qt.Key_Right: transport.requestSeekRelative(seekStepSamples); event.accepted = true; break
        }
    }

    Tk.IconButton {
        objectName: "transportPlay"
        iconName: "play"
        tooltip: !bar.transport ? qsTr("No transport engine")
                 : !bar.transport.canPlay ? (bar.transport.playUnavailableReason || qsTr("Playback unavailable"))
                 : qsTr("Play (Space)")
        enabled: bar.transport && bar.transport.canPlay
        onClicked: bar.transport.requestPlay()
    }
    Tk.IconButton {
        objectName: "transportStop"
        iconName: "square"
        tooltip: qsTr("Stop (Space while playing)")
        enabled: bar.transportAvailable
        onClicked: bar.transport.requestStop()
    }
    Tk.IconButton {
        objectName: "transportRecord"
        iconName: "circle"
        danger: bar.transport && bar.transport.recording
        checked: bar.transport && bar.transport.recording
        tooltip: !bar.transport ? qsTr("No transport engine")
                 : !bar.transport.canRecord && !bar.transport.recording
                   ? (bar.transport.recordUnavailableReason || qsTr("Recording unavailable"))
                   : qsTr("Record (R)")
        enabled: bar.transport && (bar.transport.canRecord || bar.transport.recording)
        onClicked: bar.toggleRecording()
    }
    Tk.ToolSeparator { }
    Tk.IconButton {
        objectName: "transportLocateStart"
        iconName: "skip-back"
        tooltip: !bar.transport || !bar.transport.canSeek
                 ? (bar.transport ? bar.transport.seekUnavailableReason : qsTr("No transport engine"))
                 : qsTr("Locate start (Home)")
        enabled: bar.transport && bar.transport.canSeek
        onClicked: bar.transport.requestSeek("0")
    }
    Tk.IconButton {
        objectName: "transportLoop"
        iconName: "repeat"
        checked: bar.transport && bar.transport.loopEnabled
        tooltip: !bar.transport ? qsTr("No transport engine")
                 : !bar.transport.canLoop && !bar.transport.loopEnabled
                   ? (bar.transport.loopUnavailableReason || qsTr("Loop unavailable"))
                   : qsTr("Toggle loop (L)")
        enabled: bar.transport && (bar.transport.canLoop || bar.transport.loopEnabled)
        onClicked: bar.toggleLoop()
    }
    Tk.IconButton {
        objectName: "transportPunch"
        iconName: "crosshair"
        checked: bar.transport && bar.transport.punchEnabled
        tooltip: !bar.transport ? qsTr("No transport engine")
                 : !bar.transport.canPunch && !bar.transport.punchEnabled
                   ? (bar.transport.punchUnavailableReason || qsTr("Punch unavailable"))
                   : qsTr("Toggle punch (P)")
        enabled: bar.transport && (bar.transport.canPunch || bar.transport.punchEnabled)
        onClicked: bar.togglePunch()
    }
    Tk.Mono {
        objectName: "transportTime"
        text: bar.readoutText
        color: bar.transport && bar.transport.deviceError ? Tk.Theme.color.danger : Tk.Theme.color.text
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Transport position %1").arg(text)
    }
    Tk.Caption {
        objectName: "transportStatus"
        visible: bar.transport && (bar.transport.deviceError.length > 0 || bar.transport.lastError.length > 0)
        text: !bar.transport ? "" : bar.transport.deviceError || bar.transport.lastError
        color: Tk.Theme.color.danger
    }
}
