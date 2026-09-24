// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// A canvas session created by the DocumentManager drives a CanvasView through
// the generic `model` property, and its gestures land in the shared history.
TestCase {
    id: root
    name: "CanvasSession"
    when: windowShown
    visible: true
    width: 420
    height: 300

    Tk.DocumentManager { id: manager }

    Component {
        id: viewComponent
        Item {
            id: scene
            property var session: null
            property alias canvas: view
            property alias tools: controller
            width: 400
            height: 280
            Tk.ToolController {
                id: controller
                document: scene.session ? scene.session.model : null
            }
            Tk.CanvasView {
                id: view
                viewId: "board"
                anchors.fill: parent
                document: scene.session ? scene.session.model : null
                toolController: controller
            }
        }
    }

    function test_session_model_drives_view_and_shared_history() {
        manager.recoveryManager.directory = "" // keep autosave out of the user's data
        const session = manager.createDocument("qindatk.canvas-document.v1")
        verify(session !== null)
        compare(session.kind, "canvas")
        compare(session.document, null)
        const scene = createTemporaryObject(viewComponent, root, {session: session})
        verify(scene !== null)
        waitForRendering(scene)
        compare(scene.canvas.document, session.model)
        scene.tools.activeTool = "rectangle"
        mouseDrag(scene.canvas, 60, 60, 80, 50)
        compare(scene.tools.lastOutcome, "committed")
        compare(session.model.objectCount, 1)
        verify(session.dirty)
        compare(manager.undoService.activeModel, session.model)
        compare(manager.undoService.undoText, "Draw rectangle")
        verify(manager.undoService.undo())
        compare(session.model.objectCount, 0)
        verify(session.model.canRedo)
        verify(manager.closeDocument(session.documentId, Tk.DocumentManager.Discard))
    }
}
