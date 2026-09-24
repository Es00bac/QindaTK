// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 980
    height: 620
    color: Tk.Theme.color.bg
    property real gainDb: -6
    property real frequencyHz: 440
    property bool muted: false
    property string gestureStatus: qsTr("Ready")
    property var meterSnapshot: [
        {channel: "L", peakDbfs: -2.4, rmsDbfs: -16.2, displayPeakDbfs: -2.4,
         heldPeakDbfs: -1.8, clipped: false, silent: false},
        {channel: "R", peakDbfs: -0.8, rmsDbfs: -14.7, displayPeakDbfs: -0.8,
         heldPeakDbfs: -0.8, clipped: true, silent: false}
    ]
    property var loudnessSamples: [
        {time: 0, value: -24.1}, {time: 1, value: -22.8}, {time: 2, value: -20.7},
        {time: 3, value: -21.4}, {time: 4, value: -18.9}, {time: 5, value: -19.2},
        {time: 6, value: -16.7}, {time: 7, value: -17.4}
    ]
    function resetClipLatch() {
        const next = []
        for (let i = 0; i < meterSnapshot.length; ++i) {
            const channel = Object.assign({}, meterSnapshot[i]); channel.clipped = false; next.push(channel)
        }
        meterSnapshot = next
    }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        direction: Tk.Flex.Column
        Tk.Label {
            text: qsTr("Calibrated audio controls")
            font.pixelSize: Tk.Theme.font.title
            Accessible.role: Accessible.Heading
        }
        Tk.Label {
            text: qsTr("Host owns parameter transactions and measurement snapshots; controls only present and request changes.")
            muted: true
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.lg
            Tk.Card {
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.lg
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Label { text: qsTr("Output gain") }
                        Tk.Knob {
                            parameterId: "master.gain"
                            tooltip: qsTr("Master gain in decibels")
                            from: -60; to: 6; defaultValue: 0; stepSize: 0.1
                            unit: " dB"; taper: "decibel"; value: root.gainDb
                            onGestureStarted: root.gestureStatus = qsTr("Gain gesture started")
                            onValuePreview: (v) => { root.gainDb = v; root.muted = false }
                            onGestureCommitted: root.gestureStatus = qsTr("Gain committed to host transaction")
                            onGestureCanceled: root.gestureStatus = qsTr("Gain gesture canceled")
                        }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Label { text: qsTr("Master fader") }
                        Tk.Fader {
                            parameterId: "master.output"
                            tooltip: qsTr("Master output level; −∞ is explicit silence")
                            from: -60; to: 6; value: root.gainDb
                            unityValue: 0; defaultValue: 0; unit: "dB"
                            silent: root.muted; vertical: true
                            onGestureStarted: root.gestureStatus = qsTr("Fader gesture started")
                            onValuePreview: (v, isSilent) => { root.gainDb = v; root.muted = isSilent }
                            onGestureCommitted: root.gestureStatus = qsTr("Fader committed to host transaction")
                            onGestureCanceled: (v, isSilent) => { root.gainDb = v; root.muted = isSilent; root.gestureStatus = qsTr("Fader gesture canceled") }
                        }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Label { text: qsTr("Oscillator frequency") }
                        Tk.Knob {
                            parameterId: "synth.frequency"
                            tooltip: qsTr("Oscillator frequency")
                            from: 20; to: 20000; defaultValue: 440; stepSize: 1
                            decimals: 0; unit: " Hz"; taper: "logarithmic"; value: root.frequencyHz
                            onValuePreview: root.frequencyHz = v
                            onGestureCommitted: root.gestureStatus = qsTr("Frequency committed to host transaction")
                        }
                    }
                }
            }
            Tk.Card {
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Label { text: qsTr("Provider measurement snapshot"); font.pixelSize: Tk.Theme.font.small }
                    Tk.AudioMeter {
                        width: Tk.Theme.size.fieldWidth * 2
                        levels: root.meterSnapshot
                        metric: "peak-rms"; unit: "dBFS"
                        integrationWindow: "RMS: provider-declared 300 ms"
                        tooltip: qsTr("Peak and RMS snapshot by channel")
                        onClipResetRequested: root.resetClipLatch()
                    }
                    Tk.Label { text: qsTr("Display hold and clip state come from the provider."); muted: true }
                }
            }
        }
        Tk.Card {
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Label { text: qsTr("Loudness history provider contract"); font.pixelSize: Tk.Theme.font.small }
                Tk.LoudnessHistory {
                    Tk.Flex.grow: 1
                    height: Tk.Theme.size.controlLg * 4
                    samples: root.loudnessSamples
                    metricId: "fixture.integrated-level"
                    unit: "dBFS"
                    integrationWindow: "fixture interval: one sample per second"
                    channelLayout: "stereo (L/R)"
                    targetProfile: "Example only; no standards compliance claim"
                    minimum: -60; maximum: 0; targetMinimum: -24; targetMaximum: -18
                    tooltip: qsTr("Provider-defined demonstration history")
                    onResetHistoryRequested: { root.loudnessSamples = []; root.gestureStatus = qsTr("History reset requested") }
                    onExportRequested: (metric, units, window, layout, profile, values) => {
                        root.gestureStatus = qsTr("Export requested: %1, %2, %3 samples").arg(metric).arg(units).arg(values.length)
                    }
                }
            }
        }
        Tk.StatusField { text: root.gestureStatus; muted: true }
    }
}
