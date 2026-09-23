// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Read-only, bounded rendering of QueryController's result snapshot. Every
// displayed aggregate can request drill-through using source-row provenance.
Item {
    id: pivot
    property var controller: null
    property int maxRows: 50
    property int maxColumns: 24
    property int rowPage: 0
    property int columnPage: 0
    property var collapsedRows: []
    property real rowHeaderWidth: Tk.Theme.size.panelMinWidth * 0.55
    property real columnWidth: Tk.Theme.size.fieldWidth * 0.8
    property string tooltip: qsTr("Pivot results; activate a value to inspect its source rows")
    readonly property var rowParents: controller && controller.rowParents ? controller.rowParents : ({})
    readonly property var displayedRowIds: collectRows()
    readonly property int totalRows: displayedRowIds.length
    readonly property int totalColumns: controller && controller.resultModel
                                       ? controller.resultModel.columnCount : 0
    readonly property int rowPageSize: Math.min(50, Math.max(1, maxRows))
    readonly property int columnPageSize: Math.min(24, Math.max(1, maxColumns))
    readonly property int rowPageCount: Math.max(1, Math.ceil(totalRows / rowPageSize))
    readonly property int columnPageCount: Math.max(1, Math.ceil(totalColumns / columnPageSize))
    readonly property int rowOffset: Math.min(rowPage, rowPageCount - 1) * rowPageSize
    readonly property int columnOffset: Math.min(columnPage, columnPageCount - 1) * columnPageSize
    readonly property int visibleRows: Math.max(0, Math.min(rowPageSize, totalRows - rowOffset))
    readonly property int visibleColumns: Math.max(0, Math.min(columnPageSize, totalColumns - columnOffset))
    readonly property bool truncated: totalRows > visibleRows || totalColumns > visibleColumns
    readonly property var rowLabels: controller ? controller.rowLabels : ({})
    readonly property var columnLabels: controller ? controller.columnLabels : ({})
    signal drillThrough(string rowId, string columnId, var sourceRows)

    onTotalRowsChanged: rowPage = Math.min(rowPage, rowPageCount - 1)
    onTotalColumnsChanged: columnPage = Math.min(columnPage, columnPageCount - 1)
    onMaxRowsChanged: rowPage = Math.min(rowPage, rowPageCount - 1)
    onMaxColumnsChanged: columnPage = Math.min(columnPage, columnPageCount - 1)
    Connections {
        target: pivot.controller
        function onResultChanged() {
            pivot.rowPage = 0
            pivot.columnPage = 0
            pivot.collapsedRows = []
        }
    }

    function collectRows() {
        const output = []
        if (!controller || !controller.resultModel) return output
        for (let i = 0; i < controller.resultModel.rowCount; ++i) {
            const id = controller.resultModel.rowIdAt(i)
            let parent = rowParents[id]
            let hidden = false
            for (let depth = 0; parent && depth < 16; ++depth) {
                if (collapsedRows.indexOf(parent) >= 0) { hidden = true; break }
                parent = rowParents[parent]
            }
            if (!hidden) output.push(id)
        }
        return output
    }
    function hasChildren(id) {
        for (const child in rowParents) if (rowParents[child] === id) return true
        return false
    }
    function rowDepth(id) {
        let depth = 0
        let parent = rowParents[id]
        while (parent && depth < 16) { ++depth; parent = rowParents[parent] }
        return depth
    }
    function toggleRow(id) {
        if (!hasChildren(id)) return
        const next = collapsedRows.slice()
        const index = next.indexOf(id)
        if (index >= 0) next.splice(index, 1)
        else next.push(id)
        collapsedRows = next
        rowPage = Math.min(rowPage, rowPageCount - 1)
    }

    function rowId(index) {
        if (!controller || !controller.resultModel || index < 0 || index >= visibleRows) return ""
        const value = displayedRowIds[index + rowOffset]
        return value === undefined || value === null ? "" : String(value)
    }
    function columnId(index) {
        if (!controller || !controller.resultModel || index < 0 || index >= visibleColumns) return ""
        const value = controller.resultModel.columnIdAt(index + columnOffset)
        return value === undefined || value === null ? "" : String(value)
    }
    function labelFor(labels, id, fallback) {
        const v = labels ? labels[id] : undefined
        return v === undefined || v === null || String(v).length === 0 ? fallback : String(v)
    }
    function valueText(value) {
        if (value === undefined || value === null) return "—"
        if (typeof value === "object") {
            if (value.type === "null") return "—"
            if (value.type === "error") return value.message || qsTr("Error")
            if (value.value !== undefined) return String(value.value)
        }
        return String(value)
    }
    function activateCell(row, column) {
        if (!controller || row < 0 || column < 0 || row >= visibleRows || column >= visibleColumns) return
        const r = rowId(row)
        const c = columnId(column)
        drillThrough(r, c, controller.sourceRows(r, c))
    }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    Accessible.description: truncated
                                ? qsTr("Rows %1–%2 of %3; columns %4–%5 of %6")
                                  .arg(totalRows === 0 ? 0 : rowOffset + 1)
                                  .arg(rowOffset + visibleRows).arg(totalRows)
                                  .arg(totalColumns === 0 ? 0 : columnOffset + 1)
                                  .arg(columnOffset + visibleColumns).arg(totalColumns)
                                : qsTr("%1 rows by %2 columns").arg(totalRows).arg(totalColumns)
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled
    Tk.ToolTip { text: pivot.tooltip; visible: text.length > 0 && hover.hovered }
    HoverHandler { id: hover }

    Tk.Flex {
        id: frame
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Box {
            objectName: "pivotStatus"
            width: frame.width
            implicitHeight: Tk.Theme.size.row
            color: Tk.Theme.color.surface
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            paddingLeft: Tk.Theme.space.sm
            paddingRight: Tk.Theme.space.sm
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Label {
                    text: pivot.controller ? pivot.controller.status : qsTr("No query")
                    accent: true
                }
            Tk.Label {
                visible: pivot.truncated
                text: qsTr("Rows %1–%2 of %3 · Columns %4–%5 of %6")
                      .arg(pivot.rowOffset + 1).arg(pivot.rowOffset + pivot.visibleRows)
                      .arg(pivot.totalRows).arg(pivot.columnOffset + 1)
                      .arg(pivot.columnOffset + pivot.visibleColumns).arg(pivot.totalColumns)
                    muted: true
                }
                Tk.Spacer { visible: true }
                Tk.Label {
                    visible: pivot.controller && pivot.controller.diagnostic.length > 0
                    text: pivot.controller ? pivot.controller.diagnostic : ""
                    color: Tk.Theme.color.danger
                }
            }
        }
        Tk.Flex {
            objectName: "pivotPaging"
            visible: pivot.truncated
            width: frame.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            paddingLeft: Tk.Theme.space.sm
            paddingRight: Tk.Theme.space.sm
            paddingTop: Tk.Theme.space.xs
            paddingBottom: Tk.Theme.space.xs
            Tk.Label { text: qsTr("Rows"); muted: true }
            Tk.Button {
                objectName: "pivotRowsPrevious"
                text: qsTr("Previous")
                small: true
                enabled: pivot.rowPage > 0
                tooltip: qsTr("Show previous pivot rows")
                onClicked: pivot.rowPage = Math.max(0, pivot.rowPage - 1)
            }
            Tk.Label {
                objectName: "pivotRowsPage"
                text: qsTr("%1 / %2").arg(pivot.rowPage + 1).arg(pivot.rowPageCount)
                muted: true
            }
            Tk.Button {
                objectName: "pivotRowsNext"
                text: qsTr("Next")
                small: true
                enabled: pivot.rowPage + 1 < pivot.rowPageCount
                tooltip: qsTr("Show next pivot rows")
                onClicked: pivot.rowPage = Math.min(pivot.rowPageCount - 1, pivot.rowPage + 1)
            }
            Tk.Divider { vertical: true; inset: Tk.Theme.space.xs }
            Tk.Label { text: qsTr("Columns"); muted: true }
            Tk.Button {
                objectName: "pivotColumnsPrevious"
                text: qsTr("Previous")
                small: true
                enabled: pivot.columnPage > 0
                tooltip: qsTr("Show previous pivot columns")
                onClicked: pivot.columnPage = Math.max(0, pivot.columnPage - 1)
            }
            Tk.Label {
                objectName: "pivotColumnsPage"
                text: qsTr("%1 / %2").arg(pivot.columnPage + 1).arg(pivot.columnPageCount)
                muted: true
            }
            Tk.Button {
                objectName: "pivotColumnsNext"
                text: qsTr("Next")
                small: true
                enabled: pivot.columnPage + 1 < pivot.columnPageCount
                tooltip: qsTr("Show next pivot columns")
                onClicked: pivot.columnPage = Math.min(pivot.columnPageCount - 1, pivot.columnPage + 1)
            }
        }
        Tk.Scroll {
            objectName: "pivotViewport"
            Tk.Flex.grow: 1
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Auto
            accessibleName: pivot.tooltip
            Item {
                id: canvas
                readonly property real contentWidth: pivot.rowHeaderWidth
                    + pivot.visibleColumns * (pivot.columnWidth + Tk.Theme.size.border)
                readonly property real contentHeight: (pivot.visibleRows + 1)
                    * (Tk.Theme.size.header + Tk.Theme.size.border)
                implicitWidth: Math.max(pivot.width, contentWidth)
                implicitHeight: contentHeight
                width: implicitWidth
                height: implicitHeight
                Tk.Flex {
                    id: rows
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.size.border
                    Tk.Flex.shrink: 0
                    Item {
                        id: headerItem
                        objectName: "pivotColumnHeaders"
                        width: canvas.contentWidth
                        height: Tk.Theme.size.header
                        implicitWidth: canvas.contentWidth
                        implicitHeight: Tk.Theme.size.header
                        Tk.Flex.shrink: 0
                        Tk.Flex {
                            anchors.fill: parent
                            direction: Tk.Flex.Row
                            gap: Tk.Theme.size.border
                            Tk.Box {
                                objectName: "pivotCornerHeader"
                                width: pivot.rowHeaderWidth
                                height: Tk.Theme.size.header
                                implicitWidth: pivot.rowHeaderWidth
                                implicitHeight: Tk.Theme.size.header
                                color: Tk.Theme.color.headerBg
                                paddingLeft: Tk.Theme.space.sm
                                Tk.Label { text: qsTr("Rows / values"); muted: true }
                            }
                            Repeater {
                                model: pivot.visibleColumns
                                delegate: Item {
                                    id: headerCell
                                    required property int index
                                    readonly property string columnKey: pivot.columnId(index)
                                    objectName: "pivotColumn_" + (pivot.columnOffset + index)
                                    width: pivot.columnWidth
                                    height: Tk.Theme.size.header
                                    implicitWidth: pivot.columnWidth
                                    implicitHeight: Tk.Theme.size.header
                                    Tk.Flex.shrink: 0
                                    Tk.Box {
                                        anchors.fill: parent
                                        color: Tk.Theme.color.headerBg
                                        paddingLeft: Tk.Theme.space.sm
                                        paddingRight: Tk.Theme.space.sm
                                        Tk.Label {
                                            anchors.fill: parent
                                            text: pivot.labelFor(pivot.columnLabels, headerCell.columnKey,
                                                                 headerCell.columnKey)
                                            elide: Text.ElideRight
                                            Accessible.role: Accessible.ColumnHeader
                                            Accessible.name: text
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Repeater {
                        model: pivot.visibleRows
                        delegate: Item {
                            id: rowItem
                            required property int index
                            readonly property string rowKey: pivot.rowId(index)
                            objectName: "pivotRow_" + rowKey
                            width: canvas.contentWidth
                            height: Tk.Theme.size.header
                            implicitWidth: canvas.contentWidth
                            implicitHeight: Tk.Theme.size.header
                            Tk.Flex.shrink: 0
                            Tk.Flex {
                                anchors.fill: parent
                                direction: Tk.Flex.Row
                                gap: Tk.Theme.size.border
                                Tk.Box {
                                    objectName: "pivotRowHeader_" + rowItem.rowKey
                                    width: pivot.rowHeaderWidth
                                    height: Tk.Theme.size.header
                                    implicitWidth: pivot.rowHeaderWidth
                                    implicitHeight: Tk.Theme.size.header
                                    color: Tk.Theme.color.headerBg
                                    paddingLeft: Tk.Theme.space.sm
                                    Tk.Flex {
                                        anchors.fill: parent
                                        direction: Tk.Flex.Row
                                        align: Tk.Flex.Center
                                        gap: Tk.Theme.space.xs
                                        paddingLeft: Tk.Theme.space.sm
                                                     + pivot.rowDepth(rowItem.rowKey) * Tk.Theme.space.sm
                                        Tk.Button {
                                            objectName: "pivotExpand_" + rowItem.rowKey
                                            visible: pivot.hasChildren(rowItem.rowKey)
                                            text: pivot.collapsedRows.indexOf(rowItem.rowKey) >= 0 ? "+" : "−"
                                            small: true
                                            tooltip: pivot.collapsedRows.indexOf(rowItem.rowKey) >= 0
                                                     ? qsTr("Expand dimension") : qsTr("Collapse dimension")
                                            Accessible.name: tooltip
                                            onClicked: pivot.toggleRow(rowItem.rowKey)
                                        }
                                        Tk.Label {
                                            text: pivot.labelFor(pivot.rowLabels, rowItem.rowKey, rowItem.rowKey)
                                            elide: Text.ElideRight
                                            Tk.Flex.grow: 1
                                            Accessible.role: Accessible.RowHeader
                                            Accessible.name: text
                                        }
                                    }
                                }
                                Repeater {
                                    model: pivot.visibleColumns
                                    delegate: T.AbstractButton {
                                        id: cell
                                        required property int index
                                        readonly property string rowKey: rowItem.rowKey
                                        readonly property string columnKey: pivot.columnId(index)
                                        readonly property var sourceIds: pivot.controller
                                            ? pivot.controller.sourceRows(rowKey, columnKey) : []
                                        objectName: "pivotCell_" + (pivot.rowOffset + rowItem.index)
                                            + "_" + (pivot.columnOffset + index)
                                        width: pivot.columnWidth
                                        height: Tk.Theme.size.header
                                        implicitWidth: pivot.columnWidth
                                        implicitHeight: Tk.Theme.size.header
                                        Tk.Flex.shrink: 0
                                        text: pivot.valueText(pivot.controller
                                            ? pivot.controller.cellValue(rowKey, columnKey) : null)
                                        hoverEnabled: true
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.role: Accessible.Cell
                                        Accessible.name: pivot.labelFor(pivot.rowLabels, rowKey, rowKey)
                                            + ", " + pivot.labelFor(pivot.columnLabels, columnKey, columnKey)
                                            + ": " + text
                                        Accessible.description: qsTr("%1 source rows; activate to inspect")
                                            .arg(sourceIds.length)
                                        Accessible.focusable: enabled
                                        Accessible.onPressAction: pivot.activateCell(rowItem.index, index)
                                        onClicked: pivot.activateCell(rowItem.index, index)
                                        background: Rectangle {
                                            color: cell.down ? Tk.Theme.color.pressed
                                                 : cell.hovered ? Tk.Theme.color.controlHoverBg
                                                 : Tk.Theme.color.controlBg
                                            border.width: Tk.Theme.size.border
                                            border.color: cell.activeFocus ? Tk.Theme.color.focus
                                                         : Tk.Theme.color.controlBorder
                                            radius: Tk.Theme.radius.xs
                                        }
                                        contentItem: Tk.Label {
                                            text: cell.text
                                            horizontalAlignment: Text.AlignRight
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                            leftPadding: Tk.Theme.space.sm
                                            rightPadding: Tk.Theme.space.sm
                                        }
                                        Tk.ToolTip {
                                            text: cell.Accessible.name + qsTr(" · %1 contributing source rows")
                                                  .arg(cell.sourceIds.length)
                                            visible: cell.hovered
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Tk.Label {
                        visible: pivot.totalRows === 0
                        text: pivot.controller && pivot.controller.status === "complete"
                              ? qsTr("No rows matched this query") : qsTr("Run a query to see results")
                        muted: true
                    }
                }
            }
        }
    }
}
