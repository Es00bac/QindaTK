// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "WaveformSpectrogram"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Component {
        id: waveformComponent
        Tk.WaveformEditor {
            width: 400
            height: 120
            sourceId: "source:stable-8"
            channelId: "channel:left"
            columns: [
                { minimum: -0.5, maximum: 0.6, startSample: "9007199254740993", endSample: "9007199254741093" },
                { minimum: -0.2, maximum: 0.3, startSample: "9007199254741093", endSample: "9007199254741193" },
                { minimum: -0.8, maximum: 0.9, startSample: "9007199254741193", endSample: "9007199254741293" },
                { minimum: -0.1, maximum: 0.2, startSample: "9007199254741293", endSample: "9007199254741393" }
            ]
        }
    }
    Component {
        id: spectralComponent
        Tk.SpectrogramView {
            width: 400
            height: 200
            sourceId: "source:stable-8"
            channelId: "channel:left"
            sampleBoundaries: ["9007199254740993", "9007199254741093", "9007199254741193", "9007199254741293"]
            fftSize: 2048
            hopSize: 512
            fftWindow: "Hann"
            fftNormalization: "power"
            tiles: [{ x0: 0, x1: 1, lowHz: 100, highHz: 400, intensity: 0.7 }]
        }
    }

    function test_exactSampleRequestsAndCancel() {
        const waveform = createTemporaryObject(waveformComponent, root)
        verify(waveform !== null)
        let previews = []
        let commits = []
        let cancels = []
        waveform.selectionPreviewRequested.connect(function(id, source, channel, start, end) {
            previews = [id, source, channel, start, end]
        })
        waveform.selectionCommitted.connect(function(id, source, channel, start, end) {
            commits = [id, source, channel, start, end]
        })
        waveform.gestureCanceled.connect(function(id) { cancels.push(id) })
        verify(waveform.beginSelectionAt(105))
        waveform.updateSelectionAt(205)
        compare(previews[1], "source:stable-8")
        compare(previews[2], "channel:left")
        compare(previews[3], "9007199254741093")
        compare(previews[4], "9007199254741293")
        verify(waveform.finishSelection())
        compare(commits[0], previews[0])
        compare(commits[3], "9007199254741093")
        compare(commits[4], "9007199254741293")

        verify(waveform.beginSelectionAt(10))
        const canceledId = waveform._gestureId
        verify(waveform.cancelSelection())
        compare(cancels.length, 1)
        compare(cancels[0], canceledId)

        const spectral = createTemporaryObject(spectralComponent, root, { y: 150 })
        verify(spectral !== null)
        let spectralRequest = []
        spectral.selectionCommitted.connect(function(id, source, channel, start, end, low, high) {
            spectralRequest = [id, source, channel, start, end, low, high]
        })
        verify(spectral.beginSelectionAt(2, 20))
        verify(spectral.updateSelectionAt(300, 160))
        verify(spectral.finishSelection())
        compare(spectralRequest[1], "source:stable-8")
        compare(spectralRequest[2], "channel:left")
        compare(spectralRequest[3], "9007199254740993")
        compare(spectralRequest[4], "9007199254741293")
        verify(spectralRequest[5] < spectralRequest[6])
    }
}
