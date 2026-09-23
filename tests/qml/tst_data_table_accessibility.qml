// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "DataTableAccessibility"
    when: windowShown
    visible: true
    width: 480
    height: 240

    Component {
        id: tableComponent
        Tk.DataTable {
            width: 360
            height: 120
            tooltip: "Processes"
            model: [{ pid: 42, name: "worker" }, { pid: 43, name: "render" }]
            columns: [
                Tk.TableColumn { key: "pid"; title: "PID" },
                Tk.TableColumn { key: "name"; title: "Name"; flex: 1 }
            ]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_table_and_selected_row_have_accessible_identity() {
        const table = createTemporaryObject(tableComponent, root)
        verify(table !== null)
        compare(table.Accessible.role, Accessible.Table)
        compare(table.Accessible.name, "Processes")
        const row = findChild(table, "tableRow")
        verify(row !== null)
        compare(row.Accessible.role, Accessible.Row)
        compare(row.Accessible.name, "PID: 42, Name: worker")
        verify(row.Accessible.selectable)
        compare(row.Accessible.selected, false)

        table.currentIndex = 1
        compare(row.Accessible.selected, false)
    }

    function test_return_and_accessible_press_activate_valid_rows_only() {
        const table = createTemporaryObject(tableComponent, root, { y: 130 })
        verify(table !== null)
        const spy = createTemporaryObject(spyComponent, root,
                                          { target: table, signalName: "activated" })
        table.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(spy.count, 0)
        keyClick(Qt.Key_Down)
        compare(table.currentIndex, 0)
        keyClick(Qt.Key_Down)
        compare(table.currentIndex, 1)
        keyClick(Qt.Key_Up)
        compare(table.currentIndex, 0)
        compare(spy.count, 0)

        const row = findChild(table, "tableRow")
        verify(row !== null)
        row.Accessible.pressAction()
        compare(table.currentIndex, 0)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 0)

        table.currentIndex = 1
        compare(spy.count, 1)
        keyClick(Qt.Key_Return)
        compare(spy.count, 2)
        compare(spy.signalArguments[1][0], 1)
    }
}
