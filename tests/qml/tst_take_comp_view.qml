// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "TakeCompView"
    when: windowShown
    property var boundaries: ["9007199254740990", "9007199254741090", "9007199254741190",
                              "9007199254741290", "9007199254741390"]
    property var lanes: [
        {takeId: "take-a", sourceId: "source-a", sourceOffsetSample: "12345678901234567", available: true},
        {takeId: "take-b", sourceId: "source-b", sourceOffsetSample: "98765432109876543", available: true}
    ]
    property var segments: [
        {segmentId: "segment-a", takeId: "take-a", startSample: boundaries[0], endSample: boundaries[2],
         sourceOffsetSample: "12345678901234567", fadeInEndSample: boundaries[1], fadeOutStartSample: boundaries[1]}
    ]
    property var lastProposal: null
    property var lastPreview: null
    property var lastDecision: null
    property var lastAudition: null
    property var lastCompare: null
    property var lastSelection: null
    property var lastCanceled: null
    property var lastStop: null

    Tk.TakeCompView {
        id: control
        width: 760
        height: 180
        visible: true
        takes: root.lanes
        compSegments: root.segments
        sampleBoundaries: root.boundaries
        selectionStartSample: root.boundaries[1]
        selectionEndSample: root.boundaries[2]
        referenceTakeId: "take-a"
        onCompProposalPreviewRequested: function(id, takeId, start, end, offset) {
            root.lastPreview = [id, takeId, start, end, offset]
        }
        onCompProposalRequested: function(id, segmentId, takeId, start, end, sourceId, offset) {
            root.lastProposal = [id, segmentId, takeId, start, end, sourceId, offset]
        }
        onProposalDecisionRequested: function(id, accept) { root.lastDecision = [id, accept] }
        onAuditionRequested: function(takeId, sourceId, start, end, offset) {
            root.lastAudition = [takeId, sourceId, start, end, offset]
        }
        onCompareRequested: function(takeId, referenceId, start, end, takeOffset, referenceOffset) {
            root.lastCompare = [takeId, referenceId, start, end, takeOffset, referenceOffset]
        }
        onSelectionRequested: function(start, end) { root.lastSelection = [start, end] }
        onCompProposalPreviewCanceled: function(id) { root.lastCanceled = id }
        onAuditionStopRequested: function(takeId) { root.lastStop = takeId }
    }

    function test_exact_range_requests_and_host_decision() {
        verify(control.snapshotValid)
        verify(control.beginSwipe("take-b", 0, 100))
        verify(control.updateSwipe(99, 100))
        verify(lastPreview !== null)
        compare(lastPreview[2], "9007199254740990")
        compare(lastPreview[3], "9007199254741390")
        verify(control.finishSwipe())
        verify(lastProposal !== null)
        compare(lastProposal[1], "")
        compare(lastProposal[2], "take-b")
        compare(lastProposal[3], "9007199254740990")
        compare(lastProposal[4], "9007199254741390")
        compare(lastProposal[5], "source-b")
        compare(lastProposal[6], "98765432109876543")

        control.proposal = {proposalId: "host-proposal-8", status: "pending"}
        verify(control.hasPendingProposal)
        verify(control.decideProposal(true))
        compare(lastDecision[0], "host-proposal-8")
        compare(lastDecision[1], true)
        compare(control.compSegments[0].takeId, "take-a") // UI request never edits host comp state.
        control.proposal = {proposalId: "host-proposal-9", status: "pending"}
        verify(control.decideProposal(false))
        compare(lastDecision[0], "host-proposal-9")
        compare(lastDecision[1], false)

        verify(control.requestAudition("take-b"))
        compare(lastAudition[3], "9007199254741190")
        compare(lastAudition[4], "98765432109876543")
        verify(control.requestCompare("take-b"))
        compare(lastCompare[1], "take-a")
        compare(lastCompare[4], "98765432109876543")
        compare(lastCompare[5], "12345678901234567")
    }

    function test_invalid_exact_snapshot_refuses_swipe() {
        control.sampleBoundaries = ["9007199254740990", "9007199254740990"]
        verify(!control.snapshotValid)
        verify(!control.beginSwipe("take-b", 10, 100))
    }

    function restore() {
        control.takes = root.lanes
        control.compSegments = root.segments
        control.sampleBoundaries = root.boundaries
        control.selectionStartSample = root.boundaries[1]
        control.selectionEndSample = root.boundaries[2]
        control.keyboardTakeIndex = 0
        control.auditionTakeId = ""
        control._cursorCell = -1
        control._anchorCell = -1
    }

    // WP-065: a pass that starts before the comp origin has a negative offset.
    function test_signed_source_offsets_are_valid() {
        restore()
        control.takes = [{takeId: "take-a", sourceId: "source-a", sourceOffsetSample: "-800", available: true},
                         {takeId: "take-b", sourceId: "source-b", sourceOffsetSample: "98765432109876543", available: true}]
        control.compSegments = [{segmentId: "segment-a", takeId: "take-a", startSample: boundaries[0],
                                 endSample: boundaries[2], sourceOffsetSample: "-800"}]
        verify(control.snapshotValid)
        control.takes = [{takeId: "take-a", sourceId: "source-a", sourceOffsetSample: "-", available: true}]
        verify(!control.snapshotValid)
        restore()
    }

    function test_keyboard_selects_exact_cells_and_takes() {
        restore()
        control.forceActiveFocus()
        verify(control.activeFocus)
        lastSelection = null
        keyClick(Qt.Key_Right)
        compare(lastSelection[0], "9007199254740990")
        compare(lastSelection[1], "9007199254741090")
        keyClick(Qt.Key_Right, Qt.ShiftModifier)
        compare(lastSelection[0], "9007199254740990")
        compare(lastSelection[1], "9007199254741190")
        keyClick(Qt.Key_End)
        compare(lastSelection[0], "9007199254741290")
        compare(lastSelection[1], "9007199254741390")
        keyClick(Qt.Key_Home, Qt.ShiftModifier)
        compare(lastSelection[0], "9007199254740990")
        compare(lastSelection[1], "9007199254741390")
        keyClick(Qt.Key_Down)
        compare(control.keyboardTakeId, "take-b")
        keyClick(Qt.Key_Down)
        compare(control.keyboardTakeId, "take-b") // clamped at the last lane
        lastProposal = null
        keyClick(Qt.Key_Return) // uses the host selection (boundaries 1-2)
        compare(lastProposal[2], "take-b")
        compare(lastProposal[3], "9007199254741090")
        compare(lastProposal[4], "9007199254741190")
        lastAudition = null
        keyClick(Qt.Key_Space)
        compare(lastAudition[0], "take-b")
        lastCompare = null
        keyClick(Qt.Key_C)
        compare(lastCompare[0], "take-b")
        compare(lastCompare[1], "take-a")
        keyClick(Qt.Key_Up)
        compare(control.keyboardTakeId, "take-a")
    }

    function test_cancelled_swipe_tells_the_host() {
        restore()
        lastCanceled = null
        verify(control.beginSwipe("take-b", 10, 100))
        verify(control.cancelSwipe())
        verify(lastCanceled !== null)
        verify(String(lastCanceled).length > 0)
        verify(!control.cancelSwipe())
    }

    // WP-065: the host's audition ends from the view (lane button, Space, Escape).
    function test_audition_can_be_stopped() {
        restore()
        lastStop = null
        lastAudition = null
        verify(!control.stopAudition()) // nothing is auditioned
        control.auditionTakeId = "take-b"
        const button = findChild(control, "takeAudition_take-b")
        verify(button !== null)
        compare(button.text, "Stop")
        compare(findChild(control, "takeAudition_take-a").text, "Audition")
        verify(control.requestAudition("take-b")) // the auditioned take's button stops it
        compare(lastStop, "take-b")
        compare(lastAudition, null)
        verify(control.requestAudition("take-a")) // another take starts its own audition
        compare(lastAudition[0], "take-a")
        lastStop = null
        control.forceActiveFocus()
        keyClick(Qt.Key_Escape) // no swipe: Escape ends the audition
        compare(lastStop, "take-b")
        lastStop = null
        verify(control.beginSwipe("take-b", 10, 100))
        keyClick(Qt.Key_Escape) // a swipe in progress is cancelled first
        compare(lastStop, null)
        restore()
    }
}
