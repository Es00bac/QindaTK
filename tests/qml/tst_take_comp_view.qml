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
}
