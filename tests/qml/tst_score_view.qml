// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ScoreView"
    when: windowShown
    visible: true
    width: 520
    height: 300
    property var lastNote: null
    property var lastRest: null
    property var lastSelection: null

    Tk.ScoreView {
        id: score
        width: 500
        height: 280
        visible: true
        scoreId: "score-test"
        layoutSnapshot: ({available:true, systems:[{id:"sys", staves:[{
            id:"staff-stable", partId:"part-stable", top:0.2, lineGap:0.05, x0:0.1, x1:0.9,
            measures:[{id:"measure-stable", number:"7", x0:0.1, x1:0.9,
                entryPositions:[{x:0.3,ticks:"900719925474099312345"}],
                voices:[{id:"voice-stable", events:[{id:"event-stable",kind:"note",x:0.3,staffStep:4}]}]}]
        }]}]})
        activePartId: "part-stable"
        activeStaffId: "staff-stable"
        activeVoiceId: "voice-stable"
        activeMeasureId: "measure-stable"
        entryTick: "900719925474099312345"
        entryPitch: "F#5"
        entryDurationId: "dotted-quarter"
        onNoteEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, pitch, duration) {
            root.lastNote = [requestId, scoreId, partId, staffId, voiceId, measureId, ticks, pitch, duration]
        }
        onRestEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, duration) {
            root.lastRest = [requestId, scoreId, partId, staffId, voiceId, measureId, ticks, duration]
        }
        onEventSelectionRequested: function(requestId, scoreId, eventId, partId, staffId, voiceId, measureId) {
            root.lastSelection = [requestId, scoreId, eventId, partId, staffId, voiceId, measureId]
        }
    }

    function test_host_owned_selection_and_exact_note_rest_requests() {
        verify(score.requestSelection("event-stable"))
        compare(lastSelection[1], "score-test")
        compare(lastSelection[2], "event-stable")
        compare(lastSelection[3], "part-stable")
        compare(lastSelection[4], "staff-stable")
        compare(lastSelection[5], "voice-stable")
        compare(lastSelection[6], "measure-stable")
        compare(score.selectedEventId, "") // selection remains host-owned

        verify(score.requestCursorEntry())
        compare(lastNote[6], "900719925474099312345")
        compare(lastNote[7], "F#5")
        compare(lastNote[8], "dotted-quarter")
        score.entryMode = "rest"
        verify(score.requestCursorEntry())
        compare(lastRest[6], "900719925474099312345")
        compare(lastRest[7], "dotted-quarter")
    }

    function test_unavailable_layout_blocks_entry() {
        score.layoutSnapshot = ({available:false, error:"Engraving layout unsupported"})
        verify(!score.requestCursorEntry())
        verify(findChild(root, "scoreStatus").visible)
        compare(score.effectiveError, "Engraving layout unsupported")
    }
}
