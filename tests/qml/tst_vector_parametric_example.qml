// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/vector-authoring/ParametricWorkingScene.qml the way a user
// would: the ShapePalette creates each family through the reference
// CanvasDocument, the parameter fields regenerate the selected path as one
// undo step each, and dragging a PathEditor node makes the shape freeform
// until the Undo button restores its definition.
TestCase {
    id: root
    name: "VectorParametricExample"
    when: windowShown
    visible: true
    width: 1100
    height: 640

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("../../examples/vector-authoring/ParametricWorkingScene.qml")
    }

    function part(name) {
        const item = findChild(loader.item, name)
        verify(item !== null, name)
        return item
    }
    function focusField(name) {
        const field = part(name)
        verify(field.visible, name + " is shown for the selected shape")
        const input = findChild(field, "numberInput")
        verify(input !== null, name + " input")
        input.forceActiveFocus()
        input.selectAll()
        return field
    }
    function stepField(name, key) {
        focusField(name)
        keyClick(key)
    }
    function typeDigits(name, digits) {
        focusField(name)
        for (let i = 0; i < digits.length; ++i) keyClick(Qt.Key_0 + Number(digits[i]))
        keyClick(Qt.Key_Return)
    }
    function nodeCount(doc, id) { return doc.pathNodes(id).length }

    function test_palette_creates_and_edits_each_family() {
        // Node counts shrink below: stale handles must not throw.
        failOnWarning(/PathEditor\.qml/)
        tryCompare(loader, "status", Loader.Ready)
        const scene = loader.item
        waitForRendering(scene)
        const doc = part("parametricWorkingDocument")
        compare(doc.objectCount, 3)
        compare(scene.selectedKind, "star")
        compare(part("parametricWorkingSelection").text, "SELECTED: star")

        // The initial star: tips and inner radius, one undo step each.
        const star = scene.selectedId
        compare(nodeCount(doc, star), 10)
        stepField("parametricPoints", Qt.Key_Up)
        compare(doc.parametricParameters(star).points, 6)
        compare(nodeCount(doc, star), 12)
        stepField("parametricInnerRadius", Qt.Key_Up)
        fuzzyCompare(doc.parametricParameters(star).innerRadius, 0.5, 1e-9)
        mouseClick(part("parametricWorkingUndo"))
        mouseClick(part("parametricWorkingUndo"))
        compare(doc.parametricParameters(star).points, 5)
        fuzzyCompare(doc.parametricParameters(star).innerRadius, 0.45, 1e-9)
        compare(part("parametricPoints").value, 5)
        fuzzyCompare(part("parametricInnerRadius").value, 0.45, 1e-9)

        // Palette buttons create each family through the document.
        mouseClick(part("shape_polygon"))
        compare(doc.objectCount, 4)
        compare(scene.selectedKind, "polygon")
        compare(part("parametricPoints").label, "Sides")
        verify(!part("parametricInnerRadius").visible)
        stepField("parametricPoints", Qt.Key_Down)
        compare(doc.parametricParameters(scene.selectedId).sides, 5)
        compare(nodeCount(doc, scene.selectedId), 5)

        mouseClick(part("shape_rounded-rectangle"))
        compare(doc.objectCount, 5)
        compare(scene.selectedKind, "rounded-rectangle")
        compare(doc.parametricParameters(scene.selectedId).radius, 22)
        typeDigits("parametricCornerRadius", "60")
        compare(doc.parametricParameters(scene.selectedId).radius, 60)
        compare(nodeCount(doc, scene.selectedId), 4) // full radius: the cubic circle

        mouseClick(part("shape_star"))
        compare(doc.objectCount, 6)
        compare(scene.selectedKind, "star")
        const created = scene.selectedId

        // Dragging a node through the PathEditor makes the star freeform.
        const editor = part("parametricWorkingEditor")
        const handle = findChild(editor, "pathanchor_0")
        verify(handle !== null)
        const x = handle.x + handle.width / 2, y = handle.y + handle.height / 2
        mousePress(editor, x, y)
        mouseMove(editor, x + 15, y + 6, 20, Qt.LeftButton)
        mouseMove(editor, x + 30, y + 12, 20, Qt.LeftButton)
        mouseRelease(editor, x + 30, y + 12)
        compare(scene.selectedKind, "freeform")
        compare(Object.keys(doc.parametricParameters(created)).length, 0)
        verify(!part("parametricPoints").visible)

        // Undo restores the definition, then removes the created star.
        mouseClick(part("parametricWorkingUndo"))
        compare(scene.selectedKind, "star")
        compare(doc.parametricParameters(created).points, 5)
        compare(nodeCount(doc, created), 10)
        mouseClick(part("parametricWorkingUndo"))
        compare(doc.objectCount, 5)
        compare(part("parametricWorkingSelection").text, "SELECTED: None")
    }
}
