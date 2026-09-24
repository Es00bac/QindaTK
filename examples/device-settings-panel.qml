// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A mock host: it owns capabilities, active/pending snapshots and the delayed
// apply result. No audio engine is opened by this fixture.
Item {
    id: fixture
    width: 760
    height: 640
    property string hostStatus: qsTr("Mock host ready")
    property string outstandingRequestId: ""
    property var outstandingConfiguration: ({})
    property var active: ({ deviceId: "device-main-7", sampleRate: 48000, bufferSize: 256,
                            inputLayout: "Stereo", outputLayout: "Stereo" })
    property var pending: ({ deviceId: "device-main-7", sampleRate: 96000, bufferSize: 128,
                             inputLayout: "Stereo", outputLayout: "Quad" })

    readonly property var deviceCatalog: [
        { id: "device-main-7", name: "Studio Interface", available: true,
          sampleRates: [44100, 48000, 96000], bufferSizes: [64, 128, 256, 512],
          inputLayouts: ["Mono", "Stereo", "4-channel"], outputLayouts: ["Stereo", "Quad", "5.1"] },
        { id: "device-usb-2", name: "USB Field Recorder", available: true,
          sampleRates: [44100, 48000], bufferSizes: [128, 256, 512, 1024],
          inputLayouts: ["Stereo", "4-channel"], outputLayouts: ["Stereo"] },
        { id: "device-offline-9", name: "Unavailable Device", available: false,
          unavailableReason: "Device was unplugged", sampleRates: [], bufferSizes: [], inputLayouts: [], outputLayouts: [] }
    ]

    Timer {
        id: applyReply
        interval: Tk.Theme.motion.base
        repeat: false
        onTriggered: {
            if (fixture.rejectNext) {
                panel.acknowledgeApply(fixture.outstandingRequestId, "rejected", fixture.active,
                                       qsTr("The mock host rejected this configuration"))
                fixture.hostStatus = qsTr("Rejected; the active device remains %1").arg(fixture.active.deviceId)
                fixture.rejectNext = false
            } else {
                fixture.active = Object.assign({}, fixture.outstandingConfiguration)
                fixture.pending = fixture.active
                panel.acknowledgeApply(fixture.outstandingRequestId, "accepted", fixture.active,
                                       qsTr("Mock host accepted the configuration"))
                fixture.hostStatus = qsTr("Host applied %1").arg(fixture.active.deviceId)
            }
            fixture.outstandingRequestId = ""
        }
    }
    property bool rejectNext: false

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        padding: Tk.Theme.space.lg
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Overline { title: qsTr("DEVICE SETTINGS · HOST ACKNOWLEDGMENT") }
            Tk.DeviceSettingsPanel {
                id: panel
                objectName: "fixtureDeviceSettingsPanel"
                width: parent.width
                height: parent.height - Tk.Theme.size.row * 3
                devices: fixture.deviceCatalog
                activeConfiguration: fixture.active
                pendingConfiguration: fixture.pending
                onConfigurationChangeRequested: function(candidate) {
                    fixture.pending = candidate
                    fixture.hostStatus = qsTr("Pending configuration changed; active device unchanged")
                }
                onApplyRequested: function(requestId, configuration) {
                    fixture.outstandingRequestId = requestId
                    fixture.outstandingConfiguration = configuration
                    fixture.hostStatus = qsTr("Host is evaluating %1").arg(configuration.deviceId)
                    applyReply.restart()
                }
                onApplyAcknowledged: function(requestId, state, configuration, message) {
                    fixture.hostStatus = message
                }
            }
            Tk.Flex {
                width: parent.width
                Tk.CheckBox {
                    objectName: "fixtureRejectNext"
                    text: qsTr("Reject next host request")
                    checked: fixture.rejectNext
                    tooltip: qsTr("Make the mock host report a rejected reconfiguration")
                    onToggled: fixture.rejectNext = checked
                }
                Tk.Spacer { }
                Tk.Caption { objectName: "fixtureHostStatus"; text: fixture.hostStatus }
            }
        }
    }
}
