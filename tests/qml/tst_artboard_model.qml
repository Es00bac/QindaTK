// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-042 ArtboardView over document artboards: frame and handle drags
// preview in CanvasArtboardModel and commit one undo step, a creation drag
// adds a board (Escape cancels it), and AlignDistributePanel aligns to the
// active board through CanvasAlignmentModel's reference choices.
TestCase {
    id: root
    name: "ArtboardModel"
    when: windowShown
    visible: true
    width: 860
    height: 360

    Component {
        id: sceneComponent
        Item {
            width: 840
            height: 340
            property alias view: canvasView
            property alias document: doc
            property alias boards: boardModel
            property alias artboards: artboardView
            property alias selection: shared
            property alias panel: alignPanel
            Tk.CanvasDocument { id: doc; pageRect: Qt.rect(0, 0, 300, 220) }
            Tk.ToolController { id: tools; document: doc }
            Tk.CanvasSelectionModel { id: shared; document: doc }
            Tk.CanvasArtboardModel { id: boardModel; document: doc }
            Tk.CanvasAlignmentModel {
                id: alignment
                document: doc
                selectionModel: shared
                artboardId: boardModel.activeBoardId
            }
            Tk.CanvasView {
                id: canvasView
                width: 520
                height: 340
                document: doc
                toolController: tools
                Tk.ArtboardView {
                    id: artboardView
                    anchors.fill: parent
                    surface: canvasView
                    artboardModel: boardModel
                }
            }
            Tk.AlignDistributePanel {
                id: alignPanel
                x: 540
                width: 280
                selectedIds: alignment.selectedIds
                referenceOptions: alignment.referenceOptions
                onOperationRequested: function(operation, ids, mode, keyId, spacing) {
                    alignment.apply(operation, ids, mode, keyId, spacing)
                }
            }
        }
    }

    function make() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene !== null)
        waitForRendering(scene)
        scene.view.fitRect(Qt.rect(0, 0, 300, 220))
        return scene
    }

    function test_frame_drag_commits_one_document_step() {
        const scene = make()
        const doc = scene.document
        const id = doc.addArtboard("Cover", Qt.rect(40, 30, 90, 70), "white")
        compare(scene.artboards.boardPolygons.length, 1)
        verify(scene.boards.activate(id))
        compare(scene.artboards.activeBoardId, id)
        const start = scene.view.mapFromDocument(Qt.point(80, 30)) // on the top edge
        const end = Qt.point(start.x + 30, start.y + 24)
        const revision = doc.revision
        mousePress(scene.artboards, start.x, start.y)
        mouseMove(scene.artboards, end.x, end.y)
        verify(scene.boards.previewing)
        compare(doc.revision, revision) // a preview never edits
        mouseRelease(scene.artboards, end.x, end.y)
        compare(doc.revision, revision + 1)
        compare(doc.undoText, "Move artboard")
        const a = scene.view.mapToDocument(start)
        const b = scene.view.mapToDocument(end)
        const rect = doc.artboardInfo(id).rect
        fuzzyCompare(rect.x, 40 + b.x - a.x, 1e-6)
        fuzzyCompare(rect.y, 30 + b.y - a.y, 1e-6)
        compare(rect.width, 90)
        compare(rect.height, 70)
        verify(doc.undo())
        compare(doc.artboardInfo(id).rect, Qt.rect(40, 30, 90, 70))
    }

    function test_handle_drag_resizes_and_frame_click_activates() {
        const scene = make()
        const doc = scene.document
        const first = doc.addArtboard("First", Qt.rect(40, 30, 90, 70), "white")
        const second = doc.addArtboard("Second", Qt.rect(160, 40, 80, 60), "white")
        verify(scene.boards.activate(first))
        const corner = scene.artboards.boardPolygons[0].points[2]
        const end = Qt.point(corner.x + 15, corner.y + 12)
        mousePress(scene.artboards, corner.x, corner.y)
        mouseMove(scene.artboards, end.x, end.y)
        mouseRelease(scene.artboards, end.x, end.y)
        compare(doc.undoText, "Resize artboard")
        const a = scene.view.mapToDocument(corner)
        const b = scene.view.mapToDocument(end)
        const rect = doc.artboardInfo(first).rect
        compare(rect.x, 40)
        compare(rect.y, 30)
        fuzzyCompare(rect.width, 90 + b.x - a.x, 1e-6)
        fuzzyCompare(rect.height, 70 + b.y - a.y, 1e-6)
        // A click on another board's frame only activates it.
        const edge = scene.view.mapFromDocument(Qt.point(200, 40))
        const revision = doc.revision
        mouseClick(scene.artboards, edge.x, edge.y)
        compare(scene.boards.activeBoardId, second)
        compare(doc.revision, revision)
    }

    function test_creation_drag_adds_a_board_and_escape_cancels() {
        const scene = make()
        const doc = scene.document
        scene.artboards.createEnabled = true
        const start = scene.view.mapFromDocument(Qt.point(160, 120))
        const end = Qt.point(start.x + 60, start.y + 45)
        mousePress(scene.artboards, start.x, start.y)
        mouseMove(scene.artboards, end.x, end.y)
        verify(scene.artboards.pendingBoard.width > 0)
        mouseRelease(scene.artboards, end.x, end.y)
        compare(doc.artboardCount, 1)
        compare(doc.undoText, "Add artboard")
        const a = scene.view.mapToDocument(start)
        const b = scene.view.mapToDocument(end)
        const created = doc.artboards[0]
        fuzzyCompare(created.rect.x, Math.min(a.x, b.x), 1e-6)
        fuzzyCompare(created.rect.width, Math.abs(b.x - a.x), 1e-6)
        fuzzyCompare(created.rect.height, Math.abs(b.y - a.y), 1e-6)
        compare(scene.boards.activeBoardId, created.id)
        compare(scene.artboards.pendingBoard.width, 0)

        const other = scene.view.mapFromDocument(Qt.point(20, 180))
        mousePress(scene.artboards, other.x, other.y)
        mouseMove(scene.artboards, other.x + 40, other.y + 20)
        keyClick(Qt.Key_Escape)
        mouseRelease(scene.artboards, other.x + 40, other.y + 20)
        compare(doc.artboardCount, 1)
    }

    function test_panel_aligns_to_the_active_board() {
        const scene = make()
        const doc = scene.document
        const board = doc.addArtboard("Cover", Qt.rect(40, 30, 90, 70), "white")
        verify(scene.boards.activate(board))
        const shape = doc.addRect(150, 150, 20, 10, "red")
        scene.selection.selectedIds = [shape]
        compare(scene.panel.selectedIds.length, 1)
        compare(scene.panel.referenceOptions.length, 4)
        scene.panel.referenceMode = "artboard"
        verify(scene.panel.actionAvailable)
        findChild(scene.panel, "alignLeftButton").click()
        compare(doc.objectInfo(shape).bounds.x, 40)
        compare(doc.undoText, "Align objects")
        verify(doc.undo())
        compare(doc.objectInfo(shape).bounds.x, 150)
    }
}
