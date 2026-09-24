// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Illustrative host snapshot only; no notation engraver, score model, or playback backend.
Rectangle {
    id: root
    width: 1120
    height: 700
    color: Tk.Theme.color.canvas
    property string status: qsTr("Fixture ready · host owns score state")
    property var snapshot: ({
        available: true,
        systems: [{ id: "system-1", staves: [
            { id: "staff-violin", partId: "part-strings", label: "Violin", clef: "G", top: 0.18, lineGap: 0.018, x0: 0.08, x1: 0.94,
              measures: [
                { id: "m-001", number: "1", keySignature: "D", timeSignature: "4/4", x0: 0.08, x1: 0.51,
                  entryPositions: [{x:0.20,ticks:"0"},{x:0.29,ticks:"960"},{x:0.38,ticks:"1920"},{x:0.47,ticks:"2880"}],
                  voices: [{id:"voice-vln-1", events:[
                    {id:"n-vln-1",kind:"note",x:0.20,staffStep:4,accidental:"",filledHead:true,stemDirection:"up",dotCount:0},
                    {id:"n-vln-2",kind:"note",x:0.29,staffStep:5,accidental:"♯",filledHead:true,stemDirection:"down",dotCount:1},
                    {id:"r-vln-1",kind:"rest",x:0.38,staffStep:4,dotCount:0},
                    {id:"n-vln-3",kind:"note",x:0.47,staffStep:3,accidental:"",filledHead:false,stemDirection:"up",dotCount:0}
                  ]}]},
                { id: "m-002", number: "2", keySignature: "", timeSignature: "", x0: 0.51, x1: 0.94,
                  entryPositions: [{x:0.62,ticks:"3840"},{x:0.72,ticks:"4800"},{x:0.82,ticks:"5760"},{x:0.92,ticks:"6720"}],
                  voices: [{id:"voice-vln-1", events:[
                    {id:"n-vln-4",kind:"note",x:0.62,staffStep:2,accidental:"",filledHead:true,stemDirection:"up",dotCount:0},
                    {id:"n-vln-5",kind:"note",x:0.72,staffStep:1,accidental:"",filledHead:true,stemDirection:"up",dotCount:0},
                    {id:"n-vln-6",kind:"note",x:0.82,staffStep:3,accidental:"♮",filledHead:true,stemDirection:"down",dotCount:0},
                    {id:"r-vln-2",kind:"rest",x:0.92,staffStep:4,dotCount:1}
                  ]}]}
              ]},
            { id: "staff-cello", partId: "part-cello", label: "Cello", clef: "F", top: 0.53, lineGap: 0.018, x0: 0.08, x1: 0.94,
              measures: [
                { id: "m-001", number: "1", keySignature: "D", timeSignature: "4/4", x0: 0.08, x1: 0.51,
                  entryPositions: [{x:0.20,ticks:"0"},{x:0.29,ticks:"960"},{x:0.38,ticks:"1920"},{x:0.47,ticks:"2880"}],
                  voices: [{id:"voice-cello-1", events:[
                    {id:"n-cello-1",kind:"note",x:0.20,staffStep:2,accidental:"",filledHead:false,stemDirection:"up",dotCount:0},
                    {id:"n-cello-2",kind:"note",x:0.38,staffStep:0,accidental:"",filledHead:true,stemDirection:"up",dotCount:0}
                  ]}]},
                { id: "m-002", number: "2", keySignature: "", timeSignature: "", x0: 0.51, x1: 0.94,
                  entryPositions: [{x:0.62,ticks:"3840"},{x:0.72,ticks:"4800"},{x:0.82,ticks:"5760"},{x:0.92,ticks:"6720"}],
                  voices: [{id:"voice-cello-1", events:[
                    {id:"n-cello-3",kind:"note",x:0.62,staffStep:1,accidental:"",filledHead:true,stemDirection:"up",dotCount:0},
                    {id:"n-cello-4",kind:"note",x:0.82,staffStep:3,accidental:"",filledHead:true,stemDirection:"up",dotCount:0}
                  ]}]}
              ]}
        ]}]
    })

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("Chamber score"); font.pixelSize: Tk.Theme.font.title; Accessible.role: Accessible.Heading }
            Tk.Badge { text: qsTr("Snapshot fixture"); variant: "accent" }
        }
        Tk.Label {
            text: qsTr("Host-supplied engraving coordinates and exact entry positions. This fixture does not calculate rhythm, persist notation, or play audio.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("Part: Strings"); color: Tk.Theme.color.textMuted }
            Tk.Label { text: qsTr("Voice: Violin 1"); color: Tk.Theme.color.textMuted }
            Tk.Label { text: qsTr("Measure: 1"); color: Tk.Theme.color.textMuted }
            Tk.Label { text: qsTr("Entry: quarter · D5"); color: Tk.Theme.color.accent }
        }
        Item {
            objectName: "scorePreviewFrame"
            Tk.Flex.grow: 1
            Tk.ScoreView {
                id: score
                anchors.fill: parent
                scoreId: "score-chamber-demo"
                scoreName: "Chamber score"
                tooltip: qsTr("Host-backed notation view")
                layoutSnapshot: root.snapshot
                snapshotRevision: 1
                selectedEventId: "n-vln-2"
                activePartId: "part-strings"
                activeStaffId: "staff-violin"
                activeVoiceId: "voice-vln-1"
                activeMeasureId: "m-001"
                entryTick: "960"
                entryPitch: "D5"
                entryDurationId: "quarter"
                onEventSelectionRequested: function(requestId, scoreId, eventId, partId, staffId, voiceId, measureId) {
                    root.status = qsTr("Selection request · %1 · %2 / %3 / %4").arg(requestId).arg(eventId).arg(voiceId).arg(measureId)
                }
                onNoteEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, pitch, duration) {
                    root.status = qsTr("Note request · %1 · %2 at tick %3 · %4").arg(requestId).arg(pitch).arg(ticks).arg(duration)
                }
                onRestEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, duration) {
                    root.status = qsTr("Rest request · %1 at tick %2 · %3").arg(requestId).arg(ticks).arg(duration)
                }
                onNavigateRequested: function(scoreId, selectedId, direction) {
                    root.status = qsTr("Navigation request · %1 from %2").arg(direction).arg(selectedId)
                }
                onPlaybackLocateRequested: function(measureId, ticks) {
                    root.status = qsTr("Host locate request · measure %1 · tick %2; no playback here").arg(measureId).arg(ticks)
                }
            }
        }
        Tk.Label { objectName: "fixtureStatus"; text: root.status; color: Tk.Theme.color.textMuted; elide: Text.ElideRight }
        Tk.Caption { text: qsTr("Unsupported engraving features must be reported by the host snapshot; no rhythmic layout is fabricated."); color: Tk.Theme.color.textMuted }
    }
}
