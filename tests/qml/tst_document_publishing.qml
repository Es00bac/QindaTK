// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "DocumentPublishing"
    when: windowShown
    visible: true
    width: 720
    height: 420

    Component {
        id: tableComponent
        Tk.TableEditor {
            width: 620
            table: ({
                id: "table-1", rows: [{ id: "row-a", label: "Row 1" }],
                columns: [{ id: "col-a", label: "A", width: 40 }, { id: "col-b", label: "B", width: 40 }],
                cells: [
                    { id: "cell-a", rowId: "row-a", columnId: "col-a", rowIndex: 0, columnIndex: 0, text: "A", rowSpan: 1, columnSpan: 1 },
                    { id: "cell-b", rowId: "row-a", columnId: "col-b", rowIndex: 0, columnIndex: 1, text: "B", rowSpan: 1, columnSpan: 1 }
                ]
            })
            selectedRowId: "row-a"
            selectedColumnId: "col-a"
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_add_row_emits_insertion_position_without_mutating_snapshot() {
        const editor = createTemporaryObject(tableComponent, this)
        verify(editor !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: editor, signalName: "rowInsertRequested" })
        const insert = findChild(editor, "tableInsertRow")
        verify(insert !== null && insert.enabled)

        insert.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], 1)
        compare(editor.table.rows.length, 1)
    }
}
