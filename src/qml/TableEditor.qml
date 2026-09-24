// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Structured cells and merge/selection policy belong to the document model.
// This control reports stable row, column and cell IDs without flattening text.
Item {
    id: control
    required property var table // {id, rows:[{id,label}], columns:[{id,label,width}], cells:[{id,rowId,columnId,rowIndex,columnIndex,rowSpan,columnSpan,text,styleId}]}
    property var selectedCellIds: []
    property string selectedRowId: ""
    property string selectedColumnId: ""
    property bool extendSelection: false
    property string tooltip: qsTr("Edit structured table rows, columns and cells")
    readonly property var selectedCell: cellById(selectedCellIds.length ? selectedCellIds[selectedCellIds.length - 1] : "")
    readonly property bool canMerge: selectedCellIds.length > 1 && table.canMerge !== false
    readonly property bool canSplit: selectedCell !== null && ((selectedCell.rowSpan || 1) > 1 || (selectedCell.columnSpan || 1) > 1)

    signal cellSelectionRequested(string cellId, bool extend)
    signal cellTextChangeRequested(string cellId, string text)
    signal rowInsertRequested(int index)
    signal rowDeleteRequested(string rowId)
    signal columnInsertRequested(int index)
    signal columnDeleteRequested(string columnId)
    signal cellsMergeRequested(var cellIds)
    signal cellSplitRequested(string cellId)
    signal columnWidthChangeRequested(string columnId, real width)
    signal tableStyleChangeRequested(string styleId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function cellById(id) {
        for (let i = 0; i < table.cells.length; ++i) if (table.cells[i].id === id) return table.cells[i]
        return null
    }
    function cellAt(rowIndex, columnIndex) {
        for (let i = 0; i < table.cells.length; ++i) {
            const cell = table.cells[i]
            if (cell.rowIndex === rowIndex && cell.columnIndex === columnIndex) return cell
        }
        return null
    }
    function rowIndex(id) {
        for (let i = 0; i < table.rows.length; ++i) if (table.rows[i].id === id) return i
        return -1
    }
    function columnIndex(id) {
        for (let i = 0; i < table.columns.length; ++i) if (table.columns[i].id === id) return i
        return -1
    }
    function selectCell(cellId) {
        if (!enabled || !cellById(cellId)) return
        cellSelectionRequested(cellId, extendSelection)
    }
    function insertRow() {
        if (enabled && table.rows.length < (table.maxRows || 5000))
            rowInsertRequested(rowIndex(selectedRowId) + 1)
    }
    function insertColumn() {
        if (enabled && table.columns.length < (table.maxColumns || 512))
            columnInsertRequested(columnIndex(selectedColumnId) + 1)
    }
    function mergeSelection() {
        if (enabled && canMerge) cellsMergeRequested(selectedCellIds.slice())
    }

    Tk.Flex {
        id: column
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("TABLE STRUCTURE"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { text: qsTr("%1 × %2").arg(control.table.rows.length).arg(control.table.columns.length); mono: true }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "tableInsertRow"
                text: qsTr("Add row")
                iconName: "list-plus"
                tooltip: qsTr("Insert a row after the selected row")
                enabled: control.table.rows.length < (control.table.maxRows || 5000)
                onClicked: control.insertRow()
            }
            Tk.Button {
                objectName: "tableDeleteRow"
                text: qsTr("Delete row")
                iconName: "list-minus"
                tooltip: qsTr("Delete the selected row")
                enabled: control.table.rows.length > 1 && control.rowIndex(control.selectedRowId) >= 0
                onClicked: control.rowDeleteRequested(control.selectedRowId)
            }
            Tk.Button {
                objectName: "tableInsertColumn"
                text: qsTr("Add column")
                iconName: "columns-3"
                tooltip: qsTr("Insert a column after the selected column")
                enabled: control.table.columns.length < (control.table.maxColumns || 512)
                onClicked: control.insertColumn()
            }
            Tk.Button {
                objectName: "tableDeleteColumn"
                text: qsTr("Delete column")
                iconName: "columns-2"
                tooltip: qsTr("Delete the selected column")
                enabled: control.table.columns.length > 1 && control.columnIndex(control.selectedColumnId) >= 0
                onClicked: control.columnDeleteRequested(control.selectedColumnId)
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.CheckBox {
                objectName: "tableExtendSelection"
                text: qsTr("Extend cell selection")
                checked: control.extendSelection
                tooltip: qsTr("Add or remove clicked cells from the selection")
                onToggled: function(checked) { control.extendSelection = checked }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "tableMergeCells"
                text: qsTr("Merge")
                iconName: "combine"
                tooltip: qsTr("Merge selected adjacent cells")
                enabled: control.canMerge
                onClicked: control.mergeSelection()
            }
            Tk.Button {
                objectName: "tableSplitCell"
                text: qsTr("Split")
                iconName: "split"
                tooltip: qsTr("Split the selected merged cell")
                enabled: control.canSplit
                onClicked: control.cellSplitRequested(control.selectedCell.id)
            }
        }
        Tk.Grid {
            objectName: "tableCellGrid"
            columns: "repeat(" + Math.max(1, control.table.columns.length) + ", minmax(0, 1fr))"
            rows: "repeat(" + Math.max(1, control.table.rows.length) + ", auto)"
            gap: Tk.Theme.size.border
            Repeater {
                model: control.table.cells
                delegate: Tk.Button {
                    required property var modelData
                    objectName: "tableCell_" + modelData.id
                    text: modelData.text || ""
                    variant: control.selectedCellIds.indexOf(modelData.id) >= 0 ? "outline" : "default"
                    tooltip: qsTr("Row %1, column %2: %3").arg(modelData.rowIndex + 1).arg(modelData.columnIndex + 1).arg(modelData.text || qsTr("empty cell"))
                    Accessible.role: Accessible.Cell
                    Accessible.selected: control.selectedCellIds.indexOf(modelData.id) >= 0
                    Tk.Grid.row: modelData.rowIndex
                    Tk.Grid.column: modelData.columnIndex
                    Tk.Grid.rowSpan: modelData.rowSpan || 1
                    Tk.Grid.columnSpan: modelData.columnSpan || 1
                    onClicked: control.selectCell(modelData.id)
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.TextField {
                id: cellText
                objectName: "tableCellText"
                text: control.selectedCell ? control.selectedCell.text || "" : ""
                placeholderText: qsTr("Select a cell to edit its text")
                tooltip: qsTr("Text of the selected table cell")
                enabled: control.selectedCell !== null
                onAccepted: if (control.selectedCell) control.cellTextChangeRequested(control.selectedCell.id, text)
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.NumberField {
                objectName: "tableColumnWidth"
                label: qsTr("W")
                value: {
                    const index = control.columnIndex(control.selectedColumnId)
                    return index >= 0 ? Number(control.table.columns[index].width || 1) : 1
                }
                from: 1
                to: 100000
                stepSize: 1
                decimals: 1
                tooltip: qsTr("Selected column width in document units")
                enabled: control.columnIndex(control.selectedColumnId) >= 0
                onValueModified: function(width) {
                    if (enabled) control.columnWidthChangeRequested(control.selectedColumnId, width)
                }
            }
        }
        Tk.ComboBox {
            objectName: "tableStyle"
            visible: (control.table.styles || []).length > 0
            model: control.table.styles || []
            textRole: "label"
            currentIndex: {
                const styles = control.table.styles || []
                for (let i = 0; i < styles.length; ++i) if (styles[i].id === control.table.styleId) return i
                return -1
            }
            tooltip: qsTr("Table style")
            onActivated: function(index) { control.tableStyleChangeRequested(model[index].id) }
        }
        Tk.Caption {
            objectName: "tableStructureStatus"
            visible: control.table.validationMessage !== undefined && control.table.validationMessage.length > 0
            color: Tk.Theme.color.warning
            text: control.table.validationMessage || ""
            wrapMode: Text.WordWrap
        }
    }
}
