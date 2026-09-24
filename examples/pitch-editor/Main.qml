// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mock analysis/request host. Pitch detection and rendered correction are absent.
Rectangle {
    id: root
    width: 1120
    height: 720
    color: Tk.Theme.color.canvas
    property string status: qsTr("Analysis snapshot loaded · no detector or DSP connected")
    property var pendingRequest: null
    property var ack: ({status:"", message:""})
    property var snapshot: ({
        available: true,
        minimumPitchCents: 6600,
        maximumPitchCents: 7400,
        confidenceThreshold: 0.55,
        formantMinimum: 0.8,
        formantMaximum: 1.25,
        sampleBoundaries: [
            {x:0.08,sample:"90071992547409930000"}, {x:0.19,sample:"90071992547457930000"},
            {x:0.30,sample:"90071992547505930000"}, {x:0.41,sample:"90071992547553930000"},
            {x:0.52,sample:"90071992547601930000"}, {x:0.63,sample:"90071992547649930000"},
            {x:0.74,sample:"90071992547697930000"}, {x:0.85,sample:"90071992547745930000"},
            {x:0.96,sample:"90071992547793930000"}
        ],
        segments: [{
            id:"segment-verse-03", label:"Verse · phrase 3", startSample:"90071992547409930000",
            endSample:"90071992547793930000", formantMinimum:0.82, formantMaximum:1.22,
            notes:[
                {id:"note-a", label:"A4", pitchLabel:"A4", startSample:"90071992547457930000", endSample:"90071992547505930000", x0:0.19, x1:0.30, pitchCents:6900, formantRatio:1.0},
                {id:"note-b", label:"B4", pitchLabel:"B4", startSample:"90071992547505930000", endSample:"90071992547553930000", x0:0.30, x1:0.41, pitchCents:7100, formantRatio:1.03},
                {id:"note-c", label:"C#5", pitchLabel:"C#5", startSample:"90071992547553930000", endSample:"90071992547601930000", x0:0.41, x1:0.52, pitchCents:7300, formantRatio:0.98},
                {id:"note-d", label:"B4", pitchLabel:"B4", startSample:"90071992547601930000", endSample:"90071992547649930000", x0:0.52, x1:0.63, pitchCents:7100, formantRatio:1.0},
                {id:"note-e", label:"A4", pitchLabel:"A4", startSample:"90071992547649930000", endSample:"90071992547697930000", x0:0.63, x1:0.74, pitchCents:6900, formantRatio:1.0}
            ],
            contour:[
                {id:"p0",sample:"90071992547457930000",x:0.19,pitchCents:6880,confidence:0.96,voiced:true},
                {id:"p1",sample:"90071992547481930000",x:0.245,pitchCents:6910,confidence:0.94,voiced:true},
                {id:"p2",sample:"90071992547505930000",x:0.30,pitchCents:6970,confidence:0.92,voiced:true},
                {id:"p3",sample:"90071992547529930000",x:0.355,pitchCents:7050,confidence:0.90,voiced:true},
                {id:"p4",sample:"90071992547553930000",x:0.41,pitchCents:7230,confidence:0.88,voiced:true},
                {id:"p5",sample:"90071992547577930000",x:0.465,pitchCents:7340,confidence:0.33,voiced:true},
                {id:"p6",sample:"90071992547601930000",x:0.52,pitchCents:7160,confidence:0.91,voiced:true},
                {id:"p7",sample:"90071992547625930000",x:0.575,pitchCents:7080,confidence:0.95,voiced:true},
                {id:"p8",sample:"90071992547649930000",x:0.63,pitchCents:6950,confidence:0.93,voiced:true},
                {id:"p9",sample:"90071992547673930000",x:0.685,pitchCents:6870,confidence:0.97,voiced:true},
                {id:"p10",sample:"90071992547697930000",x:0.74,pitchCents:6920,confidence:0.95,voiced:true}
            ]
        }]
    })

    function decide(accepted) {
        if (!root.pendingRequest) return
        const outcome = accepted ? "accepted" : "rejected"
        root.ack = {status:outcome, message:accepted
            ? qsTr("Mock host acknowledged parameters · no audio was rendered")
            : qsTr("Mock host rejected request · source parameters unchanged")}
        root.status = root.ack.message
        root.pendingRequest = null
    }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("Pitch correction"); font.pixelSize: Tk.Theme.font.title; Accessible.role: Accessible.Heading }
            Tk.Badge { text: qsTr("Analysis fixture"); variant: "accent" }
            Tk.Label { text: qsTr("Vocal · take 08 · 48 kHz"); color: Tk.Theme.color.textMuted }
        }
        Tk.Label {
            text: qsTr("Contour and confidence come from the host snapshot. Drag a note to request pitch, time, or formant changes; the mock host can acknowledge or reject without rendering audio.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Segmented {
                model: [
                    {text:qsTr("Pitch"),value:"pitch"},
                    {text:qsTr("Time"),value:"time"},
                    {text:qsTr("Formant"),value:"formant"}
                ]
                currentIndex: 0
                onActivated: function(index) { editor.operation = ["pitch", "time", "formant"][index] }
            }
            Tk.Label { text: qsTr("A4 · 6900 cents"); color: Tk.Theme.color.accent }
            Tk.Label { text: qsTr("Confidence 96%"); color: Tk.Theme.color.textMuted }
            Tk.Label { text: qsTr("Formant 1.00×"); color: Tk.Theme.color.textMuted }
        }
        Item {
            objectName: "pitchEditorFrame"
            Tk.Flex.grow: 1
            Tk.PitchEditor {
                id: editor
                anchors.fill: parent
                sourceId: "source-session-vocal-08"
                channelId: "channel-mono"
                analysisSnapshot: root.snapshot
                snapshotRevision: 4
                selectedSegmentId: "segment-verse-03"
                selectedNoteId: "note-a"
                operation: "pitch"
                editingEnabled: true
                tooltip: qsTr("Host-backed pitch analysis and correction requests")
                editAcknowledgement: root.ack
                onSelectionRequested: function(source, channel, segment, note) {
                    root.status = qsTr("Selection request · %1 / %2").arg(segment).arg(note)
                }
                onEditPreviewRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
                    root.pendingRequest = {gesture:gesture, segment:segment, note:note, operation:operation,
                        anchor:anchor, target:target, cents:cents, ratio:ratio, preview:true}
                    root.status = qsTr("Preview request · %1 · %2 · sample %3 · %4 cents · formant %5×")
                        .arg(operation).arg(note).arg(target).arg(Math.round(cents)).arg(ratio.toFixed(2))
                }
                onEditCommitRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
                    root.pendingRequest = {gesture:gesture, segment:segment, note:note, operation:operation,
                        anchor:anchor, target:target, cents:cents, ratio:ratio, preview:false}
                    root.ack = {status:"pending", message:qsTr("Host request pending · no parameters applied yet")}
                    root.status = root.ack.message
                }
                onGestureCanceled: function(gesture, source, segment, note) {
                    root.pendingRequest = null
                    root.ack = {status:"canceled", message:qsTr("Edit request canceled · host analysis unchanged")}
                    root.status = root.ack.message
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { objectName:"pitchFixtureStatus"; text:root.status; color:Tk.Theme.color.textMuted; elide:Text.ElideRight; Tk.Flex.grow:1 }
            Tk.Button { text:qsTr("Host accepts"); variant:"accent"; enabled:root.pendingRequest !== null; onClicked:root.decide(true) }
            Tk.Button { text:qsTr("Host rejects"); variant:"outline"; enabled:root.pendingRequest !== null; onClicked:root.decide(false) }
        }
        Tk.Caption { text:qsTr("No pitch detection, resynthesis, or corrected playback is connected. Confidence and contour are illustrative fixture data."); color:Tk.Theme.color.textMuted }
    }
}
