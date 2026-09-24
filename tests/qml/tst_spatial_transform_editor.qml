// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-041: the numeric inspector commits the same one-step operations as the
// transform handles, and a refused value snaps back to the document.
TestCase {
    id: root
    name: "SpatialTransformEditor"
    when: windowShown
    visible: true
    width: 360
    height: 260

    Component {
        id: sceneComponent
        Item {
            width: 340
            height: 240
            property alias document: editorDocument
            property alias tools: editorTools
            property alias spatial: editorTransform
            property alias editor: transformEditor
            property var firstId: 0
            property var secondId: 0
            Tk.CanvasDocument {
                id: editorDocument
                pageRect: Qt.rect(0, 0, 400, 300)
            }
            Tk.ToolController { id: editorTools; document: editorDocument }
            Tk.SpatialTransform {
                id: editorTransform
                document: editorDocument
                tools: editorTools
            }
            Tk.SpatialTransformEditor {
                id: transformEditor
                width: 320
                controller: editorTransform
            }
            Component.onCompleted: {
                firstId = editorDocument.addRect(10, 20, 40, 30, "#4a81a1")
                secondId = editorDocument.addRect(70, 20, 20, 30, "#a98655")
                editorTools.selection = [firstId, secondId]
            }
        }
    }

    function field(scene, name) {
        const item = findChild(scene.editor, name)
        verify(item !== null, name)
        return item
    }
    function type(scene, name, text) {
        const input = findChild(field(scene, name), "numberInput")
        input.forceActiveFocus()
        input.selectAll()
        for (let i = 0; i < text.length; ++i) keyClick(text[i])
        keyClick(Qt.Key_Return)
    }

    function test_fields_follow_selection_and_rows_are_control_height() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene !== null)
        waitForRendering(scene)
        compare(field(scene, "spatialX").value, 10)
        compare(field(scene, "spatialY").value, 20)
        compare(field(scene, "spatialWidth").value, 80)
        compare(field(scene, "spatialHeight").value, 30)
        compare(field(scene, "spatialRotation").value, 0)
        compare(field(scene, "spatialPivotX").value, 50)
        compare(field(scene, "spatialPivotY").value, 35)
        compare(field(scene, "spatialX").height, Tk.Theme.size.control)
        compare(scene.editor.Accessible.name, "Selection transform")
        verify(scene.editor.editable)
        scene.tools.selection = []
        verify(!scene.editor.available)
        compare(field(scene, "spatialWidth").value, 0)
    }

    function test_typed_values_commit_the_handle_operations() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene !== null)
        waitForRendering(scene)
        const revision = Number(scene.document.revision)
        type(scene, "spatialX", "25")
        compare(Number(scene.document.revision), revision + 1)
        compare(scene.document.undoText, "Move selection")
        compare(scene.document.objectInfo(scene.secondId).bounds.x, 85)
        compare(field(scene, "spatialX").value, 25)

        type(scene, "spatialWidth", "160")
        compare(scene.document.undoText, "Resize selection")
        compare(scene.document.objectInfo(scene.secondId).bounds.width, 40)
        compare(field(scene, "spatialPivotX").value, 105)

        type(scene, "spatialRotation", "90")
        compare(scene.document.undoText, "Rotate selection")
        fuzzyCompare(scene.spatial.rotation, 90, 1e-9)
        compare(Number(scene.document.revision), revision + 3)

        verify(scene.document.undo())
        verify(scene.document.undo())
        verify(scene.document.undo())
        compare(scene.document.objectInfo(scene.secondId).bounds.x, 70)
        compare(field(scene, "spatialX").value, 10)
    }

    function test_refused_value_snaps_back_and_gesture_locks_fields() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene !== null)
        waitForRendering(scene)
        const revision = Number(scene.document.revision)
        type(scene, "spatialHeight", "0")
        compare(Number(scene.document.revision), revision)
        compare(field(scene, "spatialHeight").value, 30)

        type(scene, "spatialPivotX", "10")
        compare(scene.spatial.pivot.x, 10)
        compare(Number(scene.document.revision), revision) // pivot is editor state

        scene.forceActiveFocus() // a pressed handle takes focus before its gesture
        verify(scene.spatial.update("move", Qt.point(10, 20), Qt.point(14, 26)))
        verify(!scene.editor.editable)
        compare(field(scene, "spatialX").value, 14) // follows the preview
        verify(scene.spatial.cancel())
        verify(scene.editor.editable)
        compare(field(scene, "spatialX").value, 10)
    }
}
