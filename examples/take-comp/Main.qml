// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned fixture snapshot. Accept/reject, audition and compare are shown
// as requests; this preview does not apply or render a comp.
Rectangle {
    id: root
    width: 1080
    height: 620
    color: Tk.Theme.color.canvas
    property string status: qsTr("Ready · fixture snapshots only")
    property var proposal: null
    property var boundaries: [
        "9007199254740990", "9007199254741990", "9007199254742990",
        "9007199254743990", "9007199254744990", "9007199254745990",
        "9007199254746990"
    ]
    property var takeList: [
        {takeId: "take-amber-04", label: "Amber · pass 04", sourceId: "audio:session-A/track-vocal/take-04",
         sourceOffsetSample: "19200000000", available: true, peaks: [0.10,0.22,0.48,0.92,0.42,0.20,0.70,0.30,0.18,0.38,0.62,0.16,0.24,0.81,0.45,0.20]},
        {takeId: "take-blue-07", label: "Blue · pass 07", sourceId: "audio:session-A/track-vocal/take-07",
         sourceOffsetSample: "28800000000", available: true, peaks: [0.18,0.34,0.72,0.56,0.82,0.31,0.24,0.64,0.94,0.36,0.22,0.52,0.73,0.40,0.18,0.57]},
        {takeId: "take-copper-02", label: "Copper · pass 02", sourceId: "audio:session-A/track-vocal/take-02",
         sourceOffsetSample: "9600000000", available: true, peaks: [0.08,0.25,0.38,0.60,0.32,0.76,0.43,0.22,0.69,0.88,0.28,0.35,0.54,0.30,0.64,0.12]}
    ]
    property var compSnapshot: [
        {segmentId: "comp-intro", takeId: "take-amber-04", startSample: boundaries[0], endSample: boundaries[4],
         sourceOffsetSample: "19200000000", fadeInEndSample: boundaries[1], fadeOutStartSample: boundaries[3]},
        {segmentId: "comp-outro", takeId: "take-blue-07", startSample: boundaries[3], endSample: boundaries[6],
         sourceOffsetSample: "28800000000", fadeInEndSample: boundaries[4], fadeOutStartSample: boundaries[5]}
    ]

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Label {
            text: qsTr("Vocal take comp")
            font.pixelSize: Tk.Theme.font.title
            Accessible.role: Accessible.Heading
        }
        Tk.Label {
            text: qsTr("Host owns source media, exact boundaries and comp state. Swipe a take lane to request a range; accept never applies playback in this fixture.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.TakeCompView {
            objectName: "fixtureTakeComp"
            takes: root.takeList
            compSegments: root.compSnapshot
            sampleBoundaries: root.boundaries
            selectionStartSample: root.boundaries[1]
            selectionEndSample: root.boundaries[3]
            referenceTakeId: "take-amber-04"
            proposal: root.proposal
            statusText: root.status
            tooltip: qsTr("Vocal take comp lane")
            onCompProposalPreviewRequested: function(id, takeId, startSample, endSample, sourceOffset) {
                root.status = qsTr("Swipe preview · %1 · %2–%3 · source offset %4")
                    .arg(takeId).arg(startSample).arg(endSample).arg(sourceOffset)
            }
            onCompProposalRequested: function(id, segmentId, takeId, startSample, endSample, sourceId, sourceOffset) {
                root.proposal = {proposalId: id, segmentId: segmentId, takeId: takeId,
                    startSample: startSample, endSample: endSample, sourceId: sourceId,
                    sourceOffsetSample: sourceOffset, status: "pending"}
                root.status = qsTr("Host proposal pending · %1 · %2–%3")
                    .arg(takeId).arg(startSample).arg(endSample)
            }
            onProposalDecisionRequested: function(id, accept) {
                if (!root.proposal || root.proposal.proposalId !== id) return
                root.proposal = Object.assign({}, root.proposal, {status: "decision-sent"})
                root.status = accept
                    ? qsTr("Host received accept request · comp snapshot remains unchanged")
                    : qsTr("Host received reject request · comp snapshot remains unchanged")
            }
            onAuditionRequested: function(takeId, sourceId, startSample, endSample, offset) {
                root.status = qsTr("Host audition request · %1 · %2:%3–%4 · offset %5")
                    .arg(takeId).arg(sourceId).arg(startSample).arg(endSample).arg(offset)
            }
            onCompareRequested: function(takeId, referenceId, startSample, endSample, takeOffset, referenceOffset) {
                root.status = qsTr("Host compare request · %1 vs %2 · offsets %3 / %4")
                    .arg(takeId).arg(referenceId).arg(takeOffset).arg(referenceOffset)
            }
        }
        Tk.Label { objectName: "fixtureStatus"; text: root.status; color: Tk.Theme.color.textMuted }
        Tk.Caption {
            text: root.proposal ? qsTr("Proposal %1 · %2 · segment %3").arg(root.proposal.proposalId)
                    .arg(root.proposal.status).arg(root.proposal.segmentId || qsTr("new range"))
                                : qsTr("Current comp includes a visible overlapping fade region. Source takes remain unchanged.")
            color: Tk.Theme.color.textMuted
        }
    }
}
