// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Scale bounds of a live EditTimeline and the standalone contracts of
// ClipItem, TrackHeader and TimeSelection.
TestCase {
    id: root
    name: "EditTimelineParts"
    when: windowShown
    visible: true
    width: 1000
    height: 600

    function ticks(seconds) { return String(Math.round(seconds * 705600000)) }
    function uuid(prefix, n) { return prefix + "-0000-4000-8000-" + String(n).padStart(12, "0") }
    function liveClipDelegates(lanes) {
        let count = 0
        for (let i = 0; i < lanes.children.length; ++i)
            if (String(lanes.children[i].objectName).indexOf("clip_") === 0) ++count
        return count
    }

    Tk.TimelineModel { id: large }
    Tk.TimelineModel { id: small }
    Tk.TimelineMediaProvider { id: media; referenceBackend: true }
    Tk.TimelineViewport {
        id: smallView
        model: small
        viewportWidth: 600
        viewportHeight: 200
        pixelsPerSecond: 100
    }
    Component {
        id: timelineComponent
        Tk.EditTimeline {
            width: root.width
            height: root.height
            model: large
            mediaProvider: media
        }
    }
    Component {
        id: clipComponent
        Tk.ClipItem {
            width: 200
            height: 56
            clipId: root.uuid("20000000", 9)
            label: "Probe"
            kind: "audio"
            resourceId: "probe"
            visibleSourceStartTicks: "0"
            visibleSourceEndTicks: root.ticks(2)
            mediaProvider: media
        }
    }
    Tk.TrackHeader {
        id: header
        y: 0
        width: 192
        height: 56
        trackId: root.uuid("10000000", 1)
        name: "Drums"
        kind: "audio"
        muted: true
    }
    SignalSpy { id: requests; target: header; signalName: "propertyRequested" }
    Tk.TimeSelection {
        id: range
        y: 100
        width: 600
        height: 60
        viewport: smallView
        model: small
        snapEnabled: false
        allowEmpty: false
        kind: "loop"
        startTicks: root.ticks(1)
        endTicks: root.ticks(2)
    }
    SignalSpy { id: committedRange; target: range; signalName: "rangeCommitted" }
    SignalSpy { id: rejectedRange; target: range; signalName: "rangeRejected" }
    SignalSpy { id: canceledRange; target: range; signalName: "rangeCanceled" }

    function initTestCase() {
        const tracks = [], clips = []
        for (let t = 0; t < 100; ++t) {
            tracks.push({ id: uuid("10000000", t), name: "Track " + t, kind: "audio" })
            for (let c = 0; c < 100; ++c)
                clips.push({ id: uuid("20000000", t * 100 + c), trackId: uuid("10000000", t),
                             start: ticks(c * 2), duration: ticks(1), label: "Clip " + c,
                             kind: "audio", resourceId: "demo:" + (t % 5) })
        }
        const started = Date.now()
        verify(large.loadContent({ tracks: tracks, clips: clips }).ok)
        console.log("loadContent(100 tracks, 10,000 clips) from QML: " + (Date.now() - started) + " ms")
        verify(small.loadContent({ tracks: [{ id: uuid("10000000", 1), name: "One" }] }).ok)
    }

    function test_largeSessionKeepsDelegatesBounded() {
        const started = Date.now()
        const timeline = createTemporaryObject(timelineComponent, root)
        verify(timeline)
        timeline.viewport.pixelsPerSecond = 100
        waitForRendering(timeline)
        console.log("EditTimeline over 10,000 clips ready in " + (Date.now() - started) + " ms")
        const lanes = timeline.lanes
        compare(large.clipCount, 10000)
        // 600 - 44 - 8 = 548 px of 56 px tracks -> 10 rows; 800 px lanes at
        // 100 px/s plus 72 px overscan each side -> 5 clips per track.
        compare(timeline.viewport.tracks.count, 10)
        compare(timeline.viewport.clips.count, 50)
        compare(liveClipDelegates(lanes), timeline.viewport.clips.count)
        timeline.viewport.originTicks = ticks(100)
        timeline.viewport.scrollY = 2000
        waitForRendering(timeline)
        compare(liveClipDelegates(lanes), timeline.viewport.clips.count)
        verify(liveClipDelegates(lanes) <= 60)
        verify(findChild(lanes, "clip_" + uuid("20000000", 36 * 100 + 50)))
        timeline.viewport.fitToContent()
        waitForRendering(timeline)
        verify(timeline.viewport.clips.count <= timeline.viewport.maximumClipRows)
        compare(liveClipDelegates(lanes), timeline.viewport.clips.count)
        console.log("live clip delegates: " + liveClipDelegates(lanes) + " at 100 px/s; "
                    + timeline.viewport.clips.count + " rows (" + timeline.viewport.summarizedClips
                    + " clips summarized) fitted; " + timeline.viewport.tracks.count + " track headers")
        // Keyboard navigation reveals an offscreen clip before it is current.
        timeline.viewport.pixelsPerSecond = 100
        timeline.forceActiveFocus()
        timeline.currentTrackId = uuid("10000000", 99)
        timeline.currentClipId = ""
        keyClick(Qt.Key_End)
        compare(timeline.currentClipId, uuid("20000000", 9999))
        waitForRendering(timeline)
        const item = findChild(lanes, "clip_" + uuid("20000000", 9999))
        verify(item)
        verify(item.current)
        verify(timeline.Accessible.description.indexOf("Track 99") > 0)
    }

    function test_clipItemRequestsAndReleasesPeaks() {
        const before = Number(media.cancelledCount)
        const item = createTemporaryObject(clipComponent, root)
        verify(item)
        tryCompare(item, "mediaState", "ready")
        compare(item.peaks.length, item.columns)
        item.visibleSourceEndTicks = ticks(3) // new range: new request supersedes
        tryCompare(item, "mediaState", "ready")
        item.destroy()
        wait(0)
        // Superseded requests (initialization, the new range) and the final one
        // are all released; the provider keeps no record for the dead delegate.
        verify(Number(media.cancelledCount) >= before + 2)
        compare(media.requestCount, 0)
    }

    function test_trackHeaderEmitsRequestsOnly() {
        mouseClick(findChild(header, "trackMute"))
        compare(requests.count, 1)
        compare(requests.signalArguments[0][1], "muted")
        compare(requests.signalArguments[0][2], false)
        verify(header.muted) // owner state is unchanged until the owner applies it
        compare(findChild(header, "trackSolo").Accessible.name, "Solo")
        compare(findChild(header, "trackResize").Accessible.role, Accessible.Splitter)
        compare(header.Accessible.name, "Drums")
    }

    function test_timeSelectionCrossingEmptyAndCancel() {
        verify(range.beginDrag("end", range.endTicks))
        range.updateDrag(ticks(0.5)) // past the start: the anchor stays, the range normalizes
        compare(range.draftStartTicks, ticks(0.5))
        compare(range.draftEndTicks, ticks(1))
        verify(range.commitDrag())
        compare(committedRange.signalArguments[0][0], ticks(0.5))
        compare(committedRange.signalArguments[0][1], ticks(1))
        verify(range.beginDrag("end", range.endTicks))
        range.updateDrag(ticks(1)) // exactly on the anchor: empty
        verify(!range.commitDrag())
        compare(rejectedRange.count, 1)
        compare(canceledRange.count, 1)
        compare(range.draftEndTicks, range.endTicks) // the committed range shows again
        verify(range.beginDrag("start", range.startTicks))
        range.updateDrag(ticks(1.5))
        range.cancelDrag()
        compare(canceledRange.count, 2)
        compare(range.draftStartTicks, ticks(1))
        verify(!range.setNumeric("not time", "00:00:02:00"))
        compare(rejectedRange.count, 2)
        const handle = findChild(range, "timeSelectionHandle_start")
        compare(handle.Accessible.role, Accessible.Slider)
        compare(handle.Accessible.name, "Loop range start")
    }
}
