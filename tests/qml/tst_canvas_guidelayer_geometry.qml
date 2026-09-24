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

    // Host composition from docs/canvas.md#document-guides: a ruler drag
    // previews and adds a document guide; the document records every edit.
    Component {
        id: documentSceneComponent
        Item {
            width: 520
            height: 360
            property alias document: guideDocument
            property alias view: guideView
            property alias guideLayer: documentGuides
            property alias ruler: topRuler
            Tk.CanvasDocument { id: guideDocument; pageRect: Qt.rect(0, 0, 240, 200) }
            Tk.Ruler {
                id: topRuler
                width: 520
                height: Tk.Theme.size.row
                pixelsPerUnit: guideView.zoom
                MouseArea {
                    objectName: "rulerGuideDrag"
                    anchors.fill: parent
                    preventStealing: true
                    onPositionChanged: function(mouse) {
                        const point = mapToItem(documentGuides, mouse.x, mouse.y)
                        documentGuides.previewAdd("horizontal", point.x, point.y)
                    }
                    onReleased: function(mouse) {
                        const point = mapToItem(documentGuides, mouse.x, mouse.y)
                        if (documentGuides.contains(point))
                            documentGuides.finishAdd("horizontal", point.x, point.y)
                        else
                            documentGuides.cancelAdd()
                    }
                    onCanceled: documentGuides.cancelAdd()
                }
            }
            Tk.CanvasView {
                id: guideView
                y: topRuler.height
                width: 520
                height: 360 - topRuler.height
                document: guideDocument
                centerX: 120
                centerY: 100
                Tk.GuideLayer {
                    id: documentGuides
                    anchors.fill: parent
                    surface: guideView
                    guideData: guideDocument.guides
                    onGuideAddRequested: function(orientation, position) {
                        guideDocument.addGuide(orientation, position)
                    }
                    onGuideMoveFinished: function(guideId, position) {
                        guideDocument.moveGuide(guideId, position)
                    }
                    onGuideDeleteRequested: function(guideId) { guideDocument.removeGuide(guideId) }
                }
            }
        }
    }

    function test_ruler_drag_adds_document_guide_at_each_zoom() {
        const scene = createTemporaryObject(documentSceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const zooms = [0.5, 2]
        for (let i = 0; i < zooms.length; ++i) {
            scene.view.zoom = zooms[i]
            mousePress(scene.ruler, 200, scene.ruler.height / 2)
            mouseMove(scene.ruler, 200, 90, -1, Qt.LeftButton)
            verify(scene.guideLayer.pendingGuide !== null)
            compare(scene.document.guides.length, i)
            mouseRelease(scene.ruler, 200, 150)
            compare(scene.guideLayer.pendingGuide, null)
            compare(scene.document.guides.length, i + 1)
            const expected = scene.view.mapToDocument(scene.ruler.mapToItem(scene.view, 200, 150)).y
            fuzzyCompare(scene.document.guides[i].position, expected, 1e-6)
            compare(scene.document.guides[i].orientation, "horizontal")
            compare(scene.document.undoText, "Add guide")
        }
        // Released back on the ruler: the preview is dropped and nothing is added.
        mousePress(scene.ruler, 100, 4)
        mouseMove(scene.ruler, 100, 70, -1, Qt.LeftButton)
        mouseRelease(scene.ruler, 100, 4)
        compare(scene.guideLayer.pendingGuide, null)
        compare(scene.document.guides.length, 2)
    }

    function test_document_guide_drag_and_keys_commit_one_step_each() {
        const scene = createTemporaryObject(documentSceneComponent, this)
        verify(scene !== null)
        waitForRendering(scene)
        const guideId = scene.document.addGuide("vertical", 60)
        const revision = Number(scene.document.revision)
        const action = findChild(scene.guideLayer, "guideAction_" + guideId)
        verify(action !== null)
        const start = action.mapToItem(scene.guideLayer, action.width / 2, action.height / 2)
        mousePress(scene.guideLayer, start.x, start.y)
        mouseMove(scene.guideLayer, start.x + 10, start.y, -1, Qt.LeftButton)
        mouseMove(scene.guideLayer, start.x + 20, start.y, -1, Qt.LeftButton)
        compare(Number(scene.document.revision), revision) // previewed by the layer only
        compare(scene.document.guides[0].position, 60)
        mouseRelease(scene.guideLayer, start.x + 20, start.y)
        compare(Number(scene.document.revision), revision + 1)
        const moved = scene.view.mapToDocument(Qt.point(start.x + 20, start.y)).x
        fuzzyCompare(scene.document.guides[0].position, moved, 1e-6)
        compare(scene.document.undoText, "Move guide")

        findChild(scene.guideLayer, "guideAction_" + guideId).forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(Number(scene.document.revision), revision + 2)
        fuzzyCompare(scene.document.guides[0].position, moved + 1, 1e-6)
        verify(scene.document.undo())
        verify(scene.document.undo())
        compare(scene.document.guides[0].position, 60)

        findChild(scene.guideLayer, "guideAction_" + guideId).forceActiveFocus()
        keyClick(Qt.Key_Delete)
        compare(scene.document.guides.length, 0)
        compare(scene.document.undoText, "Delete guide")
        verify(scene.document.undo())
        compare(scene.document.guides[0].id, guideId)
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
        compare(scene.finishSpy.count, 1) // a key step is a complete gesture
        fuzzyCompare(scene.finishSpy.signalArguments[0][1], 61, 1e-6)
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
