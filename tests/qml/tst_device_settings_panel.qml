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
}
