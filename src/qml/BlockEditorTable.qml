// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Internal part of Tk.BlockEditor: an editable table block. Cells commit on
// editing finished; at most `visibleRowLimit` rows are instantiated at once.
Tk.Flex {
    id: root
    property var editor: null // the owning Tk.BlockEditor (var: a typed property would make a load cycle)
    property var block: ({})
    property int visibleRowLimit: 50
    readonly property string blockId: block && block.id ? block.id : ""
    readonly property var columns: block && block.data && block.data.columns ? block.data.columns : []
    readonly property var rows: block && block.data && block.data.rows ? block.data.rows : []
    readonly property bool editable: !!editor && editor.capabilities.canEdit && !(block && block.locked)

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.size.border
    Accessible.role: Accessible.Table
    Accessible.name: qsTr("Table with %1 columns and %2 rows").arg(columns.length).arg(rows.length)

    Tk.Flex {
        objectName: "blockTableHeader_" + root.blockId
        direction: Tk.Flex.Row
        gap: Tk.Theme.size.border
        Repeater {
            model: root.columns
            delegate: Tk.Box {
                id: headerCell
                required property var modelData
                required property int index
                color: Tk.Theme.color.panelAlt
                padding: Tk.Theme.space.xs
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    Tk.Label { text: String(headerCell.modelData); font.bold: true; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                    Tk.IconButton {
                        objectName: "blockTableRemoveColumn_" + root.blockId + "_" + headerCell.index
                        small: true
                        iconName: "x"
                        tooltip: qsTr("Remove column %1").arg(String(headerCell.modelData))
                        visible: root.editable && root.columns.length > 1
                        onClicked: root.editor.tableEdit(root.block, "remove-column", -1, headerCell.index, "")
                    }
                }
            }
        }
    }
    Repeater {
        model: Math.min(root.rows.length, root.visibleRowLimit)
        delegate: Tk.Flex {
            id: tableRow
            required property int index
            readonly property var cells: root.rows[index] || []
            direction: Tk.Flex.Row
            gap: Tk.Theme.size.border
            Repeater {
                model: root.columns.length
                delegate: Tk.TextField {
                    id: cell
                    required property int index
                    readonly property string committed: String(tableRow.cells[index] === undefined ? "" : tableRow.cells[index])
                    objectName: "blockTableCell_" + root.blockId + "_" + tableRow.index + "_" + index
                    text: committed
                    small: true
                    readOnly: !root.editable
                    tooltip: qsTr("%1, row %2").arg(String(root.columns[index])).arg(tableRow.index + 1)
                    onEditingFinished: if (text !== committed) root.editor.tableEdit(root.block, "cell", tableRow.index, index, text)
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
            }
            Tk.IconButton {
                objectName: "blockTableRemoveRow_" + root.blockId + "_" + tableRow.index
                small: true
                iconName: "minus"
                tooltip: qsTr("Remove row %1").arg(tableRow.index + 1)
                visible: root.editable
                onClicked: root.editor.tableEdit(root.block, "remove-row", tableRow.index, -1, "")
            }
        }
    }
    Tk.Caption {
        visible: root.rows.length > root.visibleRowLimit
        text: qsTr("%1 more rows are not shown").arg(root.rows.length - root.visibleRowLimit)
        color: Tk.Theme.color.textMuted
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        Tk.Button {
            objectName: "blockTableAddRow_" + root.blockId
            text: qsTr("Add row")
            iconName: "plus"
            tooltip: qsTr("Append a table row")
            visible: root.editable
            onClicked: root.editor.tableEdit(root.block, "add-row", -1, -1, "")
        }
        Tk.Button {
            objectName: "blockTableAddColumn_" + root.blockId
            text: qsTr("Add column")
            iconName: "columns-2"
            tooltip: qsTr("Append a table column")
            visible: root.editable
            onClicked: root.editor.tableEdit(root.block, "add-column", -1, -1, "")
        }
        Tk.Button {
            objectName: "blockTableEdit_" + root.blockId
            text: qsTr("Edit table")
            iconName: "table"
            tooltip: qsTr("Open this table in the host table editor")
            visible: !!root.editor && !root.editor.notebook
            onClicked: root.editor.blockActionRequested(root.blockId, "edit-table", root.editor.requestId("table", root.blockId))
        }
    }
}
