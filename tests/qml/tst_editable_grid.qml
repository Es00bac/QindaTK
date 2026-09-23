// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "EditableGrid"
    when: windowShown
    visible: true
    width: 480
    height: 280

    Component {
        id: gridComponent
        Item {
            width: 420
            height: 240
            Tk.EditableGridModel {
                id: data
                Component.onCompleted: {
                    setDimensions(100000, 100)
                    frozenRows = 1
                    frozenColumns = 1
                }
            }
            Tk.EditableGrid {
                id: view
                objectName: "editableGrid"
                anchors.fill: parent
                model: data
                tooltip: "Inventory grid"
                alternatingRows: true
            }
        }
    }

    function test_large_grid_is_bounded_and_headers_follow_model() {
        const host = createTemporaryObject(gridComponent, root)
        verify(host !== null)
        const view = findChild(host, "editableGrid")
        verify(view !== null)
        compare(view.model.rowCount, 100000)
        compare(view.model.columnCount, 100)
        verify(view.visibleCellRecords.length < 100)
        compare(view.model.defaultRowSize, Tk.Theme.size.row)
        compare(view.model.defaultColumnSize, Tk.Theme.size.tableColumn)
        compare(findChild(view, "gridColumnHeaders").orientation, Qt.Horizontal)
        compare(findChild(view, "gridRowHeaders").orientation, Qt.Vertical)
        const firstHeader = findChild(view, "gridColumnHeader_c:1")
        verify(firstHeader !== null)
        compare(firstHeader.width, Tk.Theme.size.tableColumn)
        verify(view.model.requestColumnResize("c:1",
                                             Tk.Theme.size.tableColumn + Tk.Theme.space.lg))
        tryVerify(function() {
            const item = findChild(view, "gridColumnHeader_c:1")
            return item !== null
                    && Math.abs(item.width - Tk.Theme.size.tableColumn - Tk.Theme.space.lg) < 0.1
        })
        verify(view.model.requestColumnMove("c:1", "c:3"))
        tryVerify(function() { return view.model.columnIndex("c:1") === 1 })
        compare(view.model.columnIdAt(1), "c:1")
        tryVerify(function() {
            const item = findChild(view, "gridColumnHeader_c:1")
            return item !== null && Math.abs(item.x - Tk.Theme.size.tableColumn) < 0.1
        })
        compare(view.model.rowIdAt(0), "r:1")
        compare(view.model.columnIdAt(0), "c:2")
        verify(findChild(view, "gridCellViewport") !== null)
        compare(view.Accessible.role, Accessible.Table)
        compare(view.Accessible.name, "Inventory grid")
    }

    function test_keyboard_ranges_paste_and_atomic_rejection() {
        const host = createTemporaryObject(gridComponent, root)
        const view = findChild(host, "editableGrid")
        view.setCurrent(1, 1, false)
        view.forceActiveFocus()
        keyClick(Qt.Key_Right, Qt.ShiftModifier)
        compare(view.activeRow, 1)
        compare(view.activeColumn, 2)
        compare(view.model.ranges.length, 1)
        compare(view.model.ranges[0].firstColumn, 1)
        compare(view.model.ranges[0].lastColumn, 2)

        verify(view.pasteMatrix(1, 1, [["alpha", "beta"], ["gamma", "delta"]]))
        compare(view.model.cellValue("r:2", "c:2"), "alpha")
        compare(view.model.cellValue("r:3", "c:3"), "delta")
        const before = view.model.sparseCellCount
        verify(!view.pasteMatrix(99999, 99, [["edge", "outside"]]))
        compare(view.model.sparseCellCount, before)
        verify(view.model.canUndo)
        verify(view.model.undo())
        compare(view.model.cellValue("r:2", "c:2"), undefined)
    }

    function test_editor_commits_to_original_cell_after_selection_moves() {
        const host = createTemporaryObject(gridComponent, root)
        const view = findChild(host, "editableGrid")
        view.setCurrent(1, 1, false)
        verify(view.beginCellEdit())
        const editor = findChild(view, "gridCellEditor")
        verify(editor !== null)
        editor.text = "draft for B2"
        mouseClick(findChild(view, "gridCell_r:2_c:3"),
                   Tk.Theme.size.tableColumn / 2, Tk.Theme.size.row / 2)
        compare(view.activeColumn, 2)
        compare(view.editingColumnId, "c:2")
        verify(view.commitCellEdit(editor.text))
        compare(view.model.cellValue("r:2", "c:2"), "draft for B2")
        compare(view.model.cellValue("r:2", "c:3"), undefined)
    }
}
