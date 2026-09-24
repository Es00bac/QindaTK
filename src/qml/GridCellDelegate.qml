// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/editable-grid.md, docs/controls.md): one visible cell
// of Tk.EditableGrid. It renders the record the grid collected from its model
// — value, optional source `style` and merged-area span — and stores no cell
// state. objectName gridCell_<rowId>_<columnId> is part of the grid contract.
// Style keys: horizontalAlignment, bold, italic, underline, strikeout,
// textColor/fillColor (document colors), dataBar (0-1) + dataBarColor,
// icon + iconTone (positive|neutral|negative -> theme roles), error.
Item {
    id: cell

    required property var modelData
    required property int index
    property Item grid: null
    property bool editorActive: false
    readonly property var cellStyle: modelData.style || ({})
    readonly property string displayText: modelData.value === undefined || modelData.value === null
                                          ? "" : String(modelData.value)
    readonly property bool merged: (modelData.rowSpan || 1) > 1 || (modelData.columnSpan || 1) > 1
    readonly property bool hasIcon: String(cellStyle.icon || "").length > 0

    objectName: "gridCell_" + modelData.rowId + "_" + modelData.columnId
    x: modelData.x
    y: modelData.y
    width: modelData.width
    height: modelData.height
    z: modelData.frozen ? 2 : 1
    Accessible.role: Accessible.Cell
    Accessible.name: (merged ? qsTr("Rows %1–%2, columns %3–%4: %5")
                               .arg(modelData.row + 1).arg(modelData.row + (modelData.rowSpan || 1))
                               .arg(modelData.column + 1).arg(modelData.column + (modelData.columnSpan || 1))
                             : qsTr("Row %1, column %2: %3").arg(modelData.row + 1).arg(modelData.column + 1))
                     .arg(displayText)
    Accessible.description: qsTr("Select this cell")
    Accessible.focusable: true
    Accessible.focused: !!grid && grid.activeFocus && grid.activeRowId === modelData.rowId
                        && grid.activeColumnId === modelData.columnId
    Accessible.selected: !!grid && grid.cellSelected(modelData.row, modelData.column)
    Accessible.onPressAction: {
        if (!cell.grid) return
        cell.grid.setCurrent(cell.modelData.row, cell.modelData.column, false)
        cell.grid.forceActiveFocus()
    }

    Rectangle {
        objectName: "gridCellBackground"
        anchors.fill: parent
        // Document fill colours are cell data; theme roles style everything else.
        color: cell.cellStyle.fillColor ? cell.cellStyle.fillColor
             : cell.modelData.row % 2 === 1 && !!cell.grid && cell.grid.alternatingRows
               ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.divider
    }
    Rectangle {
        objectName: "gridCellDataBar"
        visible: cell.cellStyle.dataBar !== undefined
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Tk.Theme.space.xs
        width: Math.max(0, (parent.width - Tk.Theme.space.xs * 2) * Number(cell.cellStyle.dataBar || 0))
        radius: Tk.Theme.radius.xs
        color: cell.cellStyle.dataBarColor || Tk.Theme.color.accent
        opacity: Tk.Theme.opacity.ghost
    }
    Tk.Icon {
        id: cellIcon
        objectName: "gridCellIcon"
        visible: cell.hasIcon
        anchors.left: parent.left
        anchors.leftMargin: Tk.Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        name: String(cell.cellStyle.icon || "")
        size: Tk.Theme.size.iconSm
        color: cell.cellStyle.iconTone === "positive" ? Tk.Theme.color.success
             : cell.cellStyle.iconTone === "negative" ? Tk.Theme.color.danger : Tk.Theme.color.warning
    }
    Tk.Label {
        objectName: "gridCellText"
        anchors.fill: parent
        anchors.leftMargin: cell.hasIcon ? Tk.Theme.space.xs * 2 + Tk.Theme.size.iconSm : Tk.Theme.space.xs
        anchors.rightMargin: Tk.Theme.space.xs
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: cell.cellStyle.horizontalAlignment === "right" ? Text.AlignRight
                             : cell.cellStyle.horizontalAlignment === "center" ? Text.AlignHCenter
                             : Text.AlignLeft
        text: cell.displayText
        color: cell.cellStyle.textColor ? cell.cellStyle.textColor
             : cell.cellStyle.error === true ? Tk.Theme.color.danger : Tk.Theme.color.text
        font.pixelSize: Tk.Theme.font.body
        font.bold: cell.cellStyle.bold === true
        font.italic: cell.cellStyle.italic === true
        font.underline: cell.cellStyle.underline === true
        font.strikeout: cell.cellStyle.strikeout === true
        elide: cell.cellStyle.horizontalAlignment === "right" ? Text.ElideLeft : Text.ElideRight
        visible: !cell.editorActive
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: function(mouse) {
            if (!cell.grid) return
            cell.grid.setCurrent(cell.modelData.row, cell.modelData.column,
                                 (mouse.modifiers & Qt.ShiftModifier) !== 0)
            cell.grid.forceActiveFocus()
        }
        onDoubleClicked: if (cell.grid) cell.grid.beginCellEdit()
    }
}
