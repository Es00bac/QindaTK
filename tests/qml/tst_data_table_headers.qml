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
    Component {
        id: narrowTableComponent
        Tk.DataTable {
            width: 260
            height: 120
            model: [{ first: "First client", status: "Checked in", owner: "Case manager" }]
            columns: [
                Tk.TableColumn { key: "first"; title: "Client"; flex: 2; minWidth: 160 },
                Tk.TableColumn { key: "status"; title: "Status"; width: 120 },
                Tk.TableColumn { key: "owner"; title: "With"; flex: 1; minWidth: 150 }
            ]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_narrow_table_scrolls_columns_without_squeezing() {
        const table = createTemporaryObject(narrowTableComponent, root)
        verify(table)
        const horizontal = findChild(table, "tableHorizontal")
        verify(horizontal)
        tryVerify(() => horizontal.scrollableX)
        verify(table.gridWidth > table.width)
        for (const column of table.visibleColumns)
            verify(table.resolvedWidth(column, table.gridWidth - Tk.Theme.space.md) >= column.minWidth)
        const header = findChild(table, "tableHeader")
        verify(header)
        tryVerify(() => !!findChild(table, "tableRow"))
        const row = findChild(table, "tableRow")
        horizontal.contentX = 100
        tryCompare(horizontal, "contentX", 100)
        compare(Math.round(header.mapToItem(table, 0, 0).x),
                Math.round(row.mapToItem(table, 0, 0).x))
    }

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
