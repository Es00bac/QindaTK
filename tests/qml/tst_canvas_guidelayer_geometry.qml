// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "CanvasGuideLayerGeometry"
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
            property alias guideLayer: guideLayer
            property alias addSpy: guideAddSpy
            property alias moveSpy: guideMoveSpy
            property alias finishSpy: guideFinishSpy
            property alias deleteSpy: guideDeleteSpy

            Tk.CanvasDocument { id: document; pageRect: Qt.rect(0, 0, 240, 200) }
            Tk.ToolController { document: document }
            Tk.CanvasView {
                id: canvasView
                width: 520
                height: 340
                document: document
                viewRotation: 31
                zoom: 1.6
                centerX: 120
                centerY: 100
                Tk.GuideLayer {
                    id: guideLayer
                    anchors.fill: parent
                    surface: canvasView
                    guideData: [{ id: "northRail", orientation: "vertical",
                                  position: 60, label: "Reference rail" }]
                }
            }
            SignalSpy { id: guideAddSpy; target: guideLayer; signalName: "guideAddRequested" }
            SignalSpy { id: guideMoveSpy; target: guideLayer; signalName: "guideMoveRequested" }
            SignalSpy { id: guideFinishSpy; target: guideLayer; signalName: "guideMoveFinished" }
            SignalSpy { id: guideDeleteSpy; target: guideLayer; signalName: "guideDeleteRequested" }
        }
    }

    function test_guide_geometry_add_mapping_and_accessible_actions() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const action = findChild(scene.guideLayer, "guideAction_northRail")
        verify(action !== null)
        verify(action.geometry !== null)
        verify(action.geometry.length > 0)
        compare(action.Accessible.name, "Reference rail")

        const expected = Qt.point(82, 47)
        const viewPoint = scene.view.mapFromDocument(expected)
        verify(scene.guideLayer.requestAdd("vertical", viewPoint.x, viewPoint.y))
        compare(scene.addSpy.count, 1)
        compare(scene.addSpy.signalArguments[0][0], "vertical")
        fuzzyCompare(scene.addSpy.signalArguments[0][1], expected.x, 1e-6)

        action.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(scene.moveSpy.count, 1)
        compare(scene.moveSpy.signalArguments[0][0], "northRail")
        fuzzyCompare(scene.moveSpy.signalArguments[0][1], 61, 1e-6)
        keyClick(Qt.Key_Delete)
        compare(scene.deleteSpy.count, 1)
        compare(scene.deleteSpy.signalArguments[0][0], "northRail")
    }

    function test_drag_reports_moves_and_finish() {
        const scene = createTemporaryObject(sceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const action = findChild(scene.guideLayer, "guideAction_northRail")
        const start = action.mapToItem(scene.guideLayer, action.width / 2, action.height / 2)
        mouseDrag(scene.guideLayer, start.x, start.y, 8, 6)
        verify(scene.moveSpy.count > 0)
        compare(scene.finishSpy.count, 1)
        compare(scene.finishSpy.signalArguments[0][0], "northRail")
    }
}
