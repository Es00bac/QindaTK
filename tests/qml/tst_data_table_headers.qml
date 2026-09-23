// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "DataTableHeaders"
    when: windowShown
    visible: true
    width: 480
    height: 180

    Component {
        id: tableComponent
        Tk.DataTable {
            width: 360
            height: 120
            model: [{ pid: 42, name: "worker" }]
            columns: [
                Tk.TableColumn { key: "pid"; title: "PID"; width: 100 },
                Tk.TableColumn { key: "name"; title: "Name"; flex: 1 }
            ]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_header_keyboard_and_accessible_sort_resize() {
        const table = createTemporaryObject(tableComponent, root)
        verify(table !== null)
        const sortSpy = createTemporaryObject(spyComponent, root,
                                              { target: table, signalName: "sortRequested" })
        const resizeSpy = createTemporaryObject(spyComponent, root,
                                                { target: table, signalName: "columnResized" })
        const header = findChild(table, "headerCell_pid")
        verify(header !== null)
        compare(header.Accessible.role, Accessible.ColumnHeader)
        compare(header.Accessible.name, "PID")
        compare(header.Accessible.description, "Not sorted")
        header.forceActiveFocus()
        verify(header.Accessible.focused)
        verify(findChild(table, "headerFocusRing_pid").visible)
        keyClick(Qt.Key_Space)
        compare(sortSpy.count, 1)
        compare(sortSpy.signalArguments[0][0], "pid")
        header.Accessible.pressAction()
        compare(sortSpy.count, 2)
        compare(table.sortKey, "")

        const original = table.widthOf(table.columns[0])
        keyClick(Qt.Key_Right)
        compare(table.widthOf(table.columns[0]), original + Tk.Theme.space.md)
        compare(resizeSpy.count, 1)
        header.Accessible.decreaseAction()
        compare(table.widthOf(table.columns[0]), original)
        compare(resizeSpy.count, 2)
        table.columnWidths = ({ pid: 120 })
        compare(resizeSpy.count, 2)

        table.enabled = false
        header.Accessible.pressAction()
        header.Accessible.increaseAction()
        table.toggleSort(table.columns[0])
        compare(sortSpy.count, 2)
        compare(resizeSpy.count, 2)
        compare(table.widthOf(table.columns[0]), 120)
    }
}
