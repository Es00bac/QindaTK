// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: testCase
    name: "DeviceSettingsPanel"
    visible: true
    width: 720
    height: 560

    property var hostActive: ({ deviceId: "device-a", sampleRate: 44100, bufferSize: 256,
                               inputLayout: "Stereo", outputLayout: "Stereo" })
    property var hostPending: ({ deviceId: "device-a", sampleRate: 48000, bufferSize: 128,
                                 inputLayout: "Stereo", outputLayout: "Stereo" })
    property string hostRequestId: ""
    property int applyCount: 0

    readonly property var devices: [
        { id: "device-a", name: "Interface A", available: true, sampleRates: [44100, 48000],
          bufferSizes: [128, 256], inputLayouts: ["Stereo", "4-channel"], outputLayouts: ["Stereo", "Quad"] }
    ]
    Component {
        id: panelComponent
        Tk.DeviceSettingsPanel {
            width: 680
            height: 520
            devices: testCase.devices
            activeConfiguration: testCase.hostActive
            pendingConfiguration: testCase.hostPending
            onConfigurationChangeRequested: function(candidate) { testCase.hostPending = candidate }
            onApplyRequested: function(requestId, configuration) {
                testCase.hostRequestId = requestId
                ++testCase.applyCount
            }
        }
    }

    function test_applyWaitsForExplicitHostDecision() {
        const panel = createTemporaryObject(panelComponent, this)
        verify(panel !== null)
        const applyButton = findChild(panel, "deviceSettingsApply")
        verify(applyButton !== null)
        verify(applyButton.enabled)

        mouseClick(applyButton)
        compare(applyCount, 1)
        verify(hostRequestId.length > 0)
        compare(panel.applyState, "pending")
        compare(panel.acknowledgeApply("stale-request", "accepted", hostPending, ""), false)
        compare(panel.applyState, "pending")
        compare(hostActive.sampleRate, 44100)

        compare(panel.acknowledgeApply(hostRequestId, "rejected", hostActive, "Device busy"), true)
        compare(panel.applyState, "rejected")
        compare(panel.applyMessage, "Device busy")
        compare(hostActive.sampleRate, 44100)

        hostPending = Object.assign({}, hostPending, { sampleRate: 44100 })
        panel.configurationChangeRequested(Object.assign({}, hostPending, { sampleRate: 48000 }))
        hostPending = Object.assign({}, hostPending, { sampleRate: 48000 })
        mouseClick(applyButton)
        compare(applyCount, 2)
        compare(panel.applyState, "pending")
        compare(panel.acknowledgeApply(hostRequestId, "accepted", hostPending, "Engine confirmed"), true)
        hostActive = hostPending
        compare(panel.applyState, "accepted")
        compare(panel.activeConfiguration.sampleRate, 48000)
        panel.destroy()
    }

    // A live AudioEngine host with its silent built-in devices: the panel's
    // request is acknowledged by the engine, and a hot-unplug is reported with
    // recovery by the same stable device ID.
    Tk.AudioEngine { id: liveEngine }
    Component {
        id: livePanelComponent
        Tk.DeviceSettingsPanel {
            id: livePanel
            width: 680
            height: 520
            devices: liveEngine.devices
            activeConfiguration: liveEngine.activeConfiguration
            engineState: liveEngine.state
            engineMessage: liveEngine.stateMessage
            pendingConfiguration: ({ deviceId: "builtin:null", sampleRate: 48000, bufferSize: 256,
                                     inputLayout: "None", outputLayout: "Stereo" })
            onApplyRequested: function(requestId, configuration) {
                liveEngine.applyConfiguration(requestId, configuration)
            }
            Connections {
                target: liveEngine
                function onConfigurationApplied(requestId, state, active, message) {
                    livePanel.acknowledgeApply(requestId, state, active, message)
                }
            }
        }
    }

    function test_liveEngineAcknowledgesAndRecovers() {
        const panel = createTemporaryObject(livePanelComponent, this)
        verify(panel !== null)
        verify(panel.deviceForId("builtin:null") !== null)
        const applyButton = findChild(panel, "deviceSettingsApply")
        mouseClick(applyButton)
        tryCompare(panel, "applyState", "accepted", 5000)
        compare(liveEngine.state, "running")
        compare(panel.activeConfiguration.deviceId, "builtin:null")
        verify(!applyButton.enabled)

        verify(liveEngine.setBuiltinDeviceConnected("builtin:null", false))
        tryCompare(liveEngine, "state", "lost", 5000)
        const status = findChild(panel, "deviceSettingsStatus")
        compare(status.text, liveEngine.stateMessage)
        compare(panel.pendingDevice.available, false)
        verify(!applyButton.enabled)

        verify(liveEngine.setBuiltinDeviceConnected("builtin:null", true))
        tryCompare(panel, "recoveryAvailable", true)
        compare(applyButton.text, "Reconnect")
        mouseClick(applyButton)
        tryCompare(liveEngine, "state", "running", 5000)
        compare(panel.applyState, "accepted")
        liveEngine.stop()
        tryCompare(liveEngine, "state", "stopped", 5000)
    }
}
