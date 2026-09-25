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

    Component {
        id: smallWaveformComponent
        Tk.WaveformEditor {
            width: 400
            height: 120
            sourceId: "media:small"
            channelId: "1"
            selectionStartSample: "50"
            selectionEndSample: "150"
            columns: [
                { minimum: -0.5, maximum: 0.5, startSample: "0", endSample: "100" },
                { minimum: -0.25, maximum: 0.25, startSample: "100", endSample: "200" }
            ]
        }
    }

    function test_editKeysRequestDomainOperations() {
        const waveform = createTemporaryObject(smallWaveformComponent, root)
        verify(waveform !== null)
        compare(waveform.fractionForSample("100"), 0.5)   // exact boundary
        compare(waveform.fractionForSample("150"), 0.75)  // between boundaries
        let requests = []
        waveform.editRequested.connect(function(id, source, channel, operation, start, end) {
            requests.push([operation, source, channel, start, end])
        })
        waveform.forceActiveFocus()
        keyClick(Qt.Key_F)
        keyClick(Qt.Key_F, Qt.ShiftModifier)
        keyClick(Qt.Key_G)
        keyClick(Qt.Key_S)
        keyClick(Qt.Key_T)
        keyClick(Qt.Key_Delete)
        compare(requests.length, 6)
        compare(requests.map(r => r[0]).join(","), "fadeIn,fadeOut,gain,silence,crop,delete")
        compare(requests[0][1], "media:small")
        compare(requests[0][2], "1")
        compare(requests[5][3], "50")
        compare(requests[5][4], "150")
        waveform.editingEnabled = false
        keyClick(Qt.Key_F)
        compare(requests.length, 6)
    }

    function test_spectralProcessRequestsUseTheCommittedRegion() {
        const spectral = createTemporaryObject(spectralComponent, root)
        verify(spectral !== null)
        let processed = []
        spectral.processRequested.connect(function(id, source, channel, operation, start, end, low, high) {
            processed.push([operation, start, end, low, high])
        })
        verify(!spectral.requestProcess("spectralGain")) // no committed region yet
        spectral.selectionStartSample = "9007199254741093"
        spectral.selectionEndSample = "9007199254741293"
        spectral.selectionLowHz = 5000
        spectral.selectionHighHz = 7000
        verify(spectral.hasSelection)
        compare(spectral.fractionForSample("9007199254741093"), 1 / 3)
        spectral.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        compare(processed.length, 1)
        compare(processed[0][0], "spectralGain")
        compare(processed[0][1], "9007199254741093")
        compare(processed[0][2], "9007199254741293")
        compare(processed[0][3], 5000)
        compare(processed[0][4], 7000)
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
