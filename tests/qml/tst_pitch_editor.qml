// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "PitchEditor"
    when: windowShown
    visible: true
    width: 520
    height: 320
    property var lastPreview: null
    property var lastCommit: null
    property string canceledId: ""

    Tk.PitchEditor {
        id: editor
        width: 500
        height: 300
        visible: true
        sourceId: "source-exact"
        channelId: "channel-1"
        analysisSnapshot: ({available:true, minimumPitchCents:6000, maximumPitchCents:7200,
            formantMinimum:0.8, formantMaximum:1.2,
            sampleBoundaries:[{x:0,sample:"900719925474099312300"},{x:0.5,sample:"900719925474099312800"},{x:1,sample:"900719925474099313300"}],
            segments:[{id:"segment-id",notes:[{id:"note-id",startSample:"900719925474099312300",endSample:"900719925474099312800",x0:0.2,x1:0.6,pitchCents:6600,formantRatio:1}],contour:[]} ]})
        onEditPreviewRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
            root.lastPreview = [gesture,source,channel,segment,note,operation,anchor,target,cents,ratio]
        }
        onEditCommitRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
            root.lastCommit = [gesture,source,channel,segment,note,operation,anchor,target,cents,ratio]
        }
        onGestureCanceled: function(gesture) { root.canceledId = gesture }
    }

    function test_edit_requests_preserve_ids_samples_and_host_state() {
        verify(editor.beginEdit("segment-id", "note-id", "time", 250, 100))
        verify(lastPreview !== null)
        compare(lastPreview[1], "source-exact")
        compare(lastPreview[3], "segment-id")
        compare(lastPreview[4], "note-id")
        compare(lastPreview[6], "900719925474099312300")
        compare(lastPreview[7], "900719925474099312800")
        verify(editor.finishEdit())
        compare(lastCommit[7], "900719925474099312800")
        compare(editor.analysisSnapshot.segments[0].notes[0].startSample, "900719925474099312300")

        verify(editor.beginEdit("segment-id", "note-id", "pitch", 250, 100))
        fuzzyCompare(editor._targetPitchCents, 6800, 0.001)
        verify(editor.finishEdit())
        verify(editor.beginEdit("segment-id", "note-id", "formant", 250, 150))
        fuzzyCompare(editor._targetFormantRatio, 1.0, 0.001)
        const canceled = editor._gestureId
        verify(editor.cancelEdit())
        compare(canceledId, canceled)
    }

    function test_pointer_drag_previews_and_commits_host_request() {
        mousePress(editor, 200, 150)
        mouseMove(editor, 220, 130)
        verify(lastPreview !== null)
        compare(lastPreview[3], "segment-id")
        compare(lastPreview[4], "note-id")
        mouseRelease(editor, 220, 130)
        verify(lastCommit !== null)
        compare(lastCommit[3], "segment-id")
        compare(lastCommit[4], "note-id")
    }
}
