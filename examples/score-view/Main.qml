// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// A live notation host (WP-067): Tk.ScoreModel holds a two-staff chamber
// score, lays it out for the view (clefs, key, meter, beams, a tie, a triplet,
// accidentals, dynamics) and takes every ScoreView request; Tk.ScorePlayback
// plays it through the audio engine on the silent built-in device and
// auditions the selection. Undo, redo and MusicXML export are the model's.
// The notes below are demo data.
Rectangle {
    id: root
    width: 1120
    height: 700
    color: Tk.Theme.color.canvas
    property string status: qsTr("Opening the null device…")
    property var ids: ({})
    property int page: 0

    Tk.ScoreModel {
        id: score
        pageWidth: Math.max(72, view.width)
        pageHeight: Math.max(72, view.height)
        staffSpace: Tk.Theme.size.control / 2.4
        margin: Tk.Theme.space.xl * 2
    }
    Tk.AudioEngine {
        id: engine
        onConfigurationApplied: function(requestId, state, active, message) { root.status = message }
    }
    Tk.AudioSession { id: session; engine: engine }
    Tk.ScorePlayback { id: playback; score: score; session: session; autoSync: true }
    Connections {
        target: engine.transport
        function onChanged() {
            const place = engine.transport.playing ? playback.cursor() : ({ ok: false })
            if (place.ok && place.page !== root.page) root.page = place.page // follow playback
            view.playbackCursorX = place.ok ? place.x : -1
        }
    }

    function report(result, done) {
        root.status = result.ok ? done : qsTr("Refused: %1").arg(result.error)
        return result.ok
    }
    function put(part, staff, measure, tick, pitch, value) {
        return score.insertNote(part, staff, score.parts[root.ids[part]].staves[0].voices[0].id,
                                score.measures[measure].id, String(tick), pitch, value).eventId
    }
    function buildScore() {
        const made = score.newScore(qsTr("Serenade in D"), 4, "4/4", 2)
        score.composer = qsTr("QindaTK demo")
        score.setPart(made.partId, { name: qsTr("Violin"), abbreviation: qsTr("Vln.") })
        const cello = score.addPart(qsTr("Cello"), ["bass"], 1).partId
        score.setPart(cello, { abbreviation: qsTr("Vc.") })
        root.ids = { [made.partId]: 0, [cello]: 1 }
        const vln = made.staffId, vc = score.parts[1].staves[0].id
        const v = function(m, tick, pitch, value) { return root.put(made.partId, vln, m, tick, pitch, value) }
        const c = function(m, tick, pitch, value) { return root.put(cello, vc, m, tick, pitch, value) }
        // Violin
        const opening = v(0, 0, "D5", "quarter")
        v(0, 960, "F#5", "eighth"); v(0, 1440, "E5", "eighth"); v(0, 1920, "D5", "eighth"); v(0, 2400, "C#5", "eighth")
        const accented = v(0, 2880, "B4", "quarter")
        v(1, 3840, "A4", "triplet-eighth"); v(1, 4160, "B4", "triplet-eighth"); v(1, 4480, "C#5", "triplet-eighth")
        const detached = v(1, 4800, "D5", "quarter")
        v(1, 5760, "G#4", "quarter")
        const held = v(1, 6720, "A4", "quarter")
        v(2, 7680, "A4", "half")
        const forte = v(2, 9600, "C5", "quarter")
        v(2, 10560, "B4", "quarter")
        const soft = v(3, 11520, "A4", "quarter")
        v(3, 12480, "F#4", "quarter")
        const last = v(3, 13440, "D4", "half")
        score.editEvent(opening, "dynamic", "mf")
        score.editEvent(accented, "articulation", "accent")
        score.editEvent(detached, "articulation", "staccato")
        score.editEvent(held, "tie")
        score.editEvent(forte, "dynamic", "f")
        score.editEvent(soft, "dynamic", "p")
        score.editEvent(last, "articulation", "fermata")
        // Cello
        const bass = c(0, 0, "D3", "half")
        c(0, 1920, "A2", "half")
        c(1, 3840, "F#2", "quarter"); c(1, 4800, "G2", "quarter"); c(1, 5760, "A2", "half")
        c(2, 7680, "D3", "quarter"); c(2, 8640, "F#3", "eighth"); c(2, 9120, "E3", "eighth")
        c(2, 9600, "D3", "quarter"); c(2, 10560, "C#3", "quarter")
        const lastBass = c(3, 11520, "D3", "whole")
        score.editEvent(bass, "dynamic", "mf")
        score.editEvent(lastBass, "articulation", "fermata")
        score.clearHistory()
        view.selectedEventId = accented
        root.follow(accented)
    }
    // The entry cursor follows the selection: Enter replaces it with the entry value.
    function follow(eventId) {
        const data = score.eventData(eventId)
        if (!data.id) return
        view.activePartId = data.partId
        view.activeStaffId = data.staffId
        view.activeVoiceId = data.voiceId
        view.activeMeasureId = data.measureId
        view.entryTick = data.tick
        root.status = data.label
    }
    Component.onCompleted: {
        root.buildScore()
        engine.applyConfiguration("startup", { deviceId: "builtin:null", sampleRate: 48000, bufferSize: 256,
                                               inputLayout: "None", outputLayout: "Stereo" })
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            align: Tk.Flex.Center
            Tk.Label { text: qsTr("Chamber score"); font.pixelSize: Tk.Theme.font.title; Accessible.role: Accessible.Heading }
            Tk.Badge { text: qsTr("ScoreModel · live"); variant: "accent" }
            Tk.Label {
                Tk.Flex.grow: 1
                text: qsTr("Click a beat to enter, select to edit (. dot, T tie, J respell, 1–7 value, Shift+↑↓ step, Delete).")
                muted: true
                elide: Text.ElideRight
            }
        }
        Tk.Flex {
            objectName: "scoreToolbar"
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Segmented {
                objectName: "entryMode"
                small: true
                tooltip: qsTr("Enter notes or rests")
                model: [{ text: qsTr("Note"), value: "note" }, { text: qsTr("Rest"), value: "rest" }]
                onActivated: function(index) { view.entryMode = index === 0 ? "note" : "rest" }
            }
            Tk.Segmented {
                objectName: "entryDuration"
                small: true
                currentIndex: 1
                tooltip: qsTr("Value of the next entry")
                model: [{ text: qsTr("Eighth"), value: "eighth" }, { text: qsTr("Quarter"), value: "quarter" },
                        { text: qsTr("Half"), value: "half" }, { text: qsTr("Triplet"), value: "triplet-eighth" }]
                onActivated: function(index) { view.entryDurationId = ["eighth", "quarter", "half", "triplet-eighth"][index] }
            }
            Tk.ComboBox {
                objectName: "entryPitch"
                small: true
                tooltip: qsTr("Pitch of the next note")
                model: ["D4", "E4", "F#4", "G4", "A4", "B4", "C#5", "D5", "E5", "F#5"]
                currentIndex: 4
                onActivated: function(index) { view.entryPitch = currentText }
            }
            Tk.Button {
                objectName: "enterButton"
                text: qsTr("Enter")
                small: true
                tooltip: qsTr("Enter at the selection (Enter)")
                onClicked: view.requestCursorEntry()
            }
            Tk.ToolSeparator {}
            Tk.Button {
                objectName: "undoButton"
                text: qsTr("Undo")
                small: true
                enabled: score.canUndo
                tooltip: score.canUndo ? qsTr("Undo %1").arg(score.undoText) : qsTr("Nothing to undo")
                onClicked: { score.undo(); root.status = qsTr("Undone") }
            }
            Tk.Button {
                objectName: "redoButton"
                text: qsTr("Redo")
                small: true
                enabled: score.canRedo
                tooltip: score.canRedo ? qsTr("Redo %1").arg(score.redoText) : qsTr("Nothing to redo")
                onClicked: { score.redo(); root.status = qsTr("Redone") }
            }
            Tk.ToolSeparator {}
            Tk.Button {
                objectName: "playButton"
                text: engine.transport.playing ? qsTr("Stop") : qsTr("Play")
                small: true
                variant: "accent"
                enabled: engine.transport.canPlay || engine.transport.playing
                tooltip: engine.transport.canPlay ? qsTr("Play the score through the engine") : engine.transport.playUnavailableReason
                onClicked: {
                    if (engine.transport.playing) { engine.transport.requestStop(); return }
                    engine.transport.requestSeek("0")
                    engine.transport.requestPlay()
                }
            }
            Tk.Button {
                objectName: "auditionButton"
                text: qsTr("Audition")
                small: true
                enabled: view.selectedEventId.length > 0
                tooltip: qsTr("Hold to hear the selected note or chord")
                onPressed: root.status = playback.audition(view.selectedEventId)
                           ? qsTr("Auditioning %1").arg(view.selectedLabel) : playback.lastError
                onReleased: playback.releaseAudition()
                onCanceled: playback.releaseAudition()
            }
            Tk.Button {
                objectName: "previousPage"
                visible: score.pageCount > 1
                text: qsTr("‹")
                small: true
                enabled: root.page > 0
                tooltip: qsTr("Previous page")
                onClicked: root.page -= 1
            }
            Tk.Label {
                visible: score.pageCount > 1
                text: qsTr("Page %1 of %2").arg(Math.min(root.page, score.pageCount - 1) + 1).arg(score.pageCount)
                color: Tk.Theme.color.textMuted
            }
            Tk.Button {
                objectName: "nextPage"
                visible: score.pageCount > 1
                text: qsTr("›")
                small: true
                enabled: root.page + 1 < score.pageCount
                tooltip: qsTr("Next page")
                onClicked: root.page += 1
            }
            Tk.Button {
                objectName: "exportButton"
                text: qsTr("Export MusicXML")
                small: true
                tooltip: qsTr("Write the score as MusicXML 4.0 to the temporary folder")
                onClicked: {
                    const folder = String(StandardPaths.writableLocation(StandardPaths.TempLocation)).replace(/^file:\/\//, "")
                    const file = folder + "/serenade.musicxml"
                    const result = score.exportMusicXml(file)
                    root.report(result, qsTr("Exported %1 events to %2").arg(result.events).arg(file))
                }
            }
        }
        Item {
            objectName: "scorePreviewFrame"
            Tk.Flex.grow: 1
            Tk.ScoreView {
                id: view
                anchors.fill: parent
                scoreId: score.scoreId
                scoreName: score.title
                tooltip: qsTr("Serenade in D, violin and cello")
                layoutSnapshot: score.layoutRevision >= 0 ? score.layoutSnapshot(Math.min(root.page, score.pageCount - 1))
                                                          : ({ available: false })
                entryPitch: "A4"
                entryDurationId: "quarter"
                onEventSelectionRequested: function(requestId, scoreId, eventId) {
                    view.selectedEventId = eventId
                    root.follow(eventId)
                }
                onNavigateRequested: function(scoreId, selectedId, direction) {
                    const next = score.adjacentEvent(selectedId, direction)
                    if (next.length) { view.selectedEventId = next; root.follow(next) }
                }
                onNoteEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, pitch, duration) {
                    const result = score.insertNote(partId, staffId, voiceId, measureId, ticks, pitch, duration)
                    if (root.report(result, qsTr("Entered %1 %2").arg(pitch).arg(duration))) {
                        view.selectedEventId = result.eventId
                        root.follow(result.eventId)
                    }
                }
                onRestEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, duration) {
                    const result = score.insertRest(partId, staffId, voiceId, measureId, ticks, duration)
                    if (root.report(result, qsTr("Entered a %1 rest").arg(duration))) view.selectedEventId = result.eventId
                }
                onNotationEditRequested: function(requestId, scoreId, eventId, edit, value) {
                    if (root.report(score.editEvent(eventId, edit, value), qsTr("Edited: %1").arg(edit))) root.follow(eventId)
                }
                onEventDeleteRequested: function(requestId, scoreId, eventId) {
                    root.report(score.deleteEvents([eventId]), qsTr("Deleted"))
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label {
                objectName: "scoreStatusLine"
                Tk.Flex.grow: 1
                text: root.status
                color: Tk.Theme.color.textMuted
                elide: Text.ElideRight
            }
            Tk.Mono {
                objectName: "playbackReport"
                text: qsTr("%1 MIDI notes · %2 ties merged · engine %3")
                      .arg((playback.report.notes || []).length).arg((playback.report.retained || {}).tiesMerged || 0)
                      .arg(engine.state)
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
