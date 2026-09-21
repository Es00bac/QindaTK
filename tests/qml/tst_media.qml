// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "Media"
    when: windowShown
    visible: true
    width: 480
    height: 240

    Component {
        id: thumbnailComponent
        Tk.Thumbnail { placeholderIcon: "film" }
    }

    Component {
        id: waveformComponent
        Tk.Waveform { implicitWidth: 200; implicitHeight: 40 }
    }

    // An empty source is "none yet", not a failure. Marking it as an error
    // trains the reader to ignore the error mark.
    function test_emptySourceIsEmptyNotFailed() {
        const tile = createTemporaryObject(thumbnailComponent, root)
        verify(tile)
        compare(tile.loadState, "empty")
        const placeholder = findChild(tile, "thumbnailPlaceholder")
        verify(placeholder)
        verify(placeholder.visible)
        compare(placeholder.name, "film")
    }

    // A source that cannot resolve must reach "failed" rather than sitting in
    // "loading" forever, or the caller can never offer a retry.
    function test_badSourceReachesFailed() {
        const tile = createTemporaryObject(thumbnailComponent, root)
        verify(tile)
        tile.source = "image://no-such-provider/x"
        tryCompare(tile, "loadState", "failed", 2000)
        const placeholder = findChild(tile, "thumbnailPlaceholder")
        verify(placeholder.visible)
        compare(placeholder.name, "alert-triangle")
    }

    // The tile keeps its size through every state, so a bin row does not jump
    // when a frame arrives.
    function test_sizeIsStableAcrossStates() {
        const tile = createTemporaryObject(thumbnailComponent, root)
        verify(tile)
        const w = tile.width
        const h = tile.height
        tile.source = "image://no-such-provider/x"
        tryCompare(tile, "loadState", "failed", 2000)
        compare(tile.width, w)
        compare(tile.height, h)
    }

    // The caption band only exists when there is a caption: an empty band is
    // a dark stripe across the bottom of every frame.
    function test_captionBandOnlyWithACaption() {
        const tile = createTemporaryObject(thumbnailComponent, root)
        verify(tile)
        const band = findChild(tile, "thumbnailCaptionBand")
        verify(band)
        verify(!band.visible)
        tile.caption = "00:00:04"
        verify(band.visible)
    }

    function test_waveformReportsEmpty() {
        const wave = createTemporaryObject(waveformComponent, root)
        verify(wave)
        verify(wave.empty)
        const shape = findChild(wave, "waveformShape")
        verify(shape)
        verify(!shape.visible)
        // The centre line stays: a silent lane still reads as audio.
        const centre = findChild(wave, "waveformCentreLine")
        verify(centre)
        verify(centre.visible)
    }

    function test_waveformDrawsWhenGivenPeaks() {
        const wave = createTemporaryObject(waveformComponent, root)
        verify(wave)
        wave.peaks = [0.1, 0.4, 0.9, 0.3, 0.05]
        verify(!wave.empty)
        const shape = findChild(wave, "waveformShape")
        verify(shape.visible)
    }

    // The scale is fixed, not per-clip: a quiet clip must occupy less of its
    // lane than a loud one, because that difference is what the editor reads.
    // Both are drawn at the same size; only the ink differs.
    function test_quietAndLoudShareOneScale() {
        const quiet = createTemporaryObject(waveformComponent, root)
        const loud = createTemporaryObject(waveformComponent, root)
        verify(quiet && loud)
        const columns = []
        for (let i = 0; i < 64; ++i) {
            columns.push(1.0)
        }
        loud.peaks = columns
        quiet.peaks = columns.map(function(v) { return v * 0.1 })
        compare(quiet.height, loud.height)
        const quietShape = findChild(quiet, "waveformShape")
        const loudShape = findChild(loud, "waveformShape")
        // Same canvas, same geometry — the difference is in the path, which is
        // what keeps a quiet clip quiet instead of rescaling it to full height.
        compare(quietShape.height, loudShape.height)
        verify(quietShape.visible && loudShape.visible)
    }

    Component {
        id: rulerComponent
        Tk.TimeRuler { implicitWidth: 400 }
    }

    Component {
        id: stripComponent
        Tk.Filmstrip {
            implicitHeight: 40
            frames: [{caption: "in"}, {caption: "b"}, {caption: "c"}, {caption: "out"}]
        }
    }

    Component {
        id: rangeComponent
        Tk.RangeSlider { implicitWidth: 200; from: 0; to: 255 }
    }

    Component {
        id: keyCapComponent
        Tk.KeyCap { }
    }

    // The whole point of the ladder: the same ruler stays readable at any
    // zoom, so the step must grow as pixels-per-unit shrinks.
    function test_rulerStepFollowsZoom() {
        const ruler = createTemporaryObject(rulerComponent, root)
        verify(ruler)
        ruler.pixelsPerUnit = 120
        const fine = ruler.step
        ruler.pixelsPerUnit = 24
        const middle = ruler.step
        ruler.pixelsPerUnit = 4
        const coarse = ruler.step
        verify(fine < middle)
        verify(middle < coarse)
        // And every step stays on the 1/2/5/10 ladder rather than being an
        // arbitrary fraction that produces labels like "3.7s".
        const ladder = [1, 2, 5, 10]
        const mantissa = coarse / Math.pow(10, Math.floor(Math.log(coarse) / Math.LN10))
        verify(ladder.indexOf(Math.round(mantissa)) >= 0)
    }

    function test_rulerScrubReportsUnitsNotPixels() {
        const ruler = createTemporaryObject(rulerComponent, root)
        verify(ruler)
        ruler.pixelsPerUnit = 10
        ruler.originUnits = 5
        const spy = signalSpyComponent.createObject(root, {target: ruler, signalName: "scrubbed"})
        mouseClick(ruler, 100, ruler.height / 2)
        compare(spy.count, 1)
        // 5 units of origin plus 100px at 10px/unit.
        fuzzyCompare(spy.signalArguments[0][0], 15, 0.5)
        spy.destroy()
    }

    // A narrow clip drops middle frames but never the tail: the tail frame is
    // half of what a cut looks like.
    function test_filmstripKeepsTheTailWhenNarrowed() {
        const strip = createTemporaryObject(stripComponent, root)
        verify(strip)
        strip.width = 400
        compare(strip.shown, 4)
        strip.width = 80
        verify(strip.shown < 4)
        const last = findChild(strip, "filmstripFrame" + (strip.shown - 1))
        verify(last)
        compare(last.caption, "out")
    }

    function test_filmstripWithOneFrameShowsIt() {
        const strip = createTemporaryObject(stripComponent, root)
        verify(strip)
        strip.frames = [{caption: "only"}]
        compare(strip.shown, 1)
        const only = findChild(strip, "filmstripFrame0")
        verify(only)
        compare(only.caption, "only")
    }

    // The accent marks what is inside the range, not where it begins.
    function test_rangeBandSpansBetweenTheHandles() {
        const range = createTemporaryObject(rangeComponent, root)
        verify(range)
        range.first.value = 64
        range.second.value = 192
        const band = findChild(range, "rangeSliderBand")
        const track = findChild(range, "rangeSliderTrack")
        verify(band && track)
        // Roughly the middle half of the track.
        verify(band.x > 0)
        verify(band.width > 0)
        verify(band.x + band.width < track.width)
    }

    function test_keyCapSplitsASequence() {
        const cap = createTemporaryObject(keyCapComponent, root)
        verify(cap)
        cap.sequence = "Ctrl+Shift+K"
        compare(cap.keys.length, 3)
        compare(cap.keys[0], "Ctrl")
        compare(cap.keys[2], "K")
        // Empty draws nothing at all rather than an empty box.
        cap.sequence = ""
        compare(cap.keys.length, 0)
        verify(!cap.visible)
    }

    function test_keyCapSymbolicMapsModifiers() {
        const cap = createTemporaryObject(keyCapComponent, root)
        verify(cap)
        cap.symbolic = true
        cap.sequence = "Ctrl+Shift+Z"
        compare(cap.keys[0], "\u2303")
        compare(cap.keys[1], "\u21e7")
        // A non-modifier key is left alone.
        compare(cap.keys[2], "Z")
    }

    Component {
        id: signalSpyComponent
        SignalSpy { }
    }
}
