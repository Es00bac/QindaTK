// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-049 lane interaction: pointer and keyboard gestures on TempoLane and
// AutomationLane become exact model transactions (one undo step each).
// Geometry: both lanes 800 px wide at 100 px per second from origin 0, so
// at 120 BPM a 4/4 bar is 200 px; the automation plot is inset 4 px.
TestCase {
    id: root
    name: "TempoAutomationLanes"
    when: windowShown
    visible: true
    width: 800
    height: 170

    function ticks(seconds) { return String(Math.round(seconds * 705600000)) }

    Tk.TimeMap { id: timeMap }
    Tk.TimelineModel { id: session; timeMap: timeMap }
    Tk.TimelineViewport { id: view; model: session; viewportWidth: 800; pixelsPerSecond: 100 }
    Tk.TempoMapModel { id: tempo; timeMap: timeMap; timeline: session }
    Tk.AutomationModel { id: automation }
    Tk.TempoLane { id: tempoLane; width: 800; height: 66; viewport: view; tempoModel: tempo }
    Tk.AutomationLane { id: autoLane; y: 72; width: 800; height: 88; viewport: view; model: automation; parameterId: "cutoff" }

    function init() {
        tempo.reset()
        tempoLane.currentEventId = ""
        tempoLane.playheadTicks = "0"
        if (automation.parameterIds.length === 0)
            automation.addParameterMap({ id: "cutoff", name: "Cutoff", dimension: "frequency", unitId: "Hz",
                                         minimum: 20, maximum: 20000, default: 1000 })
        automation.loadContent({ parameters: [{ id: "cutoff", name: "Cutoff", dimension: "frequency", unitId: "Hz",
                                                minimum: 20, maximum: 20000, default: 1000, points: [] }] })
        autoLane.currentPointId = ""
        waitForRendering(tempoLane)
    }
    function tempoRows() { return tempo.events.filter(function(row) { return row.kind === "tempo" }) }

    function test_verticalDragOnSegmentChangesTempo() {
        const y = tempoLane.yForBpm(120)
        mousePress(tempoLane, 300, y)
        mouseMove(tempoLane, 300, y - 5)
        mouseMove(tempoLane, 300, y - 10)
        verify(tempoLane.dragging)
        mouseRelease(tempoLane, 300, y - 10)
        verify(!tempoLane.dragging)
        verify(tempoRows()[0].bpm > 125, "tempo rose to " + tempoRows()[0].bpm)
        compare(tempo.historyCount, 1)
        verify(tempo.undo())
        compare(tempoRows()[0].bpmText, "120.000")
    }

    function test_keyboardAddsEditsMovesAndRemoves() {
        tempoLane.forceActiveFocus()
        tempoLane.playheadTicks = ticks(2) // bar 2 at 120 BPM
        keyClick(Qt.Key_T)
        compare(tempo.eventCount, 3)
        compare(tempoLane.currentEvent.tick, "3840")
        keyClick(Qt.Key_Up)
        compare(tempoLane.currentEvent.bpmText, "121.000")
        keyClick(Qt.Key_Right, Qt.AltModifier)
        compare(tempoLane.currentEvent.tick, "4800")
        keyClick(Qt.Key_Delete)
        compare(tempo.eventCount, 2)
        compare(tempo.historyCount, 4)
    }

    function test_markerDragSnapsToBeatsAndEscapeCancels() {
        verify(tempo.applyEdit({ kind: "addTempo", tick: "3840", bpm: "90" }).ok)
        const row = tempoRows()[1]
        const x = tempoLane.xOf(row)
        const y = tempoLane.yForBpm(row.bpm)
        compare(Math.round(x), 200)
        mousePress(tempoLane, x, y)
        mouseMove(tempoLane, x + 20, y)
        mouseMove(tempoLane, x + 61, y)
        verify(tempoLane.dragging)
        keyClick(Qt.Key_Escape)
        verify(!tempoLane.dragging)
        mouseRelease(tempoLane, x + 61, y)
        compare(tempoRows()[1].tick, "3840")
        mousePress(tempoLane, x, y)
        mouseMove(tempoLane, x + 20, y)
        mouseMove(tempoLane, x + 61, y)
        mouseRelease(tempoLane, x + 61, y)
        const tick = Number(tempoRows()[1].tick)
        verify(tick > 3840 && tick % 960 === 0, "snapped to a beat: " + tick)
    }

    function test_knotDragKeepsExactTimeAndUndoes() {
        verify(automation.applyEdit({ kind: "addPoint", parameterId: "cutoff", ticks: "0", value: 500 }).ok)
        verify(automation.applyEdit({ kind: "addPoint", parameterId: "cutoff", ticks: ticks(2), value: 1000 }).ok)
        waitForRendering(autoLane)
        const y = autoLane.yForNormalized(automation.normalize("cutoff", 1000))
        mousePress(autoLane, 200, y)
        mouseMove(autoLane, 200, y - 10)
        mouseMove(autoLane, 200, y - 20)
        mouseRelease(autoLane, 200, y - 20)
        const point = automation.points("cutoff")[1]
        compare(point.ticks, ticks(2))
        verify(point.value > 1000, "value rose to " + point.value)
        verify(automation.undo())
        compare(automation.points("cutoff")[1].value, 1000)
    }

    function test_doubleClickAddsSegmentDragAndDelete() {
        verify(automation.applyEdit({ kind: "addPoint", parameterId: "cutoff", ticks: "0", value: 2000 }).ok)
        verify(automation.applyEdit({ kind: "addPoint", parameterId: "cutoff", ticks: ticks(4), value: 2000 }).ok)
        waitForRendering(autoLane)
        const y = autoLane.yForNormalized(automation.normalize("cutoff", 2000))
        mousePress(autoLane, 200, y)
        mouseMove(autoLane, 200, y - 10)
        mouseMove(autoLane, 200, y - 30)
        mouseRelease(autoLane, 200, y - 30)
        const rows = automation.points("cutoff")
        verify(rows[0].value > 2000)
        compare(rows[1].value, rows[0].value)
        mouseDoubleClickSequence(autoLane, 600, 20)
        compare(automation.points("cutoff").length, 3)
        verify(autoLane.currentPointId.length > 0)
        autoLane.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        compare(automation.points("cutoff").length, 2)
    }
}
