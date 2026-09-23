// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "ArtboardView"
    when: windowShown
    visible: true
    width: 540
    height: 360

    Component {
        id: sceneComponent
        Item {
            width: 520
            height: 340
            property alias view: canvasView
            property alias artboards: artboardView
            property alias activateSpy: activateSignal
            property alias arrangeSpy: arrangeSignal
            property alias finishSpy: finishSignal
            property alias cancelSpy: cancelSignal
            property alias resizeSpy: resizeSignal
            Tk.CanvasDocument { id: document; pageRect: Qt.rect(0, 0, 300, 220) }
            Tk.ToolController { document: document }
            Tk.CanvasView {
                id: canvasView
                width: 520
                height: 340
                document: document
                viewRotation: 23
                zoom: 1.3
                centerX: 150
                centerY: 110
                Tk.ArtboardView {
                    id: artboardView
                    anchors.fill: parent
                    surface: canvasView
                    boardData: [{ id: "board-a", name: "Cover", rect: Qt.rect(40, 30, 90, 70),
                                 background: Tk.Theme.color.surface },
                                { id: "board-b", name: "Back", rect: Qt.rect(160, 50, 80, 60),
                                 background: Tk.Theme.color.panel }]
                    activeBoardId: "board-a"
                }
            }
            SignalSpy { id: activateSignal; target: artboardView; signalName: "boardActivateRequested" }
            SignalSpy { id: arrangeSignal; target: artboardView; signalName: "boardArrangePreview" }
            SignalSpy { id: finishSignal; target: artboardView; signalName: "boardArrangeFinished" }
            SignalSpy { id: cancelSignal; target: artboardView; signalName: "boardArrangeCanceled" }
            SignalSpy { id: resizeSignal; target: artboardView; signalName: "boardResizePreview" }
        }
    }

    function test_maps_boards_and_click_only_activates() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        compare(scene.artboards.boardPolygons.length, 2)
        const label = findChild(scene.artboards, "artboardLabel_board-a")
        verify(label !== null)
        compare(label.Accessible.name, "Cover")
        const center = scene.view.mapFromDocument(Qt.point(80, 65))
        mouseClick(scene.artboards, center.x, center.y)
        compare(scene.activateSpy.count, 1)
        compare(scene.finishSpy.count, 0)
    }

    function test_drag_keeps_stable_identity_after_host_refresh() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const start = scene.view.mapFromDocument(Qt.point(80, 65))
        mousePress(scene.artboards, start.x, start.y)
        scene.artboards.boardData = [{ id: "replacement", name: "Replacement",
                                       rect: Qt.rect(40, 30, 90, 70) }]
        mouseMove(scene.artboards, start.x + 14, start.y + 8, 100)
        verify(scene.arrangeSpy.count > 0)
        compare(scene.arrangeSpy.signalArguments[0][0], "board-a")
        const initial = scene.arrangeSpy.signalArguments[0][1]
        const expected = scene.view.mapToDocument(start)
        fuzzyCompare(initial.x, expected.x, 0.5)
        mouseRelease(scene.artboards, start.x + 14, start.y + 8)
        compare(scene.finishSpy.count, 1)
        compare(scene.finishSpy.signalArguments[0][0], "board-a")
    }

    function test_escape_cancels_drag_and_suppresses_release_activation() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const start = scene.view.mapFromDocument(Qt.point(80, 65))
        mousePress(scene.artboards, start.x, start.y)
        mouseMove(scene.artboards, start.x + 12, start.y + 5, 100)
        verify(scene.arrangeSpy.count > 0)
        keyClick(Qt.Key_Escape)
        mouseRelease(scene.artboards, start.x + 12, start.y + 5)
        compare(scene.cancelSpy.count, 1)
        compare(scene.finishSpy.count, 0)
        compare(scene.activateSpy.count, 0)
    }

    function test_active_bottom_right_handle_reports_resize_points() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const start = scene.artboards.boardPolygons[0].points[2]
        verify(scene.artboards.resizeHit(scene.artboards.boardData[0], start))
        mouseDrag(scene.artboards, start.x, start.y, start.x + 18, start.y + 12)
        verify(scene.resizeSpy.count > 0)
        compare(scene.resizeSpy.signalArguments[0][0], "board-a")
        const point = scene.resizeSpy.signalArguments[0][1]
        const expected = scene.view.mapToDocument(start)
        fuzzyCompare(point.x, expected.x, 0.5)
        fuzzyCompare(point.y, expected.y, 0.5)
    }
}
