// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A live comp (WP-065, docs/audio-comping.md): a generated three-pass loop
// recording is placed on an AudioSession, which cuts it into one take per
// pass in a real comp. Swipes and "Use range" preview through session.comps,
// Accept/Reject commit or cancel, Audition and Compare change what plays,
// and Undo/Redo walk the comp's own history. Nothing here is a snapshot.
Rectangle {
    id: root
    width: 1080
    height: 620
    color: Tk.Theme.color.canvas

    property string compId: ""
    property string status: qsTr("Preparing the recording…")
    property var proposal: null
    property string selectionStart: "12000"
    property string selectionEnd: "18000"
    readonly property var snapshot: root.compId.length && session.comps.revision >= 0
        ? session.comps.viewState(root.compId, 48, 64) : ({})
    readonly property var takes: root.snapshot.takes || []
    readonly property int rate: root.snapshot.sampleRate || 48000

    function seconds(sample) { return (Number(sample) / root.rate).toFixed(3) }
    function takeLabel(takeId) {
        for (let i = 0; i < root.takes.length; ++i)
            if (root.takes[i].takeId === takeId) return root.takes[i].label
        return qsTr("the comp")
    }
    function segmentsText() {
        const segments = root.snapshot.compSegments || []
        const parts = []
        for (let i = 0; i < segments.length; ++i)
            parts.push(qsTr("%1 %2–%3 s").arg(root.takeLabel(segments[i].takeId))
                       .arg(root.seconds(segments[i].startSample)).arg(root.seconds(segments[i].endSample)))
        return parts.join("  ·  ")
    }
    function preview(takeId, start, end) {
        const result = session.comps.preview(root.compId, takeId, start, end)
        root.status = result.ok ? qsTr("Previewing %1 over %2–%3 s: heard now, not yet in the comp")
                                  .arg(root.takeLabel(takeId)).arg(root.seconds(start)).arg(root.seconds(end))
                                : String(result.error)
        return result.ok
    }

    Tk.AudioSession { id: session }

    Component.onCompleted: {
        const track = session.addTrack(qsTr("Vocal"))
        session.setTrackArmed(track, true)
        // Recorded from 0.917 s over the loop 1.000–1.667 s: three passes,
        // the last stopped early. Each pass sings a different note.
        const path = session.generateTestTake("", { sampleRate: 48000, segments: [
            { frames: 36000, frequency: 220.0, amplitude: 0.45, pulse: 2.5 },
            { frames: 32000, frequency: 246.94, amplitude: 0.6, pulse: 3.5 },
            { frames: 20000, frequency: 261.63, amplitude: 0.7, pulse: 4.5 }] })
        const placed = session.placeTake({ path: path, startSample: "44000", recordedSample: "44000",
            frames: "88000", sampleRate: 48000, channels: 1, latencyFrames: 0, droppedFrames: "0",
            jumps: [{ frame: "36000", position: "48000" }, { frame: "68000", position: "48000" }],
            jumpsDropped: "0" })
        if (!placed.ok) {
            root.status = qsTr("The recording could not be placed: %1").arg(placed.error)
            return
        }
        root.compId = placed.compId
        // One accepted edit in the history, a swipe waiting for a decision,
        // and Take 3 auditioned over the selection (its lane button reads Stop).
        session.comps.select(root.compId, root.takes[0].takeId, "12000", "18000")
        if (root.preview(root.takes[1].takeId, "6000", "10500"))
            root.proposal = { proposalId: "startup", status: "pending" }
        session.comps.setAudition(root.compId, root.takes[2].takeId, root.selectionStart, root.selectionEnd)
    }

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
            text: qsTr("Three loop passes, cut from one recording into takes of a live comp. Swipe a take lane or use a range to preview it, then accept or reject; the takes themselves are never changed.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Button {
                objectName: "compUndo"
                text: qsTr("Undo")
                enabled: session.comps.canUndo
                tooltip: session.comps.undoText.length ? qsTr("Undo %1").arg(session.comps.undoText) : qsTr("Nothing to undo")
                onClicked: { session.comps.undo(); root.proposal = null; root.status = qsTr("Undone") }
            }
            Tk.Button {
                objectName: "compRedo"
                text: qsTr("Redo")
                enabled: session.comps.canRedo
                tooltip: session.comps.redoText.length ? qsTr("Redo %1").arg(session.comps.redoText) : qsTr("Nothing to redo")
                onClicked: { session.comps.redo(); root.proposal = null; root.status = qsTr("Redone") }
            }
            Tk.Button {
                objectName: "compHearComp"
                text: qsTr("Hear the comp")
                enabled: root.snapshot.audition !== undefined && root.snapshot.audition.length > 0
                tooltip: qsTr("Stop auditioning a single take")
                onClicked: session.comps.setAudition(root.compId, "")
            }
            Tk.Caption {
                objectName: "compSummary"
                text: qsTr("Crossfade %1 ms %2 · history %3 · hearing %4")
                      .arg((Number(root.snapshot.fadeSamples || 0) * 1000 / root.rate).toFixed(1))
                      .arg(root.snapshot.fadeShape === "linear" ? qsTr("linear") : qsTr("equal power"))
                      .arg(session.comps.historyCount)
                      .arg(root.snapshot.audition ? qsTr("%1 alone").arg(root.takeLabel(root.snapshot.audition))
                           : root.snapshot.previewing ? qsTr("the preview") : qsTr("the comp"))
            }
        }
        Tk.TakeCompView {
            objectName: "fixtureTakeComp"
            takes: root.takes
            compSegments: root.snapshot.compSegments || []
            sampleBoundaries: root.snapshot.sampleBoundaries || []
            sampleRate: root.rate
            selectionStartSample: root.selectionStart
            selectionEndSample: root.selectionEnd
            referenceTakeId: root.takes.length ? root.takes[0].takeId : ""
            auditionTakeId: root.snapshot.audition || "" // its lane button reads Stop
            proposal: root.proposal
            statusText: root.status
            tooltip: qsTr("Vocal take comp lane")
            onSelectionRequested: function(start, end) { root.selectionStart = start; root.selectionEnd = end }
            onCompProposalPreviewRequested: function(id, takeId, start, end) { root.preview(takeId, start, end) }
            onCompProposalPreviewCanceled: function(id) {
                session.comps.cancelPreview()
                root.status = qsTr("Swipe cancelled; the comp is unchanged")
            }
            onCompProposalRequested: function(id, segmentId, takeId, start, end) {
                if (!root.preview(takeId, start, end)) return // the swipe's last range, or a "Use range" click
                root.proposal = { proposalId: id, status: "pending" }
            }
            onProposalDecisionRequested: function(id, accept) {
                const result = accept ? session.comps.commitPreview() : ({ ok: session.comps.cancelPreview() })
                root.proposal = null
                root.status = accept ? (result.ok ? qsTr("Accepted into the comp") : String(result.error))
                                     : qsTr("Rejected; the comp is unchanged")
            }
            onAuditionRequested: function(takeId, sourceId, start, end) {
                session.comps.setAudition(root.compId, takeId, start, end)
                root.status = qsTr("Auditioning %1 alone over %2–%3 s").arg(root.takeLabel(takeId))
                              .arg(root.seconds(start)).arg(root.seconds(end))
            }
            onAuditionStopRequested: function(takeId) {
                session.comps.setAudition(root.compId, "")
                root.status = qsTr("Stopped auditioning %1; hearing the comp").arg(root.takeLabel(takeId))
            }
            onCompareRequested: function(takeId, referenceId, start, end) {
                session.comps.compare(root.compId, takeId, referenceId, start, end)
                root.status = qsTr("Comparing %1 with %2 over %3–%4 s").arg(root.takeLabel(takeId))
                              .arg(root.takeLabel(referenceId)).arg(root.seconds(start)).arg(root.seconds(end))
            }
        }
        Tk.Mono {
            objectName: "compSegments"
            text: root.segmentsText()
            color: Tk.Theme.color.textMuted
        }
        Tk.Caption {
            objectName: "compSelection"
            text: qsTr("Selection %1–%2 s (samples %3–%4) · missing media: %5")
                  .arg(root.seconds(root.selectionStart)).arg(root.seconds(root.selectionEnd))
                  .arg(root.selectionStart).arg(root.selectionEnd)
                  .arg(session.missingMedia.length ? session.missingMedia.join(", ") : qsTr("none"))
        }
    }
}
