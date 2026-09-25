// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-064: the MIDI controls wired to a real AudioSession. Requests become
// model operations (one undo step each), a drag previews before it commits,
// and every audition note the piano roll starts is released.
TestCase {
    id: root
    name: "MidiSession"
    when: windowShown
    visible: true
    width: 800
    height: 500

    Tk.AudioSession { id: session }
    Component {
        id: rollComponent
        Tk.PianoRoll { width: 400; height: 240; lowestPitch: 48; pitchRows: 24 }
    }
    Component {
        id: sequencerComponent
        Tk.StepSequencer { width: 320; height: 160; stepCount: 4 }
    }
    Component {
        id: launcherComponent
        Tk.ClipLauncher { width: 360; height: 160 }
    }

    function rowY(roll, pitch) {
        return (roll.lowestPitch + roll.pitchRows - 1 - pitch + 0.5) / roll.pitchRows * roll.height
    }

    function test_pianoRollEditsTheSessionModel() {
        const pattern = session.midi.addPattern("Roll", "3840").patternId
        const roll = createTemporaryObject(rollComponent, root, {
            patternId: pattern, tickBoundaries: session.midi.tickBoundaries("0", "3840", "240") })
        verify(roll !== null)
        compare(roll.tickBoundaries.length, 17)
        const refresh = () => { roll.notes = session.midi.noteRows(pattern, "0", "3840") }
        session.midi.contentChanged.connect(refresh)
        session.midi.previewChanged.connect(refresh)
        roll.noteInsertRequested.connect((id, p, start, end, pitch, velocity) =>
            session.midi.insertNote(p, { startTicks: start, endTicks: end, pitch: pitch, velocity: velocity }))
        roll.notePreviewRequested.connect((id, p, note, operation, ticks, pitch) =>
            session.midi.previewNoteEdit(id, p, note, operation, ticks, pitch))
        roll.noteEditFinished.connect(id => session.midi.finishEdit(id))
        roll.noteEditCanceled.connect(id => session.midi.cancelEdit(id))
        const auditioned = []
        const released = []
        roll.auditionRequested.connect((pitch, velocity) => auditioned.push(pitch))
        roll.auditionReleased.connect(pitch => released.push(pitch))

        verify(roll.requestInsertAt(2, rowY(roll, 60)))
        compare(roll.notes.length, 1)
        compare(roll.notes[0].startTicks, "0")
        compare(roll.notes[0].endTicks, "240")
        compare(roll.notes[0].pitch, 60)

        verify(roll.beginEditAt(2, rowY(roll, 60)))
        roll.updateEditAt(4.5 / 16 * roll.width, rowY(roll, 62))
        compare(roll.notes[0].startTicks, "960")                          // the preview
        compare(session.midi.patternData(pattern).notes[0].tick, "0")      // nothing committed yet
        verify(roll.finishEdit())
        compare(session.midi.patternData(pattern).notes[0].tick, "960")
        compare(session.midi.patternData(pattern).notes[0].pitch, 62)
        compare(auditioned, [60, 62])
        compare(released, [60, 62])
        compare(session.midi.undoText, "Move note")
        verify(session.midi.undo())
        compare(roll.notes[0].startTicks, "0")

        verify(roll.beginEditAt(2, rowY(roll, 60)))
        verify(roll.cancelEdit())
        compare(released[released.length - 1], 60)
        verify(roll.beginEditAt(2, rowY(roll, 60)))
        session.midi.contentChanged.disconnect(refresh)
        session.midi.previewChanged.disconnect(refresh)
        roll.destroy() // a roll that goes away mid-gesture still releases its note
        wait(0)
        compare(auditioned.length, released.length)
    }

    function test_stepSequencerTogglesAndUndoRestores() {
        const pattern = session.midi.addPattern("Steps", "3840").patternId
        verify(session.midi.addRow(pattern, { pitch: 36, steps: 4, stepTicks: "240" }).ok)
        const sequencer = createTemporaryObject(sequencerComponent, root, {
            y: 260, patternId: pattern, rows: session.midi.stepRows(pattern) })
        verify(sequencer !== null)
        sequencer.stepEditRequested.connect((id, p, row, step, field, value) => {
            session.midi.setStep(p, row, step, field, value)
            sequencer.rows = session.midi.stepRows(p)
        })
        verify(sequencer.toggleAt(sequencer.width - 5, sequencer.height / 2))
        compare(sequencer.rows[0].steps[3].enabled, true)
        verify(session.midi.undo())
        compare(session.midi.stepRows(pattern)[0].steps[3].enabled, false)
    }

    function test_launcherWithoutADeviceRefusesAndExplains() {
        const pattern = session.midi.addPattern("Clip", "3840").patternId
        const track = session.addMidiTrack("Synth")
        const scene = session.midi.addScene("A").sceneId
        const slot = session.midi.setSlot(track, scene, pattern).slotId
        verify(slot.length > 0)
        const launcher = createTemporaryObject(launcherComponent, root, { y: 260, slots: session.launcher.slotList })
        verify(launcher !== null)
        compare(launcher.slots.length, 1)
        compare(launcher.slots[0].state, "ready")
        compare(launcher.slots[0].available, false) // no device runs
        verify(!launcher.requestLaunch(slot))
        let answer = []
        session.launcher.launchAcknowledged.connect((id, state, message) => { answer = [id, state, message] })
        verify(!session.launcher.launch("direct", slot, "bar"))
        compare(answer[0], "direct")
        compare(answer[1], "rejected")
        verify(answer[2].length > 0)
    }
}
