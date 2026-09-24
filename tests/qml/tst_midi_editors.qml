// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "MidiEditors"
    when: windowShown
    visible: true
    width: 800
    height: 700

    readonly property var boundaries: ["9007199254740993", "9007199254741233",
        "9007199254741473", "9007199254741713", "9007199254741953"]
    Component {
        id: pianoComponent
        Tk.PianoRoll {
            width: 400; height: 240
            patternId: "pattern-stable-9"
            tickBoundaries: root.boundaries
            notes: [{ id: "note-stable-4", pitch: 60, velocity: 0.7,
                      x0: 0.1, x1: 0.3, startTicks: "9007199254741233", endTicks: "9007199254741473" }]
            lowestPitch: 48
            pitchRows: 24
        }
    }
    Component {
        id: controllerComponent
        Tk.MidiControllerLane {
            width: 400; height: 140
            patternId: "pattern-stable-9"
            controllerId: "cc-74"
            tickBoundaries: root.boundaries
            events: [{ id: "event-stable-2", ticks: "9007199254741233", value: 0.5, x: 0.25 }]
        }
    }
    Component {
        id: sequencerComponent
        Tk.StepSequencer {
            width: 320; height: 160
            patternId: "pattern-stable-9"
            stepCount: 4
            rows: [{ id: "row-stable-3", name: "Bass", steps: [
                { id: "step-1", enabled: false, velocity: 0, probability: 1, gate: 0.8 },
                { id: "step-2", enabled: true, velocity: 0.7, probability: 0.75, gate: 0.8 },
                { id: "step-3", enabled: false, velocity: 0, probability: 1, gate: 0.8 },
                { id: "step-4", enabled: false, velocity: 0, probability: 1, gate: 0.8 }
            ] }]
        }
    }
    Component {
        id: launcherComponent
        Tk.ClipLauncher {
            width: 360; height: 180
            slots: [{ id: "slot-active", label: "Active", state: "playing", available: true },
                    { id: "slot-stable-7", label: "Queued", state: "queued", available: true }]
            activeSlotId: "slot-active"
            quantization: "bar"
        }
    }

    function test_exactTickEditsPatternIdsAndLaunchAcknowledgment() {
        const piano = createTemporaryObject(pianoComponent, root)
        verify(piano !== null)
        let preview = []
        let finished = ""
        let canceled = ""
        piano.notePreviewRequested.connect(function(id, pattern, note, operation, ticks, pitch) {
            preview = [id, pattern, note, operation, ticks, pitch]
        })
        piano.noteEditFinished.connect(function(id) { finished = id })
        piano.noteEditCanceled.connect(function(id) { canceled = id })
        const noteY = (48 + 24 - 1 - 60 + 0.5) / 24 * piano.height
        verify(piano.beginEditAt(80, noteY))
        piano.updateEditAt(260, noteY)
        compare(preview[1], "pattern-stable-9")
        compare(preview[2], "note-stable-4")
        compare(preview[3], "move")
        compare(preview[4], "9007199254741473")
        compare(preview[5], 60)
        verify(piano.finishEdit())
        compare(finished, preview[0])
        verify(piano.beginEditAt(118, noteY))
        piano.updateEditAt(360, noteY)
        compare(preview[3], "resizeEnd")
        compare(preview[4], "9007199254741713")
        verify(piano.finishEdit())
        verify(piano.beginEditAt(80, noteY))
        const canceledId = piano._gestureId
        verify(piano.cancelEdit())
        compare(canceled, canceledId)

        const controller = createTemporaryObject(controllerComponent, root, { y: 250 })
        verify(controller !== null)
        let controllerRequest = []
        controller.valuePreviewRequested.connect(function(id, pattern, control, eventId, ticks, value) {
            controllerRequest = [id, pattern, control, eventId, ticks, value]
        })
        verify(controller.beginEditAt(100))
        controller.updateEditAt(200, 35)
        compare(controllerRequest[1], "pattern-stable-9")
        compare(controllerRequest[2], "cc-74")
        compare(controllerRequest[3], "event-stable-2")
        compare(controllerRequest[4], "9007199254741473")
        verify(controllerRequest[5] > 0.7)
        verify(controller.finishEdit())

        const sequencer = createTemporaryObject(sequencerComponent, root, { y: 400 })
        verify(sequencer !== null)
        let stepRequest = []
        sequencer.stepEditRequested.connect(function(id, pattern, row, step, field, value) {
            stepRequest = [id, pattern, row, step, field, value]
        })
        verify(sequencer.toggleAt(280, 80))
        compare(stepRequest[1], "pattern-stable-9")
        compare(stepRequest[2], "row-stable-3")
        compare(stepRequest[3], "step-4")
        compare(stepRequest[4], "enabled")
        compare(stepRequest[5], true)
        compare(sequencer.rows[0].steps[3].enabled, false)

        const launcher = createTemporaryObject(launcherComponent, root, { y: 570 })
        verify(launcher !== null)
        let launch = []
        launcher.launchRequested.connect(function(id, slot, quantization) { launch = [id, slot, quantization] })
        let stopRequest = []
        launcher.stopRequested.connect(function(id, slot) { stopRequest = [id, slot] })
        verify(launcher.requestStop("slot-active"))
        compare(stopRequest[1], "slot-active")
        verify(launcher.requestLaunch("slot-stable-7"))
        compare(launch[1], "slot-stable-7")
        compare(launch[2], "bar")
        compare(launcher._requestState, "pending")
        compare(launcher.activeSlotId, "slot-active")
        verify(launcher.acknowledgeLaunch(launch[0], "rejected", "Host declined"))
        compare(launcher._requestState, "rejected")
        compare(launcher.activeSlotId, "slot-active")
    }
}
