// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#conditionalformateditor): a small cell grid
// drawing Workbook.previewConditionalFormat records ({row, column, display,
// ...presentation}) with Tk.GridCellDelegate — the sheet's own cell
// renderer — so the preview is what the committed rule draws.
Item {
    id: preview

    property var cells: []
    property string tooltip: qsTr("Preview")
    readonly property int firstRow: cells.length > 0 ? cells[0].row : 0
    readonly property int firstColumn: cells.length > 0 ? cells[0].column : 0
    readonly property int columns: {
        let count = 0
        for (let i = 0; i < cells.length && cells[i].row === firstRow; ++i) ++count
        return Math.max(1, count)
    }
    readonly property int rows: Math.ceil(cells.length / columns)
    readonly property var records: cells.map(function(cell) {
        const r = cell.row - preview.firstRow, c = cell.column - preview.firstColumn
        const style = Object.assign({}, cell)
        for (const key of ["row", "column", "display"]) delete style[key]
        return { row: cell.row, column: cell.column, rowId: "p" + cell.row, columnId: "p" + cell.column,
                 value: cell.display, style: style, frozen: false,
                 x: c * Tk.Theme.size.tableColumn, y: r * Tk.Theme.size.row,
                 width: Tk.Theme.size.tableColumn, height: Tk.Theme.size.row }
    })

    objectName: "rulePreview"
    implicitWidth: columns * Tk.Theme.size.tableColumn
    implicitHeight: rows * Tk.Theme.size.row
    clip: true
    Accessible.role: Accessible.Table
    Accessible.name: tooltip

    Repeater {
        model: preview.records
        delegate: Tk.GridCellDelegate {}
    }
}
