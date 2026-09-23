// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TreeRowAccessibility"
    when: windowShown
    visible: true
    width: 400
    height: 160

    Component { id: rowComponent; Tk.TreeRow { text: "Tracks"; expandable: true; width: 280 } }
    Component { id: leafComponent; Tk.TreeRow { text: "Audio bus"; width: 280 } }
    Component { id: editableComponent; Tk.TreeRow { text: "Track"; editable: true; width: 280 } }
    Component { id: spyComponent; SignalSpy {} }

    function test_keyboardAndAccessiblePressShareExpansion() {
        const row = createTemporaryObject(rowComponent, root)
        verify(row !== null)
        const spy = createTemporaryObject(spyComponent, root, { target: row, signalName: "expansionToggled" })

        compare(row.Accessible.role, Accessible.TreeItem)
        compare(row.Accessible.name, "Tracks")
        verify(row.Accessible.focusable)
        compare(row.Accessible.description, "Collapsed")
        row.forceActiveFocus()
        verify(row.activeFocus)
        verify(row.Accessible.focused)
        verify(findChild(row, "treeFocusRing").visible)

        keyClick(Qt.Key_Space)
        verify(row.expanded)
        compare(spy.count, 1)
        compare(row.Accessible.description, "Expanded")
        keyClick(Qt.Key_Return)
        verify(!row.expanded)
        compare(spy.count, 2)
        row.Accessible.pressAction()
        verify(row.expanded)
        compare(spy.count, 3)

        // Public state assignment is programmatic and does not impersonate
        // a user action or emit the interaction signal.
        row.expanded = false
        compare(spy.count, 3)
    }

    function test_disabledBlocksUserActionsButAllowsStateAssignment() {
        const row = createTemporaryObject(rowComponent, root, { y: 50 })
        verify(row !== null)
        const spy = createTemporaryObject(spyComponent, root, { target: row, signalName: "expansionToggled" })
        row.enabled = false
        verify(!row.Accessible.focusable)
        verify(!findChild(row, "treeFocusRing").visible)
        row.forceActiveFocus()
        keyClick(Qt.Key_Space)
        row.Accessible.pressAction()
        mouseClick(findChild(row, "treeChevron"))
        verify(!row.expanded)
        compare(spy.count, 0)

        row.expanded = true
        verify(row.expanded)
        compare(spy.count, 0)
    }

    function test_leafSharesPointerKeyboardAndAccessibleActivation() {
        const row = createTemporaryObject(leafComponent, root)
        verify(row !== null)
        const clicked = createTemporaryObject(spyComponent, root,
            { target: row, signalName: "clicked" })

        compare(row.Accessible.role, Accessible.TreeItem)
        compare(row.Accessible.name, "Audio bus")
        verify(row.Accessible.focusable)
        mouseClick(row)
        compare(clicked.count, 1)

        row.forceActiveFocus()
        verify(row.activeFocus)
        verify(row.Accessible.focused)
        verify(findChild(row, "treeFocusRing").visible)
        keyClick(Qt.Key_Space)
        compare(clicked.count, 2)
        keyClick(Qt.Key_Return)
        compare(clicked.count, 3)
        keyClick(Qt.Key_Enter)
        compare(clicked.count, 4)

        row.Accessible.pressAction()
        compare(clicked.count, 5)

        row.enabled = false
        verify(!row.Accessible.focusable)
        verify(!findChild(row, "treeFocusRing").visible)
        mouseClick(row)
        keyClick(Qt.Key_Space)
        keyClick(Qt.Key_Return)
        row.Accessible.pressAction()
        compare(clicked.count, 5)
    }

    function test_f2StartsRenameAndReturnFromEditorDoesNotRestartIt() {
        const row = createTemporaryObject(editableComponent, root, { y: 100 })
        verify(row !== null)
        const spy = createTemporaryObject(spyComponent, root, { target: row, signalName: "renamed" })
        row.forceActiveFocus()
        keyClick(Qt.Key_F2)
        verify(row.editing)
        const editor = findChild(row, "treeEditor")
        verify(editor !== null)
        editor.text = "Lead"
        keyClick(Qt.Key_Return)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "Lead")
        verify(!row.editing)
    }
}
