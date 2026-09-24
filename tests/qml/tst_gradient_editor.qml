// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "GradientEditor"
    when: windowShown
    visible: true
    width: 360
    height: 120

    SignalSpy { id: selectionSpy }
    SignalSpy { id: finishSpy }
    SignalSpy { id: previewSpy }
    SignalSpy { id: cancelSpy }
    SignalSpy { id: addSpy }
    SignalSpy { id: deleteSpy }
    SignalSpy { id: moveFinishSpy }

    Component {
        id: editorComponent
        Tk.GradientEditor {
            width: 320
            stops: [
                { id: "left", position: 0, color: "#ff0000" },
                { id: "right", position: 1, color: "#0000ff" }
            ]
        }
    }

    function make() {
        const item = createTemporaryObject(editorComponent, this)
        verify(item !== null)
        waitForRendering(item)
        return item
    }

    function test_clickSelectsWithoutFinishingMove() {
        const item = make()
        selectionSpy.target = item
        selectionSpy.signalName = "selectionRequested"
        selectionSpy.clear()
        finishSpy.target = item
        finishSpy.signalName = "moveFinished"
        finishSpy.clear()
        const x = item.trackInset + item.width - 2 * item.trackInset
        mouseClick(item, x, item.handleY)
        compare(selectionSpy.count, 1)
        compare(selectionSpy.signalArguments[0][0], "right")
        compare(finishSpy.count, 0)
        verify(findChild(item, "gradientTrack") !== null)
    }

    function test_dragSurvivesHostReplacementAndEscapeCancels() {
        const item = make()
        previewSpy.target = item
        previewSpy.signalName = "movePreviewRequested"
        previewSpy.clear()
        cancelSpy.target = item
        cancelSpy.signalName = "moveCanceled"
        cancelSpy.clear()
        const startX = item.trackInset
        mousePress(item, startX, item.handleY)
        mouseMove(item, startX + 48, item.handleY, 100)
        verify(item.dragging)
        verify(previewSpy.count > 0)
        item.stops = [{ id: "replacement", position: 0.5, color: "#00ff00" }]
        item.forceActiveFocus()
        verify(item.activeFocus)
        keyClick(Qt.Key_Escape)
        compare(cancelSpy.count, 1)
        compare(cancelSpy.signalArguments[0][0], "left")
        verify(!item.dragging)
    }

    function test_dragFinishesAcrossHostReplacement() {
        const item = make()
        moveFinishSpy.target = item
        moveFinishSpy.signalName = "moveFinished"
        moveFinishSpy.clear()
        const startX = item.trackInset
        mousePress(item, startX, item.handleY)
        mouseMove(item, startX + 36, item.handleY, 100)
        verify(item.dragging)
        item.stops = [{ id: "left", position: 0.2, color: "#ff0000" }]
        mouseRelease(item, startX + 36, item.handleY)
        compare(moveFinishSpy.count, 1)
        compare(moveFinishSpy.signalArguments[0][0], "left")
        verify(!item.dragging)
    }

    function test_addStopIsARequest() {
        const item = make()
        addSpy.target = item
        addSpy.signalName = "addStopRequested"
        addSpy.clear()
        mouseClick(item, item.width / 2, item.handleY)
        compare(addSpy.count, 1)
        verify(addSpy.signalArguments[0][0] > 0.4 && addSpy.signalArguments[0][0] < 0.6)
    }

    function test_deleteUsesHostSelectedId() {
        const item = createTemporaryObject(editorComponent, this, { selectedStopId: "right" })
        verify(item !== null)
        deleteSpy.target = item
        deleteSpy.signalName = "deleteStopRequested"
        deleteSpy.clear()
        item.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        compare(deleteSpy.count, 1)
        compare(deleteSpy.signalArguments[0][0], "right")
    }

    function test_focused_stop_nudges_through_preview_and_finish() {
        const item = make()
        previewSpy.target = item
        previewSpy.signalName = "movePreviewRequested"
        previewSpy.clear()
        moveFinishSpy.target = item
        moveFinishSpy.signalName = "moveFinished"
        moveFinishSpy.clear()
        const stop = findChild(item, "gradientStop_left")
        verify(stop !== null)
        stop.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(previewSpy.count, 1)
        compare(moveFinishSpy.count, 1)
        compare(moveFinishSpy.signalArguments[0][0], "left")
        fuzzyCompare(moveFinishSpy.signalArguments[0][1], 0.01, 0.0001)
    }

    function test_returning_to_original_position_cancels_preview() {
        const item = make()
        cancelSpy.target = item
        cancelSpy.signalName = "moveCanceled"
        cancelSpy.clear()
        moveFinishSpy.target = item
        moveFinishSpy.signalName = "moveFinished"
        moveFinishSpy.clear()
        const startX = item.trackInset
        mousePress(item, startX, item.handleY)
        mouseMove(item, startX + 36, item.handleY, 100)
        mouseMove(item, startX, item.handleY, 100)
        mouseRelease(item, startX, item.handleY)
        compare(cancelSpy.count, 1)
        compare(moveFinishSpy.count, 0)
    }
}
