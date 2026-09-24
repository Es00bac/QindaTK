// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "CanvasSelectionOverlayGeometry"
    when: windowShown
    visible: true
    width: 520
    height: 340

    Component {
        id: sceneComponent
        Item {
            width: 500
            height: 320
            property alias document: documentItem
            property alias view: canvasView
            property alias overlay: selectionOverlay
            property alias transformSpy: requestSpy
            property alias rotationStepSpy: rotationStepSignal
            property alias nudgeSpy: nudgeSignal
            Tk.CanvasDocument {
                id: documentItem
                pageRect: Qt.rect(0, 0, 240, 200)
            }
            Tk.ToolController { document: documentItem }
            Tk.CanvasView {
                id: canvasView
                width: 500
                height: 320
                document: documentItem
                zoom: 1.4
                viewRotation: 27
                centerX: 120
                centerY: 100
                Tk.SelectionOverlay {
                    id: selectionOverlay
                    anchors.fill: parent
                    surface: canvasView
                    selectionBounds: Qt.rect(40, 50, 80, 60)
                }
            }
            SignalSpy {
                id: requestSpy
                target: selectionOverlay
                signalName: "transformRequested"
            }
            SignalSpy {
                id: rotationStepSignal
                target: selectionOverlay
                signalName: "rotateStepRequested"
            }
            SignalSpy {
                id: nudgeSignal
                target: selectionOverlay
                signalName: "transformNudgeRequested"
            }
        }
    }

    function test_bounds_follow_pose_and_handle_reports_document_drag() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const overlay = scene.overlay
        const points = [scene.view.mapFromDocument(Qt.point(40, 50)),
                        scene.view.mapFromDocument(Qt.point(120, 50)),
                        scene.view.mapFromDocument(Qt.point(120, 110)),
                        scene.view.mapFromDocument(Qt.point(40, 110))]
        const xs = points.map(p => p.x)
        const ys = points.map(p => p.y)
        fuzzyCompare(overlay.viewBounds.x, Math.min.apply(null, xs), 0.01)
        fuzzyCompare(overlay.viewBounds.width,
                     Math.max.apply(null, xs) - Math.min.apply(null, xs), 0.01)
        fuzzyCompare(overlay.viewBounds.y, Math.min.apply(null, ys), 0.01)
        const oldX = overlay.viewBounds.x
        const oldRevision = overlay.geometryRevision
        scene.view.panBy(36, -18)
        tryCompare(overlay, "geometryRevision", oldRevision + 1)
        fuzzyCompare(overlay.viewBounds.x, oldX + 36, 0.01)

        const spy = scene.transformSpy
        const handle = findChild(overlay, "handleTopLeft")
        verify(handle !== null)
        const start = handle.mapToItem(overlay, handle.width / 2, handle.height / 2)
        mouseDrag(overlay, start.x, start.y, 9, 7)
        verify(spy.count >= 1)
        compare(spy.signalArguments[0][0], "topLeft")
        const emittedStart = spy.signalArguments[0][1]
        const expectedStart = scene.view.mapToDocument(start)
        fuzzyCompare(emittedStart.x, expectedStart.x, 0.5)
        fuzzyCompare(emittedStart.y, expectedStart.y, 0.5)
    }

    function test_rotation_handle_reports_drag_and_keyboard_step() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const handle = findChild(scene.overlay, "handleRotate")
        verify(handle !== null)
        const start = handle.mapToItem(scene.overlay, handle.width / 2, handle.height / 2)
        mouseDrag(scene.overlay, start.x, start.y, start.x + 15, start.y + 12)
        verify(scene.transformSpy.count > 0)
        compare(scene.transformSpy.signalArguments[0][0], "rotate")
        const emitted = scene.transformSpy.signalArguments[0][1]
        const expected = scene.view.mapToDocument(start)
        fuzzyCompare(emitted.x, expected.x, 0.5)
        fuzzyCompare(emitted.y, expected.y, 0.5)
        handle.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(scene.rotationStepSpy.count, 1)
        compare(scene.rotationStepSpy.signalArguments[0][0], 15)
    }

    function test_handle_drag_at_view_edge_autoscrolls() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const overlay = scene.overlay
        const handle = findChild(overlay, "handleRight")
        verify(handle !== null)
        const start = handle.mapToItem(overlay, handle.width / 2, handle.height / 2)
        const edgeX = overlay.width - overlay.autoScrollMargin / 2
        const y = overlay.height / 2
        mousePress(overlay, start.x, start.y)
        mouseMove(overlay, edgeX, y, -1, Qt.LeftButton)
        verify(overlay.autoScrolling)
        const before = scene.view.mapToDocument(Qt.point(edgeX, y))
        scene.transformSpy.clear()
        verify(overlay.autoScrollStep(0.05))
        const after = scene.view.mapToDocument(Qt.point(edgeX, y))
        verify(Math.hypot(after.x - before.x, after.y - before.y) > 0.1)
        const last = scene.transformSpy.signalArguments[scene.transformSpy.count - 1]
        compare(last[0], "right")
        fuzzyCompare(last[2].x, after.x, 1e-6)
        fuzzyCompare(last[2].y, after.y, 1e-6)
        mouseRelease(overlay, edgeX, y)
        verify(!overlay.autoScrolling)
        verify(!overlay.autoScrollStep(0.05))
    }

    function test_move_target_reports_keyboard_move() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const target = findChild(scene.overlay, "selectionMoveTarget")
        verify(target !== null)
        compare(target.Accessible.name, "Move selection")
        fuzzyCompare(target.width, scene.overlay.viewBounds.width, 0.01)
        target.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(scene.nudgeSpy.count, 1)
        compare(scene.nudgeSpy.signalArguments[0][0], "move")
        compare(scene.nudgeSpy.signalArguments[0][1], -Tk.Theme.size.controlSm)
        compare(scene.nudgeSpy.signalArguments[0][2], 0)
    }
}
