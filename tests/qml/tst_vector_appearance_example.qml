// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/vector-authoring/StyleWorkingScene.qml: the stroke, fill,
// opacity and blend controls write the selection through CanvasStyleModel and
// read the document back (undo included); AppearanceStack requests edit the
// key object's effects through CanvasAppearanceModel; a path button combines
// the selection. Requests are emitted as the panels emit them, so the checks
// do not depend on where the inspector has scrolled.
TestCase {
    id: root
    name: "VectorAppearanceExample"
    when: windowShown
    visible: true
    width: 1120
    height: 640

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("../../examples/vector-authoring/StyleWorkingScene.qml")
    }

    // Each test starts from a freshly loaded scene.
    function init() {
        loader.active = false
        loader.active = true
        tryCompare(loader, "status", Loader.Ready)
    }

    function part(name) {
        const item = findChild(loader.item, name)
        verify(item !== null, name)
        return item
    }

    function test_panels_follow_the_document() {
        const doc = part("styleWorkingDocument")
        const styles = part("styleWorkingStyles")
        const stroke = part("styleWorkingStroke")
        const fill = part("styleWorkingFill")
        compare(doc.objectCount, 2)
        compare(styles.selectionStyle.count, 2)
        compare(stroke.style.alignment, "center")
        compare(stroke.style.width, 4)
        // The fields show the selection, and each colour's text matches its swatch.
        const width = findChild(stroke, "strokeWidth")
        const strokeColor = findChild(stroke, "strokeColor")
        const fillColor = findChild(fill, "fillSolidColor")
        compare(width.value, 4)
        compare(String(strokeColor.color), "#e6a55a")
        compare(findChild(strokeColor, "colorHex").text, "#e6a55a")
        compare(String(fillColor.color), "#468fb8")
        compare(findChild(fillColor, "colorHex").text, "#468fb8")
        compare(part("styleWorkingOpacity").value, 1)

        stroke.styleChangeRequested(Object.assign({}, stroke.style, { alignment: "outside" }))
        compare(styles.lastError, "")
        compare(stroke.style.alignment, "outside")
        part("styleWorkingUndo").clicked()
        compare(stroke.style.alignment, "center")

        fill.fillChangeRequested(Object.assign({}, fill.fill, { kind: "pattern" }))
        compare(fill.fill.kind, "pattern")
        compare(fill.fill.settings.pattern.patternId, "hatch")
        part("styleWorkingUndo").clicked()
        compare(fill.fill.kind, "solid")

        part("styleWorkingOpacity").valueModified(0.5)
        compare(styles.selectionStyle.opacity, 0.5)
        compare(part("styleWorkingOpacity").value, 0.5)
        part("styleWorkingBlend").activated(1)
        compare(styles.selectionStyle.blend, "multiply")

        // A user edit only reports the new width (NumberField never writes
        // its own value, D-198); the field follows the owner, undo included.
        width.valueModified(6)
        compare(width.value, 6)
        compare(styles.selectionStyle.strokeWidth, 6)
        part("styleWorkingUndo").clicked()
        compare(styles.selectionStyle.strokeWidth, 4)
        compare(width.value, 4)
    }

    function test_empty_selection_is_explicit() {
        const scene = loader.item
        const stroke = part("styleWorkingStroke")
        const fill = part("styleWorkingFill")
        compare(part("styleWorkingSelection").text, "2 shape(s) selected")
        scene.selectedIds = []
        compare(part("styleWorkingStyles").selectionStyle.count, 0)
        verify(!stroke.enabled)
        verify(!fill.enabled)
        compare(part("styleWorkingSelection").text, "No shape selected. Select shapes to edit their style.")
        const strokeColor = findChild(stroke, "strokeColor")
        compare(findChild(strokeColor, "colorHex").text, String(strokeColor.color))
    }

    function test_appearance_stack_follows_the_model() {
        const scene = loader.item
        const model = part("styleWorkingAppearance")
        const stack = part("styleWorkingAppearanceStack")
        compare(model.effects.length, 0)
        part("styleWorkingAddShadow").clicked()
        part("styleWorkingAddBlur").clicked()
        compare(model.effects.length, 2)
        compare(stack.effects.length, 2)
        compare(model.effects[0].id, "drop-shadow-1")
        compare(model.effects[1].id, "gaussian-blur-1")

        stack.reorderRequested("gaussian-blur-1", 0)
        compare(stack.effects[0].id, "gaussian-blur-1")
        stack.enabledChangeRequested("drop-shadow-1", false)
        compare(model.effects[1].enabled, false)
        part("styleWorkingUndo").clicked()
        compare(model.effects[1].enabled, true)
        part("styleWorkingUndo").clicked()
        compare(model.effects[0].id, "drop-shadow-1")

        stack.editRequested("drop-shadow-1")
        compare(scene.editingEffect, "drop-shadow-1")
        const amount = part("styleWorkingEffectAmount")
        compare(amount.value, 4)
        amount.valueModified(8)
        compare(model.effect("drop-shadow-1").parameters.blur, 8)
        compare(amount.value, 8)
    }

    function test_path_buttons_combine_the_selection() {
        const scene = loader.item
        const doc = part("styleWorkingDocument")
        compare(doc.objectCount, 2)
        part("styleWorkingUnite").clicked()
        compare(doc.objectCount, 1)
        compare(scene.selectedIds.length, 1)
        compare(part("styleWorkingStyles").selectionStyle.count, 1)
        part("styleWorkingUndo").clicked()
        compare(doc.objectCount, 2)
    }
}
