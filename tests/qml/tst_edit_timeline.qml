// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Pointer, keyboard and assistive editing through EditTimeline. Geometry:
// lanes at x 192, y 44 (compact), 100 px per second from origin 0, 56 px tracks.
TestCase {
    id: root
    name: "EditTimeline"
    when: windowShown
    visible: true
    width: 800
    height: 400

    readonly property string second: "705600000"
    function ticks(seconds) { return String(Math.round(seconds * 705600000)) }
    function uuid(prefix, n) { return prefix + "-0000-4000-8000-" + String(n).padStart(12, "0") }
    readonly property string trackA: uuid("10000000", 1)
    readonly property string trackB: uuid("10000000", 2)
    readonly property string trackC: uuid("10000000", 3)
    readonly property string clipA: uuid("20000000", 1)
    readonly property string clipB: uuid("20000000", 2)
    readonly property string clipC: uuid("20000000", 3)
    readonly property string cue: uuid("30000000", 1)

    Tk.TimelineModel { id: session }
    Tk.TimelineMediaProvider { id: media; referenceBackend: true }
    Tk.EditTimeline {
        id: timeline
        width: root.width
        height: root.height
        model: session
        mediaProvider: media
        onSeekRequested: function(ticks) { timeline.playheadTicks = ticks }
    }
    SignalSpy { id: rejected; target: timeline; signalName: "editRejected" }

    function load() {
        const result = session.loadContent({
            timebase: { numerator: root.second, denominator: "1" },
            tracks: [{ id: trackA, name: "Dialogue", kind: "audio" }, { id: trackB, name: "Music", kind: "audio" },
                     { id: trackC, name: "Picture", kind: "video" }],
            clips: [{ id: clipA, trackId: trackA, start: ticks(1), duration: ticks(1), label: "Line 1", kind: "audio", resourceId: "a" },
                    { id: clipB, trackId: trackA, start: ticks(3), duration: ticks(1), label: "Line 2", kind: "audio", resourceId: "b" },
                    { id: clipC, trackId: trackB, start: ticks(1), duration: ticks(2), label: "Score", kind: "audio", resourceId: "c" }],
            markers: [{ id: cue, position: ticks(5), label: "Cue" }] })
        verify(result.ok, result.error)
        timeline.viewport.originTicks = "0"
        timeline.viewport.pixelsPerSecond = 100
        timeline.snapEnabled = true
        timeline.rippleMode = false
        timeline.playheadTicks = "0"
        timeline.selectionModel.clear()
        timeline.currentClipId = ""
        timeline.currentTrackId = ""
        rejected.clear()
        waitForRendering(timeline)
    }
    function init() {
        const editor = findChild(timeline, "timelineTimingEditor")
        if (editor && editor.visible) {
            editor.close()
            tryCompare(editor, "visible", false)
        }
        load()
    }
    function lanes() { return timeline.lanes }
    function clipItem(id) { return findChild(timeline.lanes, "clip_" + id) }
    function drag(item, fromX, fromY, toX, toY) {
        mousePress(item, fromX, fromY)
        mouseMove(item, (fromX + toX) / 2, (fromY + toY) / 2)
        mouseMove(item, toX, toY)
        mouseRelease(item, toX, toY)
    }

    function test_geometryAndBoundedDelegates() {
        compare(lanes().x, 192)
        compare(lanes().y, 44)
        compare(timeline.viewport.viewportWidth, 600)
        compare(timeline.viewport.clips.count, 3)
        const item = clipItem(clipA)
        verify(item)
        compare(item.x, 100)
        compare(item.width, 100)
        compare(item.Accessible.role, Accessible.ListItem)
        compare(item.Accessible.name, "Line 1")
        verify(item.Accessible.description.indexOf("Starts") === 0)
        const header = findChild(timeline, "trackHeader_" + trackA)
        verify(header)
        compare(header.y, 0)
        compare(findChild(header, "trackMute").Accessible.name, "Mute")
    }

    function test_pointerMoveSnapsCommitsOnceAndUndoes() {
        const history = session.historyCount
        mousePress(lanes(), 350, 28)
        mouseMove(lanes(), 300, 28)
        mouseMove(lanes(), 255, 28)
        verify(timeline.dragging)
        verify(lanes().preview.snapped)
        compare(lanes().preview.snapKind, "clipEnd")
        compare(session.clipData(clipB).start, ticks(3)) // committed state untouched by preview
        mouseRelease(lanes(), 255, 28)
        compare(session.clipData(clipB).start, ticks(2))
        compare(session.historyCount, history + 1)
        compare(timeline.selectedClipIds, [clipB])
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(session.clipData(clipB).start, ticks(3))
        keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
        compare(session.clipData(clipB).start, ticks(2))
    }

    function test_escapeAndLostGrabCancelWithoutAnEdit() {
        const revision = session.revision
        mousePress(lanes(), 200, 84)
        mouseMove(lanes(), 230, 84)
        mouseMove(lanes(), 260, 84)
        verify(timeline.dragging)
        verify(session.compareTicks(session.effectiveClipData(clipC).start, ticks(1)) > 0)
        keyClick(Qt.Key_Escape)
        verify(!timeline.dragging)
        mouseRelease(lanes(), 260, 84)
        compare(session.clipData(clipC).start, ticks(1))
        compare(session.clipData(clipC).trackId, trackB)
        compare(session.revision, revision)
        verify(!session.editing)
        // A model reload during a drag retires the gesture and reports why.
        mousePress(lanes(), 150, 28)
        mouseMove(lanes(), 180, 28)
        mouseMove(lanes(), 220, 28)
        verify(timeline.dragging)
        load()
        verify(!timeline.dragging)
        mouseRelease(lanes(), 220, 28)
        compare(session.clipData(clipA).start, ticks(1))
    }

    function test_trimClampsAndKindPolicyRejects() {
        timeline.snapEnabled = false
        drag(lanes(), 198, 28, 150, 28) // trim A's end from 2 s to 1.52 s
        compare(session.clipData(clipA).duration, ticks(0.52))
        drag(lanes(), 102, 28, 400, 28) // head past the tail clamps at the minimum
        compare(session.clipData(clipA).duration, "1")
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(session.clipData(clipA).duration, ticks(1))
        drag(lanes(), 150, 28, 150, 140) // an audio clip onto the video track
        compare(session.clipData(clipA).trackId, trackA)
        verify(rejected.count > 0)
        verify(timeline.statusText.length > 0)
    }

    function test_keyboardNavigationNudgeSplitAndDelete() {
        timeline.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(timeline.currentTrackId, trackA)
        compare(timeline.currentClipId, clipA)
        keyClick(Qt.Key_Right)
        compare(timeline.currentClipId, clipB)
        compare(timeline.selectedClipIds, [clipB])
        verify(clipItem(clipB).current)
        verify(clipItem(clipB).Accessible.selected)
        // Alt+arrows move to the next/previous frame line; an off-grid clip is
        // quantized by its first step.
        const next = session.stepTicks(ticks(3), "frames", 1)
        keyClick(Qt.Key_Right, Qt.AltModifier)
        compare(session.clipData(clipB).start, next)
        keyClick(Qt.Key_Left, Qt.AltModifier)
        compare(session.clipData(clipB).start, session.stepTicks(next, "frames", -1))
        keyClick(Qt.Key_Down)
        compare(timeline.currentClipId, clipC)
        timeline.playheadTicks = ticks(2)
        const count = session.clipCount
        keyClick(Qt.Key_S)
        compare(session.clipCount, count + 1)
        compare(session.clipData(clipC).duration, ticks(1))
        keyClick(Qt.Key_Delete)
        compare(session.clipCount, count)
        verify(!session.hasClip(clipC))
        compare(timeline.selectionModel.selectedIds.indexOf(clipC), -1)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(session.clipData(clipC).duration, ticks(2))
        timeline.selectClip(clipC, Qt.NoModifier)
        keyClick(Qt.Key_Up, Qt.AltModifier) // audio clip onto the other audio track
        compare(session.clipData(clipC).trackId, trackA)
        compare(session.clipData(clipC).start, ticks(1))
    }

    function test_rangeCreationCrossingAndCancel() {
        const band = findChild(lanes(), "timelineEditRange")
        verify(!band.visible)
        drag(lanes(), 100, 140, 300, 140) // empty picture lane: a new edit range
        compare(session.editRangeStart, ticks(1))
        compare(session.editRangeEnd, ticks(3))
        timeline.snapEnabled = false
        // The handles move while dragged, so moves are sent in lane coordinates.
        const handle = findChild(band, "timeSelectionHandle_end")
        verify(handle)
        mousePress(handle, handle.width / 2, 20)
        mouseMove(lanes(), 175, 140)
        mouseMove(lanes(), 50, 140) // the end is dragged past the start
        mouseRelease(lanes(), 50, 140)
        compare(session.editRangeStart, ticks(0.5))
        compare(session.editRangeEnd, ticks(1))
        const start = findChild(band, "timeSelectionHandle_start")
        mousePress(start, start.width / 2, 20)
        mouseMove(lanes(), 80, 140)
        mouseMove(lanes(), 120, 140)
        verify(band.dragging)
        keyClick(Qt.Key_Escape)
        mouseRelease(lanes(), 120, 140)
        compare(session.editRangeStart, ticks(0.5))
        compare(session.editRangeEnd, ticks(1))
        start.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(session.editRangeStart, session.stepTicks(ticks(0.5), "frames", 1))
        compare(start.Accessible.role, Accessible.Slider)
    }

    function test_markersMoveAddAndRemove() {
        timeline.snapEnabled = false
        const lane = timeline.markerLane
        const history = session.historyCount
        drag(lane, 500, 11, 400, 11)
        compare(session.markerData(cue).position, ticks(4))
        compare(session.historyCount, history + 1)
        lane.forceActiveFocus()
        timeline.playheadTicks = ticks(2)
        keyClick(Qt.Key_M)
        compare(session.markerCount, 2)
        compare(session.markerData(lane.currentMarkerId).position, ticks(2))
        keyClick(Qt.Key_Delete)
        compare(session.markerCount, 1)
        keyClick(Qt.Key_Right)
        compare(lane.currentMarkerId, cue)
        keyClick(Qt.Key_Right, Qt.AltModifier)
        compare(session.markerData(cue).position, session.stepTicks(ticks(4), "frames", 1))
    }

    function test_trackHeaderRequestsGoThroughTheModel() {
        const header = findChild(timeline, "trackHeader_" + trackB)
        const mute = findChild(header, "trackMute")
        mouseClick(mute)
        verify(session.trackData(trackB).muted)
        tryCompare(mute, "checked", true)
        timeline.forceActiveFocus()
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        verify(!session.trackData(trackB).muted)
        // Reordering tracks keeps clip selection by ID.
        timeline.selectionModel.select(clipC)
        timeline.currentTrackId = trackB
        keyClick(Qt.Key_Up, Qt.ControlModifier)
        compare(session.trackIds()[0], trackB)
        compare(timeline.selectedClipIds, [clipC])
        tryCompare(findChild(timeline, "trackHeader_" + trackB), "y", 0)
        tryCompare(clipItem(clipC), "y", 1)
    }

    function test_autoscrollKeepsDraggingThePreview() {
        timeline.snapEnabled = false
        mousePress(lanes(), 150, 28)
        mouseMove(lanes(), 300, 28)
        mouseMove(lanes(), 596, 28)
        const before = session.effectiveClipData(clipA).start
        lanes().autoScrollStep(0.25)
        verify(timeline.viewport.originTicks !== "0")
        const after = session.effectiveClipData(clipA).start
        verify(session.compareTicks(after, before) > 0)
        mouseRelease(lanes(), 596, 28)
        compare(session.clipData(clipA).start, after)
        compare(session.historyCount, 1)
    }

    function test_numericEntryForClipsAndRanges() {
        const editor = findChild(timeline, "timelineTimingEditor")
        verify(editor)
        verify(timeline.openTimingEditor(clipA))
        tryCompare(editor, "opened", true)
        const start = findChild(editor.contentItem, "timingStartField")
        const length = findChild(editor.contentItem, "timingLengthField")
        compare(start.text, session.formatTicks(ticks(1), "timecode", false))
        compare(length.text, session.formatDuration(ticks(1), "timecode"))
        start.text = "not a time"
        verify(!editor.apply())
        verify(editor.errorText.length > 0)
        compare(session.clipData(clipA).start, ticks(1)) // nothing changed
        start.text = "00:00:04:00"
        length.text = "00:00:00:15"
        verify(editor.apply())
        compare(session.clipData(clipA).start, session.frameStartTicksText("120"))
        compare(session.clipData(clipA).duration, session.parseDuration("00:00:00:15", "timecode").value)
        tryCompare(editor, "opened", false)
        // The edit range takes numeric start/end through the same editor.
        verify(session.setEditRange(ticks(1), ticks(2)))
        const handle = findChild(timeline.editRange, "timeSelectionHandle_end")
        handle.forceActiveFocus()
        keyClick(Qt.Key_Return)
        tryCompare(editor, "opened", true)
        compare(editor.mode, "range")
        findChild(editor.contentItem, "timingLengthField").text = "00:00:03:00"
        verify(editor.apply())
        compare(session.editRangeEnd, session.frameStartTicksText("90"))
        compare(session.editRangeStart, ticks(1)) // untouched: exact, not the displayed frame
        tryCompare(editor, "visible", false)
    }

    function test_markerDragAutoscrolls() {
        timeline.snapEnabled = false
        const lane = timeline.markerLane
        mousePress(lane, 500, 11)
        mouseMove(lane, 540, 11)
        mouseMove(lane, 596, 11)
        verify(lane.dragging)
        const before = session.editPreview.appliedDelta
        lane.autoScrollStep(0.25)
        verify(timeline.viewport.originTicks !== "0")
        verify(session.compareTicks(session.editPreview.appliedDelta, before) > 0)
        mouseRelease(lane, 596, 11)
        verify(!lane.dragging)
        verify(session.compareTicks(session.markerData(cue).position, ticks(5.96)) > 0)
        compare(session.historyCount, 1)
    }

    function test_copyPasteThroughTheClipboard() {
        timeline.forceActiveFocus()
        timeline.selectClip(clipA, Qt.NoModifier)
        keyClick(Qt.Key_C, Qt.ControlModifier)
        tryCompare(timeline.clipboard, "clipCount", 1)
        timeline.currentTrackId = trackB
        timeline.playheadTicks = ticks(4)
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(session.clipCount, 4)
        const pasted = timeline.currentClipId
        verify(pasted !== clipA)
        compare(session.clipData(pasted).start, ticks(4))
        compare(session.clipData(pasted).trackId, trackB)
        compare(timeline.selectedClipIds, [pasted])
        keyClick(Qt.Key_X, Qt.ControlModifier) // cut = copy + one delete
        compare(session.clipCount, 3)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(session.clipCount, 4)
    }
}
