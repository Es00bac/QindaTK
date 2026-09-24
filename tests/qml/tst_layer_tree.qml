// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-042 LayerTree over the document hierarchy: virtual rows, shared
// selection with reveal and no feedback, keyboard and drag reorder, and
// visibility, lock and rename as single document steps.
TestCase {
    id: root
    name: "LayerTree"
    when: windowShown
    visible: true
    width: 360
    height: 540

    Component {
        id: sceneComponent
        Item {
            width: 340
            height: 520
            property alias document: doc
            property alias selection: shared
            property alias tools: controller
            property alias tree: layerTree
            property alias revealSpy: revealSignal
            Tk.CanvasDocument { id: doc; pageRect: Qt.rect(0, 0, 200, 200) }
            Tk.CanvasSelectionModel { id: shared; document: doc }
            Tk.ToolController { id: controller; document: doc; selectionModel: shared }
            Tk.LayerTree {
                id: layerTree
                width: 320
                listHeight: 400
                document: doc
                selectionModel: shared
            }
            SignalSpy { id: revealSignal; target: layerTree.treeModel; signalName: "revealRequested" }
        }
    }

    Component {
        id: emptyComponent
        Tk.LayerTree { width: 240 }
    }

    // Top (front) above Group, which holds Base with a red object.
    function make() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene !== null)
        const doc = scene.document
        const base = doc.activeLayer
        const red = doc.addRect(10, 10, 50, 50, "red")
        const top = doc.addLayerNamed("Top")
        doc.activeLayer = top
        const blue = doc.addRect(30, 30, 50, 50, "blue")
        const group = doc.groupLayersById([base], "Group")
        verify(group > 0)
        waitForRendering(scene)
        return { scene: scene, doc: doc, tree: scene.tree, base: base, top: top, group: group,
                 red: red, blue: blue }
    }

    function layerRecord(doc, id) {
        const layers = doc.layers
        for (let i = 0; i < layers.length; ++i)
            if (String(layers[i].id) === String(id)) return layers[i]
        return null
    }

    function row(tree, name) {
        let item = null
        tryVerify(function() { item = findChild(tree, name); return item !== null })
        return item
    }

    function test_rows_follow_the_document_hierarchy() {
        const s = make()
        const adapter = s.tree.collection
        compare(s.tree.Accessible.role, Accessible.Tree)
        compare(adapter.treeCount, 3)
        compare(adapter.treeIdAt(0), "layer:" + s.top)
        compare(adapter.treeIdAt(1), "layer:" + s.group)
        compare(adapter.treeIdAt(2), "layer:" + s.base)
        compare(adapter.treeDataAt(2, "treeDepth"), 1)
        const groupRow = row(s.tree, "layerRow_" + s.group)
        compare(groupRow.Accessible.role, Accessible.TreeItem)
        verify(findChild(s.tree, "layerVisibility_" + s.base) !== null)
        verify(findChild(s.tree, "layerLock_" + s.base) !== null)
        // A collapsed group stays collapsed through later document edits.
        s.tree.toggleExpanded("layer:" + s.group)
        compare(adapter.treeCount, 2)
        verify(s.doc.addLayerNamed("Extra") > 0)
        compare(adapter.treeCount, 3)
        compare(adapter.treeIndexOfId("layer:" + s.base), -1)
    }

    function test_clicks_share_selection_and_canvas_selection_reveals() {
        const s = make()
        const baseRow = row(s.tree, "layerRow_" + s.base)
        mouseClick(baseRow, baseRow.width / 2, baseRow.height / 2)
        compare(String(s.doc.activeLayer), String(s.base))
        compare(s.tree.currentKey, "layer:" + s.base)

        // A canvas-side selection reveals its row and writes nothing back.
        s.scene.tools.selection = [s.red]
        compare(s.scene.revealSpy.count, 1)
        compare(s.tree.currentKey, "object:" + s.red)
        verify(s.tree.collection.treeIndexOfId("object:" + s.red) >= 0)
        compare(s.scene.selection.selectedIds.length, 1)
        compare(String(s.scene.selection.selectedIds[0]), String(s.red))

        // A Ctrl+click in the tree adds to the shared selection without a reveal.
        s.tree.setExpanded("layer:" + s.top, true)
        const blueRow = row(s.tree, "objectRow_" + s.blue)
        mouseClick(blueRow, blueRow.width / 2, blueRow.height / 2, Qt.LeftButton, Qt.ControlModifier)
        compare(s.scene.tools.selection.length, 2)
        compare(s.scene.revealSpy.count, 1)
        compare(s.tree.currentKey, "object:" + s.blue)
        compare(String(s.doc.activeLayer), String(s.top))
    }

    function test_keyboard_reorders_indents_and_undoes() {
        const s = make()
        s.tree.setCurrent("layer:" + s.top)
        s.tree.forceActiveFocus()
        keyClick(Qt.Key_Down, Qt.ControlModifier) // one row down: behind the group
        compare(s.doc.childLayerIds(0).map(String), [String(s.top), String(s.group)])
        compare(s.tree.collection.treeIdAt(0), "layer:" + s.group)
        compare(s.doc.undoText, "Move layer")
        keyClick(Qt.Key_Right, Qt.ControlModifier) // into the group shown above
        compare(String(layerRecord(s.doc, s.top).parent), String(s.group))
        keyClick(Qt.Key_Left, Qt.ControlModifier)
        compare(String(layerRecord(s.doc, s.top).parent), "0")
        compare(s.doc.childLayerIds(0).map(String), [String(s.group), String(s.top)])
        verify(s.doc.undo())
        verify(s.doc.undo())
        compare(String(layerRecord(s.doc, s.top).parent), "0")
        compare(s.doc.childLayerIds(0).map(String), [String(s.top), String(s.group)])
    }

    function test_dragging_a_row_into_a_group_is_one_step() {
        const s = make()
        const topRow = row(s.tree, "layerRow_" + s.top)
        const height = topRow.height
        const x = topRow.width / 2
        mousePress(topRow, x, height / 2)
        mouseMove(topRow, x, height * 1.5) // the middle of the group row
        verify(s.tree.dragging)
        verify(findChild(s.tree, "layerTreeDropMarker").visible)
        mouseRelease(topRow, x, height * 1.5)
        compare(s.doc.undoText, "Move layer")
        compare(s.doc.childLayerIds(s.group).map(String), [String(s.base), String(s.top)])
        verify(!s.tree.dragging)
        verify(s.doc.undo())
        compare(String(layerRecord(s.doc, s.top).parent), "0")
    }

    function test_visibility_lock_and_rename_are_document_steps() {
        const s = make()
        mouseClick(row(s.tree, "layerVisibility_" + s.group))
        compare(s.doc.undoText, "Hide layer")
        verify(!s.doc.objectInfo(s.red).effectiveVisible)
        verify(s.doc.undo())
        verify(s.doc.objectInfo(s.red).effectiveVisible)
        mouseClick(row(s.tree, "layerLock_" + s.top))
        compare(s.doc.undoText, "Lock layer")
        verify(s.doc.objectInfo(s.blue).effectiveLocked)

        s.tree.setCurrent("layer:" + s.base)
        s.tree.forceActiveFocus()
        keyClick(Qt.Key_F2)
        const field = findChild(s.tree, "layerTreeRename")
        verify(field.visible)
        field.text = "Renamed"
        keyClick(Qt.Key_Return)
        compare(s.doc.undoText, "Rename layer")
        compare(layerRecord(s.doc, s.base).name, "Renamed")
        verify(!field.visible)
    }

    function test_empty_states() {
        const tree = createTemporaryObject(emptyComponent, root)
        verify(tree !== null)
        const empty = findChild(tree, "layerTreeEmpty")
        verify(empty.visible)
        compare(empty.text, "No document")
        compare(tree.collection.treeCount, 0)
    }
}
