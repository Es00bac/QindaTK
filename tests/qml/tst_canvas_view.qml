// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "CanvasView"
    when: windowShown
    visible: true
    width: 520
    height: 300

    Component {
        id: sceneComponent
        Item {
            id: scene
            width: 500
            height: 280
            property alias document: doc
            property alias tools: controller
            property alias leftCanvas: leftView
            property alias rightCanvas: rightView
            Tk.CanvasDocument {
                id: doc
                pageRect: Qt.rect(0, 0, 200, 200)
            }
            Tk.ToolController {
                id: controller
                document: doc
            }
            Tk.CanvasViewState { id: shared }
            Tk.CanvasView {
                id: leftView
                viewId: "left"
                width: 300
                height: 280
                document: doc
                toolController: controller
                viewState: shared
                accessibleName: "Artboard"
            }
            Tk.CanvasView {
                id: rightView
                viewId: "right"
                x: 300
                width: 200
                height: 280
                document: doc
                toolController: controller
                viewState: shared
            }
        }
    }

    function make() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene !== null)
        waitForRendering(scene)
        return scene
    }

    function test_contract_and_accessibility() {
        const scene = make()
        const view = scene.leftCanvas
        compare(view.Accessible.role, Accessible.Canvas)
        compare(view.Accessible.name, "Artboard")
        verify(view.activeFocusOnTab)
        verify(findChild(view, "canvasOverlay") !== null)
        verify(findChild(view, "canvasOverlaySlot") !== null)
        view.forceActiveFocus()
        verify(findChild(view, "canvasFocusRing").visible)
        compare(view.canvasColor, Tk.Theme.color.canvas)
        compare(view.hitTolerance, Tk.Theme.space.xs)
        compare(view.implicitHeight, Tk.Theme.size.panelMinHeight)
    }

    function test_keyboard_navigation_keeps_document() {
        const scene = make()
        const view = scene.leftCanvas
        const id = scene.document.addRect(40, 40, 60, 60, "red")
        const bounds = scene.document.objectInfo(id).bounds
        view.fitDocument()
        view.forceActiveFocus()
        const zoom = view.zoom
        keyClick(Qt.Key_Plus)
        fuzzyCompare(view.zoom, zoom * view.wheelZoomStep, 1e-9)
        keyClick(Qt.Key_R)
        compare(view.viewRotation, view.rotationStep)
        keyClick(Qt.Key_Left)
        // Synchronized: the other view follows every step.
        compare(scene.rightCanvas.zoom, view.zoom)
        compare(scene.rightCanvas.viewRotation, view.viewRotation)
        compare(scene.rightCanvas.centerX, view.centerX)
        // Fit keeps the rotation, so turn back before comparing zoom.
        keyClick(Qt.Key_R, Qt.ShiftModifier)
        compare(view.viewRotation, 0)
        keyClick(Qt.Key_0, Qt.ControlModifier)
        fuzzyCompare(view.zoom, zoom, 1e-9)
        compare(scene.document.objectInfo(id).bounds, bounds)
    }

    function test_drawing_and_escape_through_the_view() {
        const scene = make()
        const view = scene.leftCanvas
        view.fitDocument()
        scene.tools.activeTool = "rectangle"
        const revision = scene.document.revision
        mousePress(view, 60, 60)
        mouseMove(view, 120, 120)
        compare(scene.tools.gestureOwner, "left")
        compare(scene.tools.preview.kind, "rect")
        keyClick(Qt.Key_Escape)
        compare(scene.tools.lastOutcome, "canceled:escape")
        mouseRelease(view, 120, 120)
        compare(scene.document.revision, revision)
        mouseDrag(view, 60, 60, 80, 50)
        compare(scene.tools.lastOutcome, "committed")
        compare(scene.document.objectCount, 1)
        // Picking agrees across the two synchronized views.
        const docPoint = view.mapToDocument(Qt.point(100, 85))
        const inRight = scene.rightCanvas.mapFromDocument(docPoint)
        const a = view.hitTest(100, 85)
        const b = scene.rightCanvas.hitTest(inRight.x, inRight.y)
        compare(a.length, 1)
        compare(b.length, 1)
        compare(a[0].objectId, b[0].objectId)
        verify(scene.document.undo())
        compare(scene.document.objectCount, 0)
    }

    function test_space_is_a_temporary_pan_tool() {
        const scene = make()
        const view = scene.leftCanvas
        view.forceActiveFocus()
        scene.tools.activeTool = "rectangle"
        keyPress(Qt.Key_Space)
        compare(scene.tools.effectiveTool, "pan")
        const cx = view.centerX
        mouseDrag(view, 100, 100, 40, 0)
        verify(view.centerX < cx)
        compare(scene.document.objectCount, 0)
        keyRelease(Qt.Key_Space)
        compare(scene.tools.effectiveTool, "rectangle")
    }
}
