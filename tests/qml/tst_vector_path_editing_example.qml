// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/vector-authoring/PathWorkingScene.qml: clicking a node
// selects it by stable id; Insert, Split, Join, Smooth and Delete act on the
// selection through PathEditor's CanvasPathEditModel; the selection survives
// segment insertion and undo; a refused delete is explained in the panel.
TestCase {
    id: root
    name: "VectorPathEditingExample"
    when: windowShown
    visible: true
    width: 1100
    height: 640

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("../../examples/vector-authoring/PathWorkingScene.qml")
    }

    function init() {
        loader.active = false
        loader.active = true
        tryCompare(loader, "status", Loader.Ready)
        tryCompare(part("pathWorkingEditor").nodes, "length", 4)
    }

    function part(name) {
        const item = findChild(loader.item, name)
        verify(item !== null, name)
        return item
    }

    function test_click_selects_and_insertion_keeps_the_selection() {
        const editor = part("pathWorkingEditor")
        const model = editor.model
        const anchor = findChild(editor, "pathanchor_2")
        mouseClick(anchor)
        compare(model.selectedNodeIds.length, 1)
        const id = model.selectedNodeIds[0]
        compare(id, editor.nodes[2].id)
        verify(anchor.selected)
        compare(part("pathWorkingNodeInfo").text, "Node " + id + " selected (corner)")

        part("pathWorkingInsert").clicked()
        compare(editor.nodes.length, 5)
        compare(model.selectedNodeIds, [id])
        compare(editor.nodes[2].id, id)
        verify(findChild(editor, "pathanchor_2").selected)
        verify(!findChild(editor, "pathanchor_3").selected)
        part("pathWorkingUndo").clicked()
        compare(editor.nodes.length, 4)
        compare(model.selectedNodeIds, [id])

        part("pathWorkingSmooth").clicked()
        compare(model.node(id).type, "smooth")
        compare(part("pathWorkingNodeInfo").text, "Node " + id + " selected (smooth)")
        part("pathWorkingCorner").clicked()
        compare(model.node(id).type, "corner")
    }

    function test_split_join_and_explained_refusals() {
        const editor = part("pathWorkingEditor")
        const model = editor.model
        mouseClick(findChild(editor, "pathanchor_1"))
        const id = model.selectedNodeIds[0]
        part("pathWorkingSplit").clicked()
        compare(model.contours[0].closed, false)
        compare(editor.nodes.length, 5)
        compare(editor.nodes[0].id, id)
        // Select both ends (they coincide) and join them again.
        model.selectNode(editor.nodes[4].id, true)
        compare(model.selectedNodeIds.length, 2)
        part("pathWorkingJoin").clicked()
        compare(model.contours[0].closed, true)
        compare(editor.nodes.length, 4)

        // The inner contour: Delete from the keyboard, then a refused delete.
        part("pathWorkingNextContour").clicked()
        tryCompare(editor, "contourIndex", 1)
        compare(editor.nodes.length, 4)
        mouseClick(findChild(editor, "pathanchor_0"))
        keyClick(Qt.Key_Delete)
        compare(editor.nodes.length, 3)
        compare(part("pathWorkingError").visible, false)
        mouseClick(findChild(editor, "pathanchor_0"))
        part("pathWorkingDelete").clicked()
        compare(editor.nodes.length, 3)
        verify(part("pathWorkingError").visible)
        verify(part("pathWorkingError").text.indexOf("three") >= 0)
    }
}
