// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ListRowAccessibility"
    when: windowShown
    visible: true
    width: 360
    height: 120

    Component {
        id: rowComponent
        Tk.ListRow { width: 260; text: "Layers"; secondaryText: "Three items" }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_keyboard_accessible_press_and_owner_selection() {
        const row = createTemporaryObject(rowComponent, root)
        verify(row !== null)
        const spy = createTemporaryObject(spyComponent, root,
                                          { target: row, signalName: "clicked" })
        compare(row.Accessible.role, Accessible.ListItem)
        compare(row.Accessible.name, "Layers")
        compare(row.Accessible.description, "Three items")
        verify(row.Accessible.selectable)
        row.selected = true
        verify(row.Accessible.selected)
        compare(spy.count, 0)

        row.forceActiveFocus()
        verify(row.activeFocus)
        verify(row.Accessible.focused)
        const ring = findChild(row, "rowFocusRing")
        verify(ring !== null && ring.visible)
        keyClick(Qt.Key_Space)
        compare(spy.count, 1)
        row.Accessible.pressAction()
        compare(spy.count, 2)

        row.enabled = false
        row.Accessible.pressAction()
        keyClick(Qt.Key_Space)
        compare(spy.count, 2)
    }

    function test_tooltip_names_icon_only_row() {
        const row = createTemporaryObject(rowComponent, root,
                                          { text: "", secondaryText: "", tooltip: "Assets" })
        verify(row !== null)
        compare(row.Accessible.name, "Assets")
    }
}
