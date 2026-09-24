// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-061 fixture: the knobs and the fader edit real AudioSession parameters
// through Tk.AudioParameterBinding (each gesture is one undoable transaction;
// the knob and the fader share one gain parameter), and the meter and the
// loudness history show values measured from generated test tones by
// Tk.AudioMeterModel and Tk.AudioLoudnessModel (ITU-R BS.1770-4). No device
// is opened here; examples/audio-session plays through the live engine.
Rectangle {
    id: root
    width: 980
    height: 620
    color: Tk.Theme.color.bg
    property string gestureStatus: qsTr("Ready")

    Tk.AudioSession { id: session }
    Tk.AudioParameterBinding {
        id: gain
        session: session
        onTransactionFinished: (result) => root.gestureStatus = qsTr("Gain %1: %2").arg(result).arg(gain.valueText)
    }
    Tk.AudioParameterBinding {
        id: frequency
        session: session
        resetValue: 440
        onTransactionFinished: (result) => root.gestureStatus = qsTr("Frequency %1: %2").arg(result).arg(frequency.valueText)
    }
    Tk.AudioMeterModel { id: toneMeter }
    Tk.AudioLoudnessModel { id: toneLoudness; historyMetric: "momentary" }

    // Phase-continuous 1 kHz sine: `levels` dBFS for `seconds` each, `channels` wide.
    function tone(levels, seconds, channels, rate) {
        const samples = []
        let n = 0
        for (let l = 0; l < levels.length; ++l) {
            const amplitude = Math.pow(10, levels[l] / 20)
            for (let i = 0; i < seconds * rate; ++i, ++n) {
                const s = amplitude * Math.sin(2 * Math.PI * 1000 * n / rate)
                for (let c = 0; c < channels; ++c) samples.push(s)
            }
        }
        return samples
    }
    Component.onCompleted: {
        gain.parameterId = session.ensureGainParameter(session.addTrack(qsTr("Master")))
        session.automation.addParameterMap({ id: "synth:frequency", name: qsTr("Oscillator frequency"),
                                             dimension: "frequency", unitId: "Hz", minimum: 20, maximum: 20000,
                                             default: 440, step: 1 })
        frequency.parameterId = "synth:frequency"
        // Measured blocks: -6 dBFS on channel 1, full scale (a clip) on channel 2.
        const block = tone([0], 0.01, 1, 48000)
        const stereo = []
        for (let i = 0; i < block.length; ++i) stereo.push(0.5 * block[i], block[i])
        toneMeter.submitInterleaved(stereo, 2, 10)
        // Two seconds at -26 dBFS, then two at -20 dBFS, mono.
        toneLoudness.submitInterleaved(tone([-26, -20], 2, 1, 48000), 1, 48000)
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
            text: qsTr("Controls edit session parameters as one transaction per gesture; meters show measured test tones.")
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
                            binding: gain
                            tooltip: qsTr("Master gain in decibels")
                            stepSize: 0.1
                        }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Label { text: qsTr("Master fader") }
                        Tk.Fader {
                            binding: gain
                            tooltip: qsTr("Master output level; −∞ is exact silence")
                            unityValue: 0
                            scaleMarks: [-60, -30, -18, -12, -6, 0, 6, 12]
                            vertical: true
                        }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Label { text: qsTr("Oscillator frequency") }
                        Tk.Knob {
                            binding: frequency
                            tooltip: qsTr("Oscillator frequency")
                            stepSize: 1
                            decimals: 0
                        }
                    }
                }
            }
            Tk.Card {
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Label { text: qsTr("Measured test tone"); font.pixelSize: Tk.Theme.font.small }
                    Tk.AudioMeter {
                        width: Tk.Theme.size.fieldWidth * 2
                        meter: toneMeter
                        tooltip: qsTr("Sample peak and RMS by channel")
                    }
                    Tk.Label { text: qsTr("Hold, fall and the clip latch come from the meter model."); muted: true }
                }
            }
        }
        Tk.Card {
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Label { text: qsTr("BS.1770 momentary loudness"); font.pixelSize: Tk.Theme.font.small }
                Tk.LoudnessHistory {
                    Tk.Flex.grow: 1
                    height: Tk.Theme.size.controlLg * 4
                    loudness: toneLoudness
                    targetProfile: qsTr("Example band only; not a delivery specification")
                    minimum: -60; maximum: 0; targetMinimum: -24; targetMaximum: -18
                    tooltip: qsTr("Momentary loudness of the generated test tone")
                    onExportRequested: (metric, units, window, layout, profile, values) => {
                        root.gestureStatus = qsTr("Report: %1, %2, %3 readings").arg(metric).arg(units).arg(values.length)
                    }
                }
            }
        }
        Tk.StatusField { text: root.gestureStatus; muted: true }
    }
}
