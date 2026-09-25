// SPDX-License-Identifier: LGPL-3.0-or-later
import QtCore
import QtQuick
import QindaTK as Tk

// PitchEditor on the real pitch host: a generated vocal-like phrase is
// analysed by AudioPitchSource (YIN) and every drag becomes a previewed,
// undoable TD-PSOLA "pitch" edit in the session (docs/audio-pitch.md).
Rectangle {
    id: root
    width: 1120
    height: 720
    color: Tk.Theme.color.canvas
    property string resourceId: ""
    property string status: qsTr("Generating the reference phrase")
    property var ack: ({ status: "", message: "" })

    function report(result, verb) {
        if (!result || result.unchanged) {
            root.ack = { status: "", message: qsTr("No change") }
        } else if (result.ok) {
            root.ack = { status: verb, message: verb === "accepted"
                ? qsTr("Committed · %1").arg(session.edits.undoText)
                : qsTr("Previewing on a worker thread") }
        } else {
            root.ack = { status: "rejected", message: String(result.error || qsTr("Request refused")) }
        }
        root.status = root.ack.message
    }

    Tk.AudioSession { id: session }
    Tk.AudioPitchSource {
        id: pitch
        session: session
        resourceId: root.resourceId
        columnCount: Math.max(1, Math.round(editor.width))
        pitchSnapCents: 0
    }
    Connections {
        target: pitch
        // Until a gesture reports something, the status line follows the analysis.
        function onStateChanged() {
            if (root.ack.message.length) return
            if (pitch.state === "ready") root.status = qsTr("Analysis ready · %1 notes").arg(pitch.noteCount)
            else if (pitch.state === "failed") root.status = pitch.error
        }
    }
    Connections {
        target: session.edits
        function onPreviewReady(requestId, summary) { root.status = qsTr("Preview ready · %1").arg(summary.label) }
        function onPreviewFailed(requestId, error) { root.ack = { status: "rejected", message: error }; root.status = error }
    }
    Component.onCompleted: {
        const folder = StandardPaths.writableLocation(StandardPaths.TempLocation)
        // A4 slightly flat with vibrato, B4 flat, C#5 sharp, a breath, then A4.
        const written = pitch.writeTestSignal(folder + "/qindatk-pitch-example.wav", [
            { pitchCents: 6885, seconds: 0.6, vibratoCents: 20 },
            { pitchCents: 7082, seconds: 0.45 },
            { pitchCents: 7318, seconds: 0.45 },
            { seconds: 0.15 },
            { pitchCents: 6906, seconds: 0.7, vibratoCents: 25 }
        ])
        if (!written.ok) { root.status = written.error; return }
        const track = session.addTrack(qsTr("Vocal"))
        root.resourceId = session.importMedia(written.path, track, "0").resourceId || ""
        root.status = root.resourceId.length ? qsTr("Analysing") : session.lastError
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
            Tk.Badge { text: pitch.detector + " · " + pitch.corrector; variant: "accent" }
            Tk.Label {
                objectName: "pitchSourceState"
                text: qsTr("Reference phrase · 48 kHz · analysis %1 · %2 notes").arg(pitch.state).arg(pitch.noteCount)
                color: Tk.Theme.color.textMuted
            }
        }
        Tk.Label {
            text: qsTr("Drag a note up or down to move its pitch, or switch to Time or Formant. Each drag previews on a worker thread; releasing commits one undo step that playback and offline render use.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Segmented {
                model: [
                    { text: qsTr("Pitch"), value: "pitch" },
                    { text: qsTr("Time"), value: "time" },
                    { text: qsTr("Formant"), value: "formant" }
                ]
                currentIndex: 0
                onActivated: function(index) { editor.operation = ["pitch", "time", "formant"][index] }
            }
            Tk.Segmented {
                model: [
                    { text: qsTr("Free"), value: 0 },
                    { text: qsTr("Semitones"), value: 100 }
                ]
                currentIndex: 0
                onActivated: function(index) { pitch.pitchSnapCents = [0, 100][index] }
            }
            Tk.Segmented {
                model: [
                    { text: qsTr("Keep vibrato"), value: 0 },
                    { text: qsTr("Flatten"), value: 1 }
                ]
                currentIndex: 0
                onActivated: function(index) { pitch.correctionAmount = index }
            }
            Tk.Button { text: qsTr("Undo"); variant: "outline"; enabled: session.edits.canUndo; onClicked: session.edits.undo() }
            Tk.Button { text: qsTr("Redo"); variant: "outline"; enabled: session.edits.canRedo; onClicked: session.edits.redo() }
        }
        Item {
            objectName: "pitchEditorFrame"
            Tk.Flex.grow: 1
            Tk.PitchEditor {
                id: editor
                anchors.fill: parent
                sourceId: root.resourceId
                channelId: "all"
                analysisSnapshot: pitch.snapshot.available ? pitch.snapshot
                    : ({ available: false, segments: [], sampleBoundaries: [],
                         error: pitch.state === "failed" ? pitch.error : qsTr("Analysing pitch") })
                operation: "pitch"
                editingEnabled: pitch.current
                tooltip: qsTr("Drag a note to correct it; Escape cancels")
                editAcknowledgement: root.ack
                onSelectionRequested: function(source, channel, segment, note) {
                    editor.selectedSegmentId = segment
                    editor.selectedNoteId = note
                }
                onEditPreviewRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
                    root.report(pitch.previewEdit(gesture, segment, note, operation, anchor, target, cents, ratio), "previewing")
                }
                onEditCommitRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
                    root.report(pitch.commitEdit(gesture, segment, note, operation, anchor, target, cents, ratio), "accepted")
                }
                onGestureCanceled: function(gesture) {
                    pitch.cancelEdit(gesture)
                    root.ack = { status: "canceled", message: qsTr("Edit canceled · committed samples restored") }
                    root.status = root.ack.message
                }
                onZoomRequested: function(anchor, factor) { pitch.zoom(anchor, factor) }
                onScrollRequested: function(fraction) { pitch.scroll(fraction) }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { objectName: "pitchFixtureStatus"; text: root.status; color: Tk.Theme.color.textMuted; elide: Text.ElideRight; Tk.Flex.grow: 1 }
            Tk.Label { text: session.edits.undoText.length ? qsTr("Last edit: %1").arg(session.edits.undoText) : ""; color: Tk.Theme.color.textMuted }
        }
        Tk.Caption {
            text: qsTr("Limits: ±700 cents, formant 0.80–1.25×, retiming 0.67–1.5× per side. Pure sine tones barely shift: the corrector keeps the tone colour.")
            color: Tk.Theme.color.textMuted
        }
    }
}
