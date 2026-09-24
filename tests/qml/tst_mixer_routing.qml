// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: testCase
    name: "MixerRouting"
    visible: true
    width: 800
    height: 600
    property string commandChannelId: ""
    property string commandRequestId: ""
    property string commandName: ""
    property string routeInputId: ""
    property string routeOutputId: ""
    property string routeRequestId: ""
    readonly property var inputs: [{id: "input-stable-7", label: "Voice in", available: true}]
    readonly property var outputs: [
        {id: "output-main", label: "Main", available: true},
        {id: "output-monitor", label: "Monitor", available: true}
    ]
    readonly property var activeConnections: [{inputId: "input-stable-7", outputId: "output-main"}]

    Component {
        id: surfaces
        Item {
            width: 800; height: 600
            Tk.ChannelStrip {
                id: strip
                objectName: "testChannelStrip"
                width: 220; height: 520
                channelId: "channel-stable-44"
                channelName: "Lead Vox"
                gainParameterId: "audio/lead-vox/gain"
                onChannelCommandRequested: function(channelId, requestId, command) {
                    testCase.commandChannelId = channelId
                    testCase.commandRequestId = requestId
                    testCase.commandName = command
                }
            }
            Tk.RoutingMatrix {
                id: matrix
                objectName: "testRoutingMatrix"
                x: 240
                width: 520; height: 520
                inputPorts: testCase.inputs
                outputPorts: testCase.outputs
                connections: testCase.activeConnections
                onRouteRequested: function(requestId, inputId, outputId) {
                    testCase.routeRequestId = requestId
                    testCase.routeInputId = inputId
                    testCase.routeOutputId = outputId
                }
            }
        }
    }

    function test_stableChannelAndPortRequestsWaitForHostDecision() {
        const root = createTemporaryObject(surfaces, this)
        verify(root !== null)
        const strip = findChild(root, "testChannelStrip")
        const matrix = findChild(root, "testRoutingMatrix")
        verify(strip !== null)
        verify(matrix !== null)

        const muteButton = findChild(strip, "channelMute")
        verify(muteButton !== null)
        compare(muteButton.objectName, "channelMute")
        strip.requestCommand("mute", true)
        compare(commandChannelId, "channel-stable-44")
        compare(commandName, "mute")
        verify(commandRequestId.length > 0)
        compare(strip.muted, false)
        verify(strip.acknowledgeCommand(commandRequestId, "rejected", "Host refused mute"))
        compare(strip.commandState, "rejected")
        compare(strip.muted, false)

        routeRequestId = matrix.requestRoute("input-stable-7", "output-monitor")
        verify(routeRequestId.length > 0)
        compare(routeInputId, "input-stable-7")
        compare(routeOutputId, "output-monitor")
        compare(matrix.outputIdFor("input-stable-7"), "output-main")
        verify(matrix.acknowledgeRoute(routeRequestId, "rejected", "Feedback route refused"))
        compare(matrix.routeState, "rejected")
        compare(matrix.outputIdFor("input-stable-7"), "output-main")
        root.destroy()
    }
}
