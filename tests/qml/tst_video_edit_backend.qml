// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-071: TrimView, TransitionEditor and MulticamView bound to their
// backends perform real TimelineModel edits (docs/sequence-edits.md).
// 25 fps; one frame is 28,224,000 ticks. Angle media are this folder's QML
// files, so the angles exist on disk without decoding anything.
TestCase {
    id: root
    name: "VideoEditBackend"
    visible: true
    width: 1000
    height: 1400
    when: windowShown

    readonly property string frame: "28224000"
    function frames(n) { return String(n * 28224000) }
    function uid(n) {
        const digits = String(n)
        return "00000000-0000-4000-8000-" + "000000000000".slice(digits.length) + digits
    }
    function clipMap(n, track, kind, start, duration, offset, link) {
        return {id: uid(n), trackId: uid(track), kind: kind, start: frames(start), duration: frames(duration),
                sourceOffset: frames(offset), sourceDuration: frames(100), resourceId: "missing-" + n,
                linkId: uid(link)}
    }

    Tk.TimeMap { id: timeMap; sampleRate: 48000 }
    Tk.TimelineModel { id: timeline; timeMap: timeMap }
    Tk.TimelineModel { id: program; timeMap: timeMap }
    Tk.SequenceEditor { id: editor; timeline: timeline }
    Tk.MulticamModel { id: multicam; timeline: program; trackId: root.uid(1) }

    Tk.TrimView { id: trim; editor: editor; width: 920; height: 460 }
    Tk.TransitionEditor { id: transitionView; editor: editor; y: 470; width: 640; height: 360 }
    Tk.MulticamView { id: multicamView; multicam: multicam; y: 840; width: 640; height: 520 }

    function init() {
        verify(timeMap.setFrameRate(25, 1))
        const loaded = timeline.loadContent({
            timebase: {numerator: "705600000", denominator: "1"},
            tracks: [{id: uid(1), name: "V1", kind: "video"}, {id: uid(2), name: "A1", kind: "audio"}],
            clips: [clipMap(11, 1, "video", 0, 50, 10, 91), clipMap(12, 1, "video", 50, 50, 20, 92),
                    clipMap(21, 2, "audio", 0, 50, 10, 91), clipMap(22, 2, "audio", 50, 50, 20, 92)]})
        verify(loaded.ok, loaded.error)
    }

    function test_trim_view_commits_a_linked_roll() {
        verify(editor.selectEditPoint(uid(11), uid(12)))
        verify(trim.snapshotValid)
        compare(trim.effectiveEdit.missingLeft, true) // offline media is shown, not blocking
        trim.mode = "roll"
        trim.deltaTicks = frames(5)
        const preview = findChild(trim, "trimPreview")
        verify(preview.enabled)
        mouseClick(preview)
        compare(editor.trimStatus, "ready")
        compare(trim.effectiveEdit.cutTicks, frames(55))
        compare(timeline.clipData(uid(11)).duration, frames(50)) // still a preview
        const commit = findChild(trim, "trimCommit")
        verify(commit.enabled)
        mouseClick(commit)
        compare(timeline.clipData(uid(11)).duration, frames(55))
        compare(timeline.clipData(uid(21)).duration, frames(55))
        compare(timeline.clipData(uid(22)).sourceOffset, frames(25))
        compare(timeline.historyCount, 1)
        compare(findChild(trim, "trimStatus").text, "Committed to the sequence")
        verify(timeline.undo())
        compare(timeline.clipData(uid(11)).duration, frames(50))
    }

    function test_trim_view_slip_side_and_cancel() {
        verify(editor.selectEditPoint(uid(11), uid(12)))
        trim.mode = "slip"
        trim.side = "right"
        verify(findChild(trim, "trimSide").visible)
        trim.deltaTicks = frames(3)
        verify(trim.requestPreview())
        compare(timeline.effectiveClipData(uid(12)).sourceOffset, frames(23))
        const cancel = findChild(trim, "trimCancel")
        verify(cancel.enabled)
        mouseClick(cancel)
        verify(!timeline.editing)
        compare(timeline.clipData(uid(12)).sourceOffset, frames(20))
        compare(timeline.historyCount, 0)
    }

    function test_transition_editor_edits_the_cut() {
        editor.transitionClipId = uid(12)
        verify(transitionView.validSnapshot)
        verify(!transitionView.hasTransition)
        verify(transitionView.edit("type", "dissolve"))
        verify(transitionView.hasTransition)
        compare(transitionView.durationTicks, frames(25)) // one second, placed on whole frames
        compare(transitionView.alignment, "center")
        compare(timeline.clipData(uid(12)).transition.beforeCut, frames(13))
        verify(transitionView.edit("alignment", "start"))
        compare(timeline.clipData(uid(12)).transition.afterCut, frames(25))
        // B's 20-frame head handle cannot feed a 25-frame lead-in.
        verify(!transitionView.edit("alignment", "end"))
        verify(transitionView.effectiveError.length > 0)
        compare(timeline.clipData(uid(12)).transition.afterCut, frames(25))
        const remove = findChild(transitionView, "transitionRemove")
        verify(remove.visible && remove.enabled)
        mouseClick(remove)
        verify(!transitionView.hasTransition)
        compare(timeline.historyCount, 3)
    }

    function test_multicam_view_switches_the_program() {
        const loaded = program.loadContent({timebase: {numerator: "705600000", denominator: "1"},
                                            tracks: [{id: uid(1), name: "V1", kind: "video"}], clips: []})
        verify(loaded.ok, loaded.error)
        if (multicam.angleCount === 0) {
            verify(multicam.addAngle({angleId: "one", label: "Wide", resourceId: String(Qt.resolvedUrl("tst_video_edit_backend.qml")),
                                      syncOffsetTicks: "0", sourceDurationTicks: frames(200)}))
            verify(multicam.addAngle({angleId: "two", label: "Close", resourceId: String(Qt.resolvedUrl("tst_video_edit_surfaces.qml")),
                                      syncOffsetTicks: frames(10), sourceDurationTicks: frames(200)}))
        }
        verify(multicam.insertProgram("one", "0", frames(100)).ok, multicam.lastError)
        multicam.playheadTicks = frames(20)
        compare(multicamView.effectiveActiveAngleId, "one")
        verify(multicamView.snapshotValid)
        const card = findChild(multicamView, "multicamAngle_two")
        verify(card.enabled)
        mouseClick(card)
        compare(multicam.activeAngleId, "two")
        compare(multicamView.effectiveActiveAngleId, "two")
        compare(multicam.angleAt(frames(19)), "one")
        compare(program.clipCount, 2)
        compare(multicam.unsyncedClipIds().length, 0)
        verify(!findChild(multicamView, "multicamAngle_two").enabled) // now the program angle
    }
}
