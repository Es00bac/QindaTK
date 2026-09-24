// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A live host (WP-060): AudioSession + AudioEngine start on the silent
// built-in null device. DeviceSettingsPanel requests are acknowledged by the
// engine itself, the transport bar drives the engine's TransportModel, and
// the statistics are measured device callbacks — no mock host or timer.
Rectangle {
    id: root
    width: 1040
    height: 600
    color: Tk.Theme.color.bg
    property string hostStatus: qsTr("Opening the null device…")
    property var pending: ({ deviceId: "builtin:null", sampleRate: 48000, bufferSize: 256,
                             inputLayout: "None", outputLayout: "Stereo" })
    readonly property var stats: engine.statistics

    function micros(value) { return Number(value || 0).toFixed(1) }

    Tk.AudioEngine {
        id: engine
        onConfigurationApplied: function(requestId, state, active, message) {
            panel.acknowledgeApply(requestId, state, active, message)
            root.hostStatus = message
        }
    }
    Tk.AudioSession {
        id: session
        engine: engine
    }
    Component.onCompleted: engine.applyConfiguration("startup", root.pending)

    Tk.Flex {
        id: columns
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        gap: Tk.Theme.space.lg

        Tk.DeviceSettingsPanel {
            id: panel
            objectName: "audioSessionDevicePanel"
            Tk.Flex.flex: 1
            height: columns.height
            devices: engine.devices
            activeConfiguration: engine.activeConfiguration
            pendingConfiguration: root.pending
            engineState: engine.state
            engineMessage: engine.stateMessage
            onConfigurationChangeRequested: function(candidate) { root.pending = candidate }
            onApplyRequested: function(requestId, configuration) { engine.applyConfiguration(requestId, configuration) }
        }

        Tk.Flex {
            id: side
            Tk.Flex.flex: 1
            height: columns.height
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm

            Tk.Overline { title: qsTr("TRANSPORT · LIVE ENGINE") }
            Tk.TransportBar {
                objectName: "audioSessionTransport"
                width: side.width
                transport: engine.transport
            }
            Tk.Overline { title: qsTr("MEASURED DEVICE CALLBACKS") }
            Tk.Mono {
                objectName: "audioSessionEngine"
                text: qsTr("%1 · %2 Hz · %3-frame periods").arg(engine.state).arg(engine.sampleRate).arg(engine.bufferSize)
            }
            Tk.Mono {
                objectName: "audioSessionCallbacks"
                text: qsTr("callbacks %1 · late %2 · xruns %3")
                      .arg(root.stats.callbacks || 0).arg(root.stats.lateCallbacks || 0).arg(root.stats.xruns || 0)
            }
            Tk.Mono {
                objectName: "audioSessionTiming"
                text: qsTr("callback max %1 µs · mean %2 µs · budget %3 µs")
                      .arg(root.micros(root.stats.maxCallbackUs)).arg(root.micros(root.stats.meanCallbackUs))
                      .arg(root.micros(root.stats.budgetUs))
            }
            Tk.Mono {
                objectName: "audioSessionLatency"
                text: qsTr("latency in %1 · out %2 frames · wake jitter max %3 µs")
                      .arg(root.stats.inputLatencyFrames || 0).arg(root.stats.outputLatencyFrames || 0)
                      .arg(root.micros(root.stats.maxWakeLatenessUs))
            }
            Tk.Mono {
                objectName: "audioSessionClips"
                text: qsTr("session: %1 tracks · %2 clips · %3 missing media")
                      .arg(session.timeline.trackCount).arg(session.timeline.clipCount).arg(session.missingMedia.length)
            }
            Tk.Caption {
                objectName: "audioSessionStatus"
                width: side.width
                text: root.hostStatus
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
