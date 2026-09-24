// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Gesture lifetimes in EditTimeline: every drag is one model session that
// ends exactly once, whatever happens to delegates, focus or the model.
// Geometry: header column at x 0, lanes at x 192, both from y 44 (compact),
// 56 px tracks, 100 px per second from origin 0.
TestCase {
    id: root
    name: "EditTimelineGestures"
    when: windowShown
    visible: true
    width: 800
    height: 400

    readonly property string second: "705600000"
    function ticks(seconds) { return String(Math.round(seconds * 705600000)) }
    function uuid(prefix, n) { return prefix + "-0000-4000-8000-" + String(n).padStart(12, "0") }
    function trackId(n) { return uuid("10000000", n) }
    readonly property string clipA: uuid("20000000", 1)
    readonly property string cue: uuid("30000000", 1)

    Tk.TimelineModel { id: session }
    Tk.TimelineModel { id: other }
    Tk.EditTimeline {
        id: timeline
        width: root.width
        height: root.height
        model: session
    }
    SignalSpy { id: rejected; target: timeline; signalName: "editRejected" }

    function load(trackCount) {
        const tracks = []
        for (let i = 1; i <= trackCount; ++i) tracks.push({ id: trackId(i), name: "T" + i, kind: "audio" })
        const result = session.loadContent({
            timebase: { numerator: root.second, denominator: "1" },
            tracks: tracks,
            clips: [{ id: clipA, trackId: trackId(1), start: ticks(1), duration: ticks(1), label: "A", kind: "audio" }],
            markers: [{ id: cue, position: ticks(5), label: "Cue" }] })
        verify(result.ok, result.error)
        timeline.model = session
        timeline.viewport.originTicks = "0"
        timeline.viewport.pixelsPerSecond = 100
        timeline.viewport.scrollY = 0
        timeline.snapEnabled = false
        timeline.playheadTicks = "0"
        rejected.clear()
        waitForRendering(timeline)
    }
    function init() {
        const editor = findChild(timeline, "timelineTimingEditor")
        if (editor && editor.visible) {
            editor.close()
            tryCompare(editor, "visible", false)
        }
        load(3)
    }
    function headers() { return findChild(timeline, "timelineTrackHeaders") }
    function order() { return session.trackIds().map(function(id) { return Number(id.slice(-2)) }).join(",") }

    function test_headerReorderFollowsItsOwnSlot() {
        // B spans y 56..112. Staying above its midpoint but inside its own
        // slot previews and commits nothing.
        mousePress(headers(), 40, 84)
        mouseMove(headers(), 40, 70)
        mouseMove(headers(), 40, 60)
        verify(headers().gestureActive)
        compare(timeline.viewport.trackIdAt(1), trackId(2))
        mouseRelease(headers(), 40, 60)
        compare(order(), "1,2,3")
        compare(session.historyCount, 0)
        compare(rejected.count, 0)
        // Past the midpoint of A it moves before A: one undo step.
        mousePress(headers(), 40, 84)
        mouseMove(headers(), 40, 70)
        mouseMove(headers(), 40, 20)
        compare(timeline.viewport.trackIdAt(0), trackId(2)) // previewed before commit
        mouseRelease(headers(), 40, 20)
        compare(order(), "2,1,3")
        compare(session.historyCount, 1)
        // Escape cancels, and the rest of that press does nothing.
        mousePress(headers(), 40, 28)
        mouseMove(headers(), 40, 60)
        mouseMove(headers(), 40, 100)
        verify(session.editing)
        keyClick(Qt.Key_Escape)
        verify(!session.editing)
        mouseMove(headers(), 40, 150)
        verify(!session.editing)
        mouseRelease(headers(), 40, 150)
        compare(order(), "2,1,3")
        compare(session.historyCount, 1)
    }

    function test_headerDragOutlivesItsDelegate() {
        load(20)
        const column = headers()
        mousePress(column, 40, 84)
        mouseMove(column, 40, 100)
        mouseMove(column, 40, column.height + 250)
        verify(session.editing)
        // The preview put the track below the visible rows: its delegate is
        // gone, but the column still owns the gesture.
        tryVerify(function() { return !findChild(timeline, "trackHeader_" + trackId(2)) })
        const scrolled = timeline.viewport.scrollY
        column.autoScrollStep(0.25)
        verify(timeline.viewport.scrollY > scrolled)
        mouseRelease(column, 40, column.height + 250)
        verify(!session.editing)
        verify(!timeline.dragging)
        compare(session.historyCount, 1)
        verify(session.trackIds().indexOf(trackId(2)) > 1)
        verify(session.applyEdit({ kind: "trackProperty", trackId: trackId(3), property: "muted", value: true }).ok)
    }

    function test_resizeSeamClickIsNotAnEdit() {
        const seam = Tk.Theme.size.seamTarget / 2
        const y = 112 - seam / 2 // inside B's bottom seam
        mousePress(headers(), 40, y)
        mouseRelease(headers(), 40, y)
        compare(rejected.count, 0)
        compare(timeline.statusText, "")
        compare(session.historyCount, 0)
        verify(!session.editing)
        // A real drag is one step, previewed live.
        mousePress(headers(), 40, y)
        mouseMove(headers(), 40, y + 10)
        mouseMove(headers(), 40, y + 20)
        verify(findChild(timeline, "trackHeader_" + trackId(2)).resizing)
        tryCompare(timeline.viewport, "contentHeight", 3 * 56 + 20)
        mouseRelease(headers(), 40, y + 20)
        compare(session.trackData(trackId(2)).height, 76)
        compare(session.historyCount, 1)
        compare(rejected.count, 0)
    }

    function test_markerEscapeEndsThePress() {
        const lane = timeline.markerLane
        mousePress(lane, 500, 11)
        mouseMove(lane, 470, 11)
        mouseMove(lane, 450, 11)
        verify(lane.dragging)
        keyClick(Qt.Key_Escape)
        verify(!lane.dragging)
        mouseMove(lane, 420, 11)
        verify(!lane.dragging)
        verify(!session.editing)
        mouseRelease(lane, 400, 11)
        compare(session.markerData(cue).position, ticks(5))
        compare(session.historyCount, 0)
    }

    function test_replacingTheModelCancelsEveryGesture() {
        verify(other.loadContent({ timebase: { numerator: root.second, denominator: "1" },
                                   tracks: [{ id: trackId(9), name: "X", kind: "audio" }] }).ok)
        const lanes = timeline.lanes
        mousePress(lanes, 150, 28)
        mouseMove(lanes, 180, 28)
        mouseMove(lanes, 220, 28)
        verify(session.editing)
        timeline.model = other
        verify(!session.editing)
        verify(!timeline.dragging)
        mouseRelease(lanes, 220, 28)
        compare(session.clipData(clipA).start, ticks(1))
        compare(other.historyCount, 0)
        // The marker lane and the header column give their sessions back too.
        timeline.model = session
        const lane = timeline.markerLane
        mousePress(lane, 500, 11)
        mouseMove(lane, 470, 11)
        mouseMove(lane, 450, 11)
        verify(session.editing)
        timeline.model = other
        verify(!session.editing)
        mouseRelease(lane, 450, 11)
        timeline.model = session
        mousePress(headers(), 40, 84)
        mouseMove(headers(), 40, 70)
        mouseMove(headers(), 40, 20)
        verify(session.editing)
        timeline.model = other
        verify(!session.editing)
        mouseRelease(headers(), 40, 20)
        timeline.model = session
        compare(order(), "1,2,3")
        compare(session.markerData(cue).position, ticks(5))
        compare(session.historyCount, 0)
    }

    function test_addMarkerAtAnOccupiedTickSelectsTheNewOne() {
        const lane = timeline.markerLane
        lane.forceActiveFocus()
        timeline.playheadTicks = ticks(5) // the cue sits here
        keyClick(Qt.Key_M)
        compare(session.markerCount, 2)
        verify(lane.currentMarkerId !== cue)
        compare(session.markerData(lane.currentMarkerId).label, "Marker")
        keyClick(Qt.Key_Delete)
        compare(session.markerCount, 1)
        compare(session.markerData(cue).label, "Cue")
    }

    function test_timingEditorRefusesValuesItNeverShowed() {
        const editor = findChild(timeline, "timelineTimingEditor")
        verify(timeline.openTimingEditor(clipA))
        tryCompare(editor, "opened", true)
        // Someone else moves the clip while the editor is open.
        verify(session.applyEdit({ kind: "move", clipIds: [clipA], grabTicks: ticks(1) },
                                 { pointerTicks: ticks(2), snap: false }).ok)
        verify(!editor.apply())
        verify(editor.errorText.length > 0)
        compare(session.clipData(clipA).start, ticks(2)) // the newer value stands
        compare(session.historyCount, 1)
    }
}
