// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-049: tempo, meter and automation editing on the shared timeline.
// The "Drums" clips are beat-locked (music icon) and follow the tempo map;
// the "Dialogue" clips are time-locked and keep their absolute time. The
// startup tempo change at bar 3 (120 -> 90 BPM) is a real, undoable edit:
// drag the tempo line, press Undo tempo, and watch only the drums move.
// The mock engine reads immutable EngineSchedule snapshots; its sample clock
// (advanced here by a timer standing in for audio callbacks) stamps the
// cutoff slider's touch/move/release gestures for the record path.
Item {
    id: page
    width: 1280
    height: 600

    property string playhead: "0"
    property string status: ""
    property string parameterId: "synth/cutoff"
    readonly property real second: 705600000

    function ticks(seconds) { return String(Math.round(seconds * page.second)) }
    function uuid(prefix, n) { return prefix + "-0000-4000-8000-" + String(n).padStart(12, "0") }
    function build() {
        const tracks = [{ id: page.uuid("10000000", 1), name: "Drums (beat-locked)", kind: "audio", color: "#22c55e" },
                        { id: page.uuid("10000000", 2), name: "Dialogue (time-locked)", kind: "audio", color: "#3b82f6" }]
        const clips = []
        for (let i = 0; i < 4; ++i) {
            // At 120 BPM a 4/4 bar is 2 s: bars 1, 3, 5 and 7 start at 0, 4, 8 and 12 s.
            clips.push({ id: page.uuid("20000000", i), trackId: tracks[0].id, start: page.ticks(i * 4), duration: page.ticks(2),
                         resourceId: "demo:drums", kind: "audio", label: "Groove " + (i + 1), anchor: "musical" })
            clips.push({ id: page.uuid("21000000", i), trackId: tracks[1].id, start: page.ticks(i * 4), duration: page.ticks(2),
                         resourceId: "demo:voice", kind: "audio", label: "Line " + (i + 1), anchor: "time" })
        }
        session.loadContent({ tracks: tracks, clips: clips,
                              markers: [{ id: page.uuid("30000000", 1), position: page.ticks(8), anchor: "musical", label: "Bar 5" }] })
        tempo.applyEdit({ kind: "addTempo", tick: String(2 * 3840), bpm: "90" })
        automation.addParameterMap({ id: "synth/cutoff", name: "Cutoff", dimension: "frequency", unitId: "Hz",
                                     minimum: 20, maximum: 20000, default: 1000, mode: "touch" })
        automation.addParameterMap({ id: "synth/gain", name: "Gain", dimension: "gain", unitId: "dB",
                                     minimum: -60, maximum: 6, default: 0, step: 0.5, interpolation: "step" })
        const points = [[0, 400, "linear"], [4, 6000, "cubic"], [8, 1500, "linear"], [11, 9000, "step"], [14, 2500, "linear"]]
        for (const p of points)
            automation.applyEdit({ kind: "addPoint", parameterId: "synth/cutoff", ticks: page.ticks(p[0]), value: p[1],
                                   interpolation: p[2] })
        for (const p of [[0, -6], [6, -18], [10, 0]])
            automation.applyEdit({ kind: "addPoint", parameterId: "synth/gain", ticks: page.ticks(p[0]), value: p[1] })
        // Shape the 4 s -> 8 s cubic segment with explicit handles (an ease).
        const rows = automation.points("synth/cutoff")
        automation.applyEdit({ kind: "setHandle", parameterId: "synth/cutoff", pointId: rows[1].id, side: "out" },
                             { handle: { x: 0.55, y: rows[1].normalizedValue } })
        automation.applyEdit({ kind: "setHandle", parameterId: "synth/cutoff", pointId: rows[2].id, side: "in" },
                             { handle: { x: 0.6, y: rows[2].normalizedValue } })
        automation.clearHistory()
        engine.process() // evaluate the first block at 0 without playing
    }

    Tk.TimeMap { id: timeMap }
    Tk.TimelineModel { id: session; timeMap: timeMap }
    Tk.TempoMapModel { id: tempo; timeMap: timeMap; timeline: session; onRejected: function(reason) { page.status = reason } }
    Tk.AutomationModel { id: automation; onRejected: function(reason) { page.status = reason } }
    Tk.EngineSchedule { id: schedule; tempoModel: tempo; automationModel: automation }
    Tk.MockAutomationEngine { id: engine; schedule: schedule; blockFrames: 2400 }
    Timer {
        interval: 50
        repeat: true
        running: engine.playing
        onTriggered: {
            engine.process()
            page.playhead = engine.lastBlock.ticks
        }
    }
    Component.onCompleted: page.build()

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        Tk.Flex {
            direction: Tk.Flex.Column

            Tk.ToolBar {
                objectName: "tempoAutomationToolbar"
                width: parent.width
                compact: true
                Tk.IconButton {
                    objectName: "tempoUndo"
                    iconName: "undo-2"
                    enabled: tempo.canUndo
                    tooltip: tempo.canUndo ? qsTr("Undo tempo: %1").arg(tempo.undoText) : qsTr("No tempo edit to undo")
                    onClicked: tempo.undo()
                }
                Tk.IconButton {
                    objectName: "tempoRedo"
                    iconName: "redo-2"
                    enabled: tempo.canRedo
                    tooltip: tempo.canRedo ? qsTr("Redo tempo: %1").arg(tempo.redoText) : qsTr("No tempo edit to redo")
                    onClicked: tempo.redo()
                }
                Tk.ToolSeparator { }
                Tk.IconButton {
                    objectName: "automationUndo"
                    iconName: "undo-2"
                    enabled: automation.canUndo
                    tooltip: automation.canUndo ? qsTr("Undo automation: %1").arg(automation.undoText) : qsTr("No automation edit to undo")
                    onClicked: automation.undo()
                }
                Tk.IconButton {
                    objectName: "automationRedo"
                    iconName: "redo-2"
                    enabled: automation.canRedo
                    tooltip: automation.canRedo ? qsTr("Redo automation: %1").arg(automation.redoText) : qsTr("No automation edit to redo")
                    onClicked: automation.redo()
                }
                Tk.ToolSeparator { }
                Tk.Segmented {
                    id: modeSelector
                    objectName: "automationModeSelector"
                    small: true
                    tooltip: qsTr("Automation mode of the shown parameter")
                    model: ["off", "read", "touch", "latch", "write"].map(function(mode) {
                        const capability = automation.capabilities[mode] || { available: true, reason: "" }
                        return { text: mode.charAt(0).toUpperCase() + mode.slice(1), value: mode, enabled: capability.available,
                                 tooltip: capability.available ? qsTr("%1 mode").arg(mode) : capability.reason }
                    })
                    function modeIndex() {
                        return ["off", "read", "touch", "latch", "write"].indexOf(automationLane.writeState.mode || "read")
                    }
                    currentIndex: modeIndex()
                    onActivated: function(index) {
                        const result = automation.setModeName(page.parameterId, currentValue)
                        if (!result.ok) page.status = result.error
                        // The model decides the mode; show what it accepted.
                        currentIndex = Qt.binding(modeIndex)
                    }
                }
                Tk.CheckBox {
                    objectName: "latchCapability"
                    text: qsTr("Engine supports latch")
                    small: true
                    checked: true
                    tooltip: qsTr("Declare latch unsupported to see an explicit capability refusal")
                    onToggled: automation.setModeAvailability("latch", checked, qsTr("The mock engine was told it cannot latch"))
                }
                Tk.ToolSeparator { }
                Tk.IconButton {
                    objectName: "engineLocate"
                    iconName: "skip-back"
                    tooltip: qsTr("Locate the mock engine to the start")
                    onClicked: { engine.locate("0"); page.playhead = "0" }
                }
                Tk.IconButton {
                    objectName: "enginePlay"
                    iconName: engine.playing ? "square" : "play"
                    tooltip: engine.playing ? qsTr("Stop the mock engine") : qsTr("Play the mock engine")
                    onClicked: engine.playing ? engine.stop() : engine.play()
                }
                Tk.Slider {
                    objectName: "cutoffControl"
                    width: Tk.Theme.size.fieldWidth
                    small: true
                    from: 20
                    to: 20000
                    value: 1000
                    tooltip: qsTr("Cutoff control: touch, move and release are recorded at engine time")
                    onPressedChanged: pressed ? engine.touch("synth/cutoff", value) : engine.release("synth/cutoff")
                    onMoved: engine.move("synth/cutoff", value)
                }
                Tk.Spacer { }
                Tk.Caption {
                    objectName: "engineReadout"
                    text: {
                        const block = engine.lastBlock
                        if (!block.ok) return qsTr("Engine stopped")
                        return qsTr("Engine %1 s · %2 · %3 BPM · cutoff %4")
                            .arg((Number(block.sample) / 48000).toFixed(2)).arg(tempo.formatPosition(block.musicalTick))
                            .arg(block.bpm.toFixed(3)).arg(automation.formatValue("synth/cutoff", block.values["synth/cutoff"]))
                    }
                }
            }

            Tk.Grid {
                objectName: "tempoAutomationGrid"
                width: parent.width
                Tk.Flex.grow: 1
                columns: timeline.headerWidth + "px 1fr " + Tk.Theme.size.scrollbar + "px"
                rows: Tk.Theme.size.row * 3 + "px 1fr " + Tk.Theme.size.row * 4 + "px"

                Tk.Flex {
                    Tk.Grid.row: 0
                    Tk.Grid.column: 0
                    direction: Tk.Flex.Column
                    padding: Tk.Theme.space.sm
                    Tk.Label { text: qsTr("Tempo · Meter") }
                    Tk.Caption { text: qsTr("%1 events").arg(tempo.eventCount); color: Tk.Theme.color.textMuted }
                }
                Tk.TempoLane {
                    id: tempoLane
                    Tk.Grid.row: 0
                    Tk.Grid.column: 1
                    viewport: timeline.viewport
                    tempoModel: tempo
                    playheadTicks: page.playhead
                    onEditRejected: function(reason) { page.status = reason }
                }
                Tk.EditTimeline {
                    id: timeline
                    objectName: "tempoAutomationTimeline"
                    Tk.Grid.row: 1
                    Tk.Grid.column: 0
                    Tk.Grid.columnSpan: 3
                    model: session
                    playheadTicks: page.playhead
                    tooltip: qsTr("Session timeline")
                    onSeekRequested: function(ticks) { page.playhead = ticks }
                    Component.onCompleted: timeline.viewport.pixelsPerSecond = 58
                }
                Tk.Flex {
                    Tk.Grid.row: 2
                    Tk.Grid.column: 0
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    padding: Tk.Theme.space.sm
                    Tk.Label { text: qsTr("Automation") }
                    Tk.Segmented {
                        objectName: "automationParameter"
                        small: true
                        model: [{ text: qsTr("Cutoff"), value: "synth/cutoff" }, { text: qsTr("Gain"), value: "synth/gain" }]
                        tooltip: qsTr("Parameter shown in the lane")
                        onActivated: page.parameterId = currentValue
                    }
                }
                Tk.AutomationLane {
                    id: automationLane
                    Tk.Grid.row: 2
                    Tk.Grid.column: 1
                    viewport: timeline.viewport
                    model: automation
                    parameterId: page.parameterId
                    playheadTicks: page.playhead
                    onEditRejected: function(reason) { page.status = reason }
                }
            }

            Tk.StatusBar {
                width: parent.width
                Tk.StatusField {
                    objectName: "tempoAutomationStatus"
                    text: page.status.length > 0 ? page.status
                          : qsTr("Beat-locked clips follow tempo edits; time-locked clips stay. Drag, double-click or use the keyboard in either lane.")
                }
                Tk.Spacer { }
                Tk.StatusField {
                    objectName: "tempoAutomationRevisions"
                    text: qsTr("tempo r%1 · automation r%2 · snapshot g%3").arg(tempo.revision).arg(automation.revision).arg(schedule.generation)
                }
            }
        }
    }
}
