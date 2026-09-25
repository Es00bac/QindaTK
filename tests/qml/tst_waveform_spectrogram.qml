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

    Component {
        id: eightColumnComponent
        Tk.WaveformEditor {
            width: 400
            height: 120
            sourceId: "media:eight"
            channelId: "0"
            selectionStartSample: "100"
            selectionEndSample: "700"
            columns: {
                const list = []
                for (let i = 0; i < 8; ++i)
                    list.push({ minimum: -0.5, maximum: 0.5, startSample: String(i * 100), endSample: String(i * 100 + 100) })
                return list
            }
        }
    }
    Component {
        id: stereoComponent
        Tk.WaveformEditor {
            width: 400
            height: 120
            sourceId: "media:stereo"
            selectionStartSample: "0"
            selectionEndSample: "200"
            columns: [{ minimum: -0.5, maximum: 0.5, startSample: "0", endSample: "100" },
                      { minimum: -0.5, maximum: 0.5, startSample: "100", endSample: "200" }]
            channelLayout: [{ id: "0", label: "L" }, { id: "1", label: "R" }]
            channelColumns: [columns, columns]
        }
    }

    function test_editKeysRequestDomainOperations() {
        const waveform = createTemporaryObject(smallWaveformComponent, root)
        verify(waveform !== null)
        compare(waveform.fractionForSample("100"), 0.5)   // exact boundary
        compare(waveform.fractionForSample("150"), 0.75)  // between boundaries
        let requests = []
        waveform.editRequested.connect(function(id, source, channel, operation, start, end, parameters) {
            requests.push([operation, source, channel, start, end, parameters])
        })
        waveform.forceActiveFocus()
        keyClick(Qt.Key_G) // 0 dB would change nothing: no request, no undo step
        compare(requests.length, 0)
        keyClick(Qt.Key_F)
        keyClick(Qt.Key_E) // equal-power curve for the next fade
        keyClick(Qt.Key_F, Qt.ShiftModifier)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down, Qt.ShiftModifier)
        compare(waveform.gainDb, -2.1)
        keyClick(Qt.Key_G)
        keyClick(Qt.Key_S)
        keyClick(Qt.Key_T)
        keyClick(Qt.Key_Delete)
        compare(requests.length, 6)
        compare(requests.map(r => r[0]).join(","), "fadeIn,fadeOut,gain,silence,crop,delete")
        compare(requests[0][1], "media:small")
        compare(requests[0][2], "1")
        compare(requests[0][5].shape, "linear")
        compare(requests[1][5].shape, "equalPower")
        compare(requests[2][5].gainDb, -2.1)
        compare(requests[5][3], "50")
        compare(requests[5][4], "150")
        waveform.editingEnabled = false
        keyClick(Qt.Key_F)
        compare(requests.length, 6)
    }

    function test_gainAndFadeHandlesRequestAmounts() {
        const waveform = createTemporaryObject(eightColumnComponent, root)
        verify(waveform !== null)
        let requests = []
        let cancels = []
        waveform.editRequested.connect(function(id, source, channel, operation, start, end, parameters) {
            requests.push({ operation, start, end, parameters })
        })
        waveform.gestureCanceled.connect(function(id) { cancels.push(id) })
        const gain = findChild(waveform, "waveformGainHandle")
        const fadeIn = findChild(waveform, "waveformFadeInHandle")
        verify(gain !== null && gain.visible)
        verify(fadeIn !== null && fadeIn.visible)
        // The handles sit on the selection: fade in at its start, gain over its middle.
        const canvas = findChild(waveform, "waveformSelectionCanvas")
        fuzzyCompare(fadeIn.x + fadeIn.width / 2, canvas.width / 8, 0.01)
        fuzzyCompare(gain.x + gain.width / 2, canvas.width / 2, 0.01)
        // Dragging the gain handle up raises the gain and requests it on release.
        mouseDrag(gain, gain.width / 2, gain.height / 2, 0, -40)
        compare(requests.length, 1)
        compare(requests[0].operation, "gain")
        verify(requests[0].parameters.gainDb > 0)
        compare(requests[0].parameters.gainDb, waveform.gainDb)
        compare(requests[0].start, "100")
        compare(requests[0].end, "700")
        // Dragging the fade-in handle two columns right: a fade over [100, 300).
        waveform.fadeShape = "equalPower"
        // (Dragged in canvas coordinates: the handle follows the pointer, so
        // coordinates relative to the handle itself would overshoot.)
        mouseDrag(canvas, fadeIn.x + fadeIn.width / 2, fadeIn.y + fadeIn.height / 2, canvas.width / 4, 0)
        compare(requests.length, 2)
        compare(requests[1].operation, "fadeIn")
        compare(requests[1].start, "100")
        compare(requests[1].end, "300")
        compare(requests[1].parameters.shape, "equalPower")
        // Fade out to the middle, snapped to the exact boundary.
        verify(waveform.beginHandleDrag("fadeOut"))
        verify(waveform.updateHandleDrag(0.49, 0))
        verify(waveform.finishHandleDrag())
        compare(requests[2].operation, "fadeOut")
        compare(requests[2].start, "400")
        compare(requests[2].end, "700")
        // Escape during a gain drag restores the gain and requests nothing.
        const before = waveform.gainDb
        waveform.forceActiveFocus()
        verify(waveform.beginHandleDrag("gain"))
        verify(waveform.updateHandleDrag(0, 40))
        compare(waveform.gainDb, before - 10)
        keyClick(Qt.Key_Escape)
        compare(waveform.gainDb, before)
        compare(cancels.length, 1)
        compare(requests.length, 3)
        // No selection: no handles.
        waveform.selectionEndSample = waveform.selectionStartSample
        verify(!gain.visible)
    }

    function test_channelLayoutSelectsAndEditsOneChannel() {
        const waveform = createTemporaryObject(stereoComponent, root)
        verify(waveform !== null)
        compare(waveform.bandCount, 2)
        let commits = []
        let requests = []
        waveform.selectionCommitted.connect(function(id, source, channel, start, end) { commits.push(channel) })
        waveform.editRequested.connect(function(id, source, channel, operation) { requests.push(channel) })
        const canvas = findChild(waveform, "waveformSelectionCanvas")
        const bandHeight = (canvas.height - Tk.Theme.size.row) / 2
        // A drag inside the second band selects the right channel only.
        verify(waveform.beginSelectionAt(20, canvas.y + bandHeight * 1.5))
        verify(waveform.finishSelection())
        compare(commits[0], "1")
        // A drag across both bands selects every channel.
        verify(waveform.beginSelectionAt(20, canvas.y + bandHeight * 0.5))
        verify(waveform.updateSelectionAt(200, canvas.y + bandHeight * 1.5))
        verify(waveform.finishSelection())
        compare(commits[1], "all")
        // Edits follow the host's committed channel.
        waveform.selectionChannelId = "1"
        verify(waveform.requestEdit("silence"))
        waveform.selectionChannelId = ""
        verify(waveform.requestEdit("silence"))
        compare(requests.join(","), "1,all")
        verify(waveform.implicitHeight > Tk.Theme.size.waveformHeight * 2)
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

    function test_selectionKeepsItsCoordinatesThroughZoom() {
        const spectral = createTemporaryObject(spectralComponent, root)
        verify(spectral !== null)
        // A drawn region, committed by the host in sample/Hz coordinates.
        spectral.selectionCommitted.connect(function(id, source, channel, start, end, low, high) {
            spectral.selectionStartSample = start
            spectral.selectionEndSample = end
            spectral.selectionLowHz = low
            spectral.selectionHighHz = high
        })
        verify(spectral.beginSelectionAt(140, 40))
        verify(spectral.updateSelectionAt(260, 120))
        verify(spectral.finishSelection())
        compare(spectral.selectionStartSample, "9007199254741093")
        compare(spectral.selectionEndSample, "9007199254741193")
        const low = spectral.selectionLowHz, high = spectral.selectionHighHz
        const overlay = findChild(spectral, "spectrogramSelection")
        const canvas = findChild(spectral, "spectrogramTileCanvas")
        verify(overlay.visible)
        fuzzyCompare(overlay.x, canvas.width / 3, 0.01)
        fuzzyCompare(overlay.width, canvas.width / 3, 0.01)
        // Zoom in (the host's new boundaries every 50 samples) and narrow the band.
        spectral.sampleBoundaries = ["9007199254741043", "9007199254741093", "9007199254741143",
                                     "9007199254741193", "9007199254741243"]
        spectral.minimumHz = 100
        spectral.maximumHz = 10000
        compare(spectral.selectionStartSample, "9007199254741093") // the selection itself never moves
        compare(spectral.selectionLowHz, low)
        fuzzyCompare(overlay.x, canvas.width / 4, 0.01)
        fuzzyCompare(overlay.width, canvas.width / 2, 0.01)
        fuzzyCompare(overlay.y, spectral.yForFrequency(high), 0.01)
        fuzzyCompare(overlay.y + overlay.height, spectral.yForFrequency(low), 0.01)
        // Scroll so the selection starts off the left edge: it is clamped, not shifted.
        spectral.sampleBoundaries = ["9007199254741143", "9007199254741193", "9007199254741243"]
        compare(overlay.x, 0)
        fuzzyCompare(overlay.width, canvas.width / 2, 0.01)
        // Processing still uses the committed coordinates.
        let processed = []
        spectral.processRequested.connect(function(id, source, channel, operation, start, end, lo, hi) {
            processed = [start, end, lo, hi]
        })
        verify(spectral.requestProcess("spectralGain"))
        compare(processed[0], "9007199254741093")
        compare(processed[1], "9007199254741193")
        compare(processed[2], low)
        compare(processed[3], high)
    }

    function test_lassoAndPaintMasks() {
        const spectral = createTemporaryObject(spectralComponent, root)
        verify(spectral !== null)
        let masks = []
        let cancels = 0
        spectral.maskCommitted.connect(function(id, source, channel, polygons, start, end, low, high) {
            masks.push({ polygons, start, end, low, high })
        })
        spectral.gestureCanceled.connect(function() { ++cancels })
        spectral.forceActiveFocus()
        keyClick(Qt.Key_L)
        compare(spectral.selectionMode, "lasso")
        verify(spectral.beginSelectionAt(50, 50))
        spectral.updateSelectionAt(150, 50)
        spectral.updateSelectionAt(150, 150)
        spectral.updateSelectionAt(50, 150)
        verify(spectral.finishSelection())
        compare(masks.length, 1)
        compare(masks[0].polygons.length, 1)
        compare(masks[0].polygons[0].length, 4)
        // x = 50 of 400 over three 100-sample columns: 37.5 → 38 samples in, exact past 2^53.
        compare(masks[0].polygons[0][0].sample, "9007199254741031")
        compare(masks[0].start, "9007199254741031")
        compare(masks[0].end, spectral.sampleAtX(150))
        verify(masks[0].low < masks[0].high)
        // The host commits the mask; Delete processes it with the mask as a parameter.
        spectral.maskPolygons = masks[0].polygons
        let processed = []
        spectral.processRequested.connect(function(id, source, channel, operation, start, end, low, high, parameters) {
            processed = [start, end, parameters]
        })
        keyClick(Qt.Key_Delete)
        compare(processed[0], masks[0].start)
        compare(processed[1], masks[0].end)
        compare(processed[2].mask.length, 1)
        // A committed mask follows zoom from its sample/Hz points.
        const before = spectral.maskPointPosition(0, 0)
        spectral.sampleBoundaries = ["9007199254740993", "9007199254741043", "9007199254741093"]
        const after = spectral.maskPointPosition(0, 0)
        verify(after.x > before.x * 1.9)
        fuzzyCompare(after.y, before.y, 0.001)
        // Paint adds one octagon per dab to the committed mask.
        keyClick(Qt.Key_B)
        verify(spectral.beginSelectionAt(100, 100))
        spectral.updateSelectionAt(100 + spectral.brushRadius, 100)
        spectral.updateSelectionAt(100 + spectral.brushRadius * 2, 100)
        spectral.updateSelectionAt(100 + spectral.brushRadius * 2.5, 100) // closer than a radius: no dab
        verify(spectral.finishSelection())
        compare(masks[1].polygons.length, 4)
        compare(masks[1].polygons[3].length, 8)
        // Escape cancels a lasso in progress; nothing is committed.
        keyClick(Qt.Key_L)
        verify(spectral.beginSelectionAt(10, 10))
        spectral.updateSelectionAt(90, 90)
        keyClick(Qt.Key_Escape)
        compare(cancels, 1)
        compare(masks.length, 2)
        compare(spectral.addSamples("9007199254740999", 12), "9007199254741011")
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
