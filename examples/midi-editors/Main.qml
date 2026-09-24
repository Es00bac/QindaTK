// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned fixture snapshots for design and gesture contract review only.
// No MIDI scheduler, instrument, probability engine, or transport runs here.
Item {
    id: page
    width: 1280
    height: 980
    property string status: qsTr("Mock MIDI host ready · no scheduling backend attached")
    property string lastRequestId: ""
    property string lastSlotId: ""
    property string launchOutcome: qsTr("No launch request")

    readonly property var tickBoundaries: [
        "9007199254740993", "9007199254741233", "9007199254741473", "9007199254741713",
        "9007199254741953", "9007199254742193", "9007199254742433", "9007199254742673",
        "9007199254742913", "9007199254743153", "9007199254743393", "9007199254743633",
        "9007199254743873", "9007199254744113", "9007199254744353", "9007199254744593",
        "9007199254744833"
    ]
    readonly property var notes: [
        { id: "note-uuid-01", pitch: 60, velocity: 0.91, x0: 0.04, x1: 0.24,
          startTicks: "9007199254740993", endTicks: "9007199254741473" },
        { id: "note-uuid-02", pitch: 64, velocity: 0.72, x0: 0.18, x1: 0.43,
          startTicks: "9007199254741233", endTicks: "9007199254741713" },
        { id: "note-uuid-03", pitch: 67, velocity: 0.84, x0: 0.34, x1: 0.50,
          startTicks: "9007199254741473", endTicks: "9007199254741953" },
        { id: "note-uuid-04", pitch: 72, velocity: 0.65, x0: 0.55, x1: 0.78,
          startTicks: "9007199254742193", endTicks: "9007199254742673" },
        { id: "note-uuid-05", pitch: 67, velocity: 0.96, x0: 0.70, x1: 0.94,
          startTicks: "9007199254742673", endTicks: "9007199254743153" },
        { id: "note-uuid-06", pitch: 60, velocity: 0.52, x0: 0.82, x1: 0.97,
          startTicks: "9007199254743153", endTicks: "9007199254743393" }
    ]
    readonly property var controllerEvents: [
        { id: "velocity-event-01", ticks: "9007199254740993", value: 0.91, x: 0.04, noteId: "note-uuid-01" },
        { id: "velocity-event-02", ticks: "9007199254741233", value: 0.72, x: 0.18, noteId: "note-uuid-02" },
        { id: "velocity-event-03", ticks: "9007199254741473", value: 0.84, x: 0.34, noteId: "note-uuid-03" },
        { id: "velocity-event-04", ticks: "9007199254742193", value: 0.65, x: 0.55, noteId: "note-uuid-04" },
        { id: "velocity-event-05", ticks: "9007199254742673", value: 0.96, x: 0.70, noteId: "note-uuid-05" },
        { id: "velocity-event-06", ticks: "9007199254743153", value: 0.52, x: 0.82, noteId: "note-uuid-06" }
    ]
    function makeRow(id, name, active) {
        const steps = []
        for (let i = 0; i < 16; ++i) {
            const enabled = active.indexOf(i) >= 0
            steps.push({ id: id + "-step-" + String(i + 1).padStart(2, "0"), enabled: enabled,
                         velocity: enabled ? 0.52 + (i % 5) * 0.1 : 0,
                         probability: enabled && i % 3 === 0 ? 0.75 : 1, gate: enabled ? 0.82 : 0 })
        }
        return { id: id, name: name, steps: steps }
    }
    readonly property var patternRows: [
        makeRow("row-kick", "Kick", [0, 4, 8, 12]),
        makeRow("row-snare", "Snare", [4, 12]),
        makeRow("row-hat", "Closed hat", [2, 6, 10, 14]),
        makeRow("row-bass", "Bass", [0, 3, 8, 11, 14])
    ]
    readonly property var clipSlots: [
        { id: "slot-intro", sceneId: "scene-a", label: "Intro", state: "playing", available: true },
        { id: "slot-verse", sceneId: "scene-a", label: "Verse", state: "ready", available: true },
        { id: "slot-chorus", sceneId: "scene-a", label: "Chorus", state: "queued", available: true },
        { id: "slot-break", sceneId: "scene-a", label: "Break", state: "empty", available: true },
        { id: "slot-intro-b", sceneId: "scene-b", label: "Intro alt", state: "ready", available: true },
        { id: "slot-verse-b", sceneId: "scene-b", label: "Verse alt", state: "ready", available: true },
        { id: "slot-chorus-b", sceneId: "scene-b", label: "Chorus alt", state: "ready", available: true },
        { id: "slot-fill", sceneId: "scene-b", label: "Fill", state: "ready", available: true }
    ]

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        Tk.Scroll {
            anchors.fill: parent
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                padding: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    Tk.Overline { text: qsTr("PATTERN · NIGHT DRIVE") }
                    Tk.Caption { text: qsTr("4/4 · 16 steps · exact tick origin %1").arg(page.tickBoundaries[0]); color: Tk.Theme.color.textMuted }
                    Tk.Spacer { Tk.Flex.flex: 1 }
                    Tk.Caption { objectName: "midiFixtureStatus"; text: page.status; color: Tk.Theme.color.accent }
                }
                Item {
                    width: parent.width
                    implicitHeight: 260
                    Tk.Flex.shrink: 0
                    Tk.PianoRoll {
                        id: piano
                        objectName: "fixturePianoRoll"
                        anchors.fill: parent
                        patternId: "pattern-night-drive"
                        notes: page.notes
                        tickBoundaries: page.tickBoundaries
                        lowestPitch: 48
                        pitchRows: 24
                        tooltip: qsTr("Night Drive piano roll")
                        onNotePreviewRequested: function(requestId, pattern, noteId, operation, ticks, pitch) {
                            page.status = qsTr("Preview %1 · %2 · %3 at tick %4 pitch %5")
                                .arg(requestId).arg(noteId).arg(operation).arg(ticks).arg(pitch)
                        }
                        onNoteEditFinished: function(requestId) { page.status = qsTr("Host edit session finished · %1").arg(requestId) }
                        onNoteEditCanceled: function(requestId) { page.status = qsTr("Host edit session canceled · %1").arg(requestId) }
                        onNoteInsertRequested: function(requestId, pattern, start, end, pitch, velocity) {
                            page.status = qsTr("Insert request · %1–%2 pitch %3").arg(start).arg(end).arg(pitch)
                        }
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    Tk.Overline { text: qsTr("VELOCITY") }
                    Tk.Caption { text: qsTr("7-bit resolution · events remain attached to stable note IDs"); color: Tk.Theme.color.textMuted }
                }
                Item {
                    width: parent.width
                    implicitHeight: 128
                    Tk.Flex.shrink: 0
                    Tk.MidiControllerLane {
                        id: velocity
                        objectName: "fixtureVelocityLane"
                        anchors.fill: parent
                        patternId: "pattern-night-drive"
                        controllerId: "velocity-7bit"
                        controllerLabel: qsTr("Velocity")
                        resolutionBits: 7
                        events: page.controllerEvents
                        tickBoundaries: page.tickBoundaries
                        tooltip: qsTr("Note velocity lane")
                        onValuePreviewRequested: function(id, pattern, controller, eventId, ticks, value) {
                            page.status = qsTr("Controller preview · %1 %2=%3 at %4")
                                .arg(eventId).arg(controller).arg(value.toFixed(3)).arg(ticks)
                        }
                        onEditFinished: function(id) { page.status = qsTr("Controller edit finished · %1").arg(id) }
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    Tk.Overline { text: qsTr("STEP PATTERN") }
                    Tk.Caption { text: qsTr("Per-step gate, velocity and probability snapshots"); color: Tk.Theme.color.textMuted }
                }
                Item {
                    width: parent.width
                    implicitHeight: 218
                    Tk.Flex.shrink: 0
                    Tk.StepSequencer {
                        id: sequencer
                        objectName: "fixtureStepSequencer"
                        anchors.fill: parent
                        patternId: "pattern-night-drive"
                        patternName: qsTr("Night Drive pattern")
                        rows: page.patternRows
                        stepCount: 16
                        tooltip: qsTr("Night Drive step sequencer")
                        onStepEditRequested: function(id, pattern, row, step, field, value) {
                            page.status = qsTr("Step request · %1 / %2 · %3 → %4 (%5)")
                                .arg(row).arg(step).arg(field).arg(value).arg(id)
                        }
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    Tk.Overline { text: qsTr("SCENES · LAUNCH QUANTIZATION: BAR") }
                    Tk.Caption { text: qsTr("Queued and playing status are host snapshots"); color: Tk.Theme.color.textMuted }
                }
                Item {
                    width: parent.width
                    implicitHeight: 148
                    Tk.Flex.shrink: 0
                    Tk.ClipLauncher {
                        id: launcher
                        objectName: "fixtureClipLauncher"
                        anchors.fill: parent
                        slots: page.clipSlots
                        columns: 4
                        quantization: "bar"
                        activeSlotId: "slot-intro"
                        tooltip: qsTr("Clip launcher for Night Drive")
                        onLaunchRequested: function(requestId, slotId, quantize) {
                            page.lastRequestId = requestId
                            page.lastSlotId = slotId
                            page.launchOutcome = qsTr("Pending host acknowledgment · %1").arg(slotId)
                            page.status = qsTr("Launch request %1 · %2 quantized to %3").arg(requestId).arg(slotId).arg(quantize)
                        }
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.xs
                    Tk.Button {
                        objectName: "midiFixtureAcceptLaunch"
                        text: qsTr("Mock host accepts launch")
                        enabled: page.lastRequestId.length > 0
                        onClicked: {
                            if (launcher.acknowledgeLaunch(page.lastRequestId, "accepted",
                                    qsTr("Accepted; waiting for updated playing-state snapshot")))
                                page.launchOutcome = launcher.requestDescription
                        }
                    }
                    Tk.Button {
                        objectName: "midiFixtureRejectLaunch"
                        text: qsTr("Mock host rejects launch")
                        enabled: page.lastRequestId.length > 0
                        onClicked: {
                            if (launcher.acknowledgeLaunch(page.lastRequestId, "rejected", qsTr("Mock engine is offline")))
                                page.launchOutcome = launcher.requestDescription
                        }
                    }
                    Tk.Caption { text: page.launchOutcome; color: Tk.Theme.color.textMuted }
                }
                Tk.Caption {
                    text: qsTr("Mock host fixture only · no MIDI scheduling, quantization clock, probability engine or instrument")
                    color: Tk.Theme.color.textMuted
                }
            }
        }
    }
}
