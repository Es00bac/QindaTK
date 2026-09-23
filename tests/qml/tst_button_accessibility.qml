// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ButtonAccessibility"
    when: windowShown
    visible: true
    width: 420
    height: 180

    Component {
        id: sceneComponent
        FocusScope {
            width: 400
            height: 120
            Tk.Button {
                objectName: "saveButton"
                text: "Save"
                tooltip: "Save document"
                checkable: true
                x: 0
            }
            Tk.IconButton {
                objectName: "closeButton"
                iconName: "x"
                tooltip: "Close document"
                checkable: true
                x: 100
            }
            Tk.Chip {
                objectName: "layersChip"
                text: "Layers"
                tooltip: "Toggle layers"
                checkable: true
                x: 140
            }
            Tk.Button { objectName: "beforeActions"; text: "Before"; y: 60 }
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function find(item, name) {
        const result = findChild(item, name)
        verify(result !== null, "Missing " + name)
        return result
    }

    function spy(item, signalName) {
        return createTemporaryObject(spyComponent, root,
                                     { target: item, signalName: signalName })
    }

    function test_namesRolesAndTabOrder() {
        const scene = createTemporaryObject(sceneComponent, root)
        const save = find(scene, "saveButton")
        const close = find(scene, "closeButton")
        const layers = find(scene, "layersChip")
        const before = find(scene, "beforeActions")

        compare(save.Accessible.role, Accessible.Button)
        compare(save.Accessible.name, "Save")
        compare(save.Accessible.description, "Save document")
        verify(save.Accessible.checkable)
        compare(close.Accessible.role, Accessible.Button)
        compare(close.Accessible.name, "Close document")
        verify(close.Accessible.checkable)
        compare(layers.Accessible.role, Accessible.CheckBox)
        compare(layers.Accessible.name, "Layers")
        compare(layers.Accessible.description, "Toggle layers")
        verify(layers.Accessible.checkable)
        verify(save.activeFocusOnTab)
        verify(close.activeFocusOnTab)
        verify(layers.activeFocusOnTab)

        before.forceActiveFocus()
        keyClick(Qt.Key_Tab)
        verify(save.activeFocus)
        keyClick(Qt.Key_Tab)
        verify(close.activeFocus)
        keyClick(Qt.Key_Tab)
        verify(layers.activeFocus)
    }

    function test_keyboardAndAccessibleActionsAreSingleUserOperations() {
        const scene = createTemporaryObject(sceneComponent, root)
        const save = find(scene, "saveButton")
        const close = find(scene, "closeButton")
        const layers = find(scene, "layersChip")
        const saveClicked = spy(save, "clicked")
        const saveToggled = spy(save, "toggled")
        const closeClicked = spy(close, "clicked")
        const closeToggled = spy(close, "toggled")
        const layersClicked = spy(layers, "clicked")
        const layersToggled = spy(layers, "toggled")

        save.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Space)
        verify(save.checked)
        verify(save.Accessible.checked)
        compare(saveClicked.count, 1)
        compare(saveToggled.count, 1)
        save.Accessible.pressAction()
        compare(saveClicked.count, 2)
        compare(saveToggled.count, 2)
        verify(!save.checked)

        close.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Space)
        verify(close.checked)
        verify(close.Accessible.checked)
        compare(closeClicked.count, 1)
        compare(closeToggled.count, 1)
        close.Accessible.pressAction()
        compare(closeClicked.count, 2)
        compare(closeToggled.count, 2)
        verify(!close.checked)

        layers.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Space)
        verify(layers.active)
        verify(layers.Accessible.checked)
        compare(layersClicked.count, 1)
        compare(layersToggled.count, 1)
        layers.Accessible.pressAction()
        verify(!layers.active)
        compare(layersClicked.count, 2)
        compare(layersToggled.count, 2)
    }

    function test_ownerCheckedAssignmentsStaySilentAndDisabledActionsAreRejected() {
        const scene = createTemporaryObject(sceneComponent, root)
        const save = find(scene, "saveButton")
        const close = find(scene, "closeButton")
        const layers = find(scene, "layersChip")
        const saveClicked = spy(save, "clicked")
        const saveToggled = spy(save, "toggled")
        const layersClicked = spy(layers, "clicked")
        const layersToggled = spy(layers, "toggled")
        const closeClicked = spy(close, "clicked")
        const closeToggled = spy(close, "toggled")

        save.checked = true
        close.checked = true
        layers.active = true
        verify(save.checked)
        verify(close.checked)
        verify(layers.checked)
        verify(save.Accessible.checked)
        verify(close.Accessible.checked)
        verify(layers.Accessible.checked)
        compare(saveClicked.count, 0)
        compare(saveToggled.count, 0)
        compare(closeClicked.count, 0)
        compare(closeToggled.count, 0)
        compare(layersClicked.count, 0)
        compare(layersToggled.count, 0)

        const items = [save, close, layers]
        const clickedSpies = [saveClicked, closeClicked, layersClicked]
        for (let i = 0; i < items.length; ++i) {
            const item = items[i]
            item.forceActiveFocus(Qt.TabFocusReason)
            item.enabled = false
            verify(!item.Accessible.focusable)
            keyClick(Qt.Key_Space)
            item.Accessible.pressAction()
            mouseClick(item)
            compare(clickedSpies[i].count, 0)
        }
        verify(save.checked)
        verify(close.checked)
        verify(layers.active)
    }
}
