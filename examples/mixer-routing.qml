// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The host owns the mock channel graph. Its explicit callbacks update channel
// snapshots and accept/reject operations; this fixture is not an audio engine.
Item {
    id: fixture
    width: 1280
    height: 740
    property string selectedChannelId: "channel-001"
    property string hostStatus: qsTr("Mock mixer ready")
    property bool rejectNext: false
    property var preview: ({})
    property var channels: []
    property var connections: [
        { inputId: "input-vocal", outputId: "bus-main" },
        { inputId: "input-drums", outputId: "bus-main" },
        { inputId: "input-synth", outputId: "bus-monitor" }
    ]
    readonly property var inputs: [
        { id: "input-vocal", label: "Vocal · Stereo", layout: "Stereo", available: true },
        { id: "input-drums", label: "Drums · 4ch", layout: "4-channel", available: true },
        { id: "input-synth", label: "Synth · Stereo", layout: "Stereo", available: true },
        { id: "input-archive", label: "Archive · Stereo", layout: "Stereo", available: false }
    ]
    readonly property var outputs: [
        { id: "bus-main", label: "Main · Stereo", layout: "Stereo", available: true },
        { id: "bus-monitor", label: "Monitor · Stereo", layout: "Stereo", available: true },
        { id: "device-out-5", label: "Device Out 5/6 · Stereo", layout: "Stereo", available: false }
    ]

    function buildChannels() {
        const rows = []
        for (let i = 1; i <= 12; ++i) {
            const id = "channel-" + String(i).padStart(3, "0")
            rows.push({ id: id, name: i === 1 ? "Vocal" : i === 2 ? "Drums" : i === 3 ? "Synth" : "Bus " + i,
                        kind: i > 9 ? "bus" : "audio", gainParameterId: id + "/gain",
                        gainValue: i === 1 ? -3 : i === 2 ? -6 : 0, gainSilent: false,
                        muted: i === 5, soloed: false, panValue: i === 2 ? -0.25 : 0,
                        stereoWidth: 1, speakerLayout: "Stereo", peakDbfs: -6 - i * 2,
                        rmsDbfs: -14 - i, routeSummary: i < 4 ? "→ Main" : "→ Monitor",
                        commandState: "idle", commandMessage: "" })
        }
        channels = rows
    }
    function stripFor(id) {
        for (let i = 0; i < mixer.channelList.count; ++i) {
            const item = mixer.channelList.itemAtIndex(i)
            if (item && item.channelId === id) return item
        }
        return null
    }
    function replaceChannel(id, patch) {
        const rows = channels.slice()
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].id === id) rows[i] = Object.assign({}, rows[i], patch)
        channels = rows
    }
    function useRejection() {
        if (!rejectNext) return false
        rejectNext = false
        return true
    }
    Component.onCompleted: buildChannels()

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        padding: Tk.Theme.space.md
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Flex {
                width: parent.width
                Tk.Overline { title: qsTr("MIXER · ROUTING") }
                Tk.Spacer { }
                Tk.CheckBox {
                    objectName: "mixerRejectNext"
                    text: qsTr("Reject next request")
                    checked: fixture.rejectNext
                    tooltip: qsTr("Make the mock host reject the next mixer or routing request")
                    onToggled: fixture.rejectNext = checked
                }
            }
            Tk.Flex {
                width: parent.width
                height: parent.height - Tk.Theme.size.row * 2
                Tk.Flex.grow: 1
                gap: Tk.Theme.space.md
                Tk.MixerView {
                    id: mixer
                    objectName: "fixtureMixer"
                    Tk.Flex.grow: 2
                    channels: fixture.channels
                    selectedChannelId: fixture.selectedChannelId
                    onChannelSelected: function(id) { fixture.selectedChannelId = id }
                    onGainEditRequested: function(id, parameterId, gestureId, phase, value, silent) {
                        fixture.preview = ({channelId: id, parameterId: parameterId, value: value, phase: phase})
                        const strip = fixture.stripFor(id)
                        if (phase === "commit") {
                            if (fixture.useRejection()) {
                                if (strip) strip.acknowledgeGainEdit(gestureId, "rejected", qsTr("Host rejected gain edit"))
                                fixture.hostStatus = qsTr("Rejected gain edit for %1").arg(id)
                            } else {
                                fixture.replaceChannel(id, {gainValue: value, gainSilent: silent,
                                                            commandState: "accepted", commandMessage: qsTr("Gain edit accepted")})
                                if (strip) strip.acknowledgeGainEdit(gestureId, "accepted", qsTr("Gain edit accepted"))
                                fixture.hostStatus = qsTr("Updated %1 gain").arg(id)
                            }
                        }
                    }
                    onPanEditRequested: function(id, parameterId, gestureId, phase, value) {
                        fixture.preview = ({channelId: id, parameterId: parameterId, value: value, phase: phase})
                        const strip = fixture.stripFor(id)
                        if (phase === "commit") {
                            if (fixture.useRejection()) {
                                if (strip) strip.panningControl.acknowledgeEdit(gestureId, false, qsTr("Host rejected pan edit"))
                                fixture.hostStatus = qsTr("Rejected panning edit for %1").arg(id)
                            } else {
                                const patch = parameterId.endsWith("/pan") ? {panValue: value} : {stereoWidth: value}
                                fixture.replaceChannel(id, patch)
                                if (strip) strip.panningControl.acknowledgeEdit(gestureId, true, qsTr("Panning edit accepted"))
                                fixture.hostStatus = qsTr("Updated %1").arg(parameterId)
                            }
                        }
                    }
                    onChannelCommandRequested: function(id, requestId, command, enabled) {
                        const strip = fixture.stripFor(id)
                        if (fixture.useRejection()) {
                            if (strip) strip.acknowledgeCommand(requestId, "rejected", qsTr("Host rejected %1").arg(command))
                            fixture.replaceChannel(id, {commandState: "rejected", commandMessage: qsTr("%1 rejected").arg(command)})
                            fixture.hostStatus = qsTr("Rejected %1 for %2").arg(command).arg(id)
                        } else {
                            const patch = command === "mute" ? {muted: enabled} : {soloed: enabled}
                            patch.commandState = "accepted"
                            patch.commandMessage = qsTr("%1 accepted").arg(command)
                            fixture.replaceChannel(id, patch)
                            if (strip) strip.acknowledgeCommand(requestId, "accepted", patch.commandMessage)
                            fixture.hostStatus = qsTr("%1 %2 for %3").arg(command).arg(enabled ? "on" : "off").arg(id)
                        }
                    }
                }
                Tk.RoutingMatrix {
                    id: routing
                    objectName: "fixtureRouting"
                    Tk.Flex.grow: 1
                    inputPorts: fixture.inputs
                    outputPorts: fixture.outputs
                    connections: fixture.connections
                    onRouteRequested: function(requestId, inputId, outputId) {
                        if (fixture.useRejection()) {
                            routing.acknowledgeRoute(requestId, "rejected", qsTr("Host rejected incompatible or unsafe route"))
                            fixture.hostStatus = qsTr("Rejected route %1 → %2").arg(inputId).arg(outputId || "disconnected")
                        } else {
                            const rows = fixture.connections.filter(function(item) { return item.inputId !== inputId })
                            if (outputId.length) rows.push({inputId: inputId, outputId: outputId})
                            fixture.connections = rows
                            routing.acknowledgeRoute(requestId, "accepted", qsTr("Host accepted route"))
                            fixture.hostStatus = qsTr("Routed %1 → %2").arg(inputId).arg(outputId || "disconnected")
                        }
                    }
                }
            }
            Tk.Caption {
                objectName: "mixerHostStatus"
                text: [fixture.hostStatus, fixture.preview.channelId
                       ? qsTr("Preview %1 · %2").arg(fixture.preview.parameterId).arg(fixture.preview.phase) : ""]
                      .filter(function(v) { return v.length > 0 }).join(" · ")
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
