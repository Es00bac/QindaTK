// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: EditableGridModel owns cell data, transactions, section
// geometry/order, frozen counts and range identity. This view owns only the
// active cell/editor and the two viewport offsets.
T.Control {
    id: grid

    property Tk.EditableGridModel model: null
    property string tooltip: qsTr("Editable grid")
    property real rowHeaderWidth: Tk.Theme.size.tableColumn
    property real headerHeight: Tk.Theme.size.header
    property var columnLabel: function(id, index) { return String(index + 1) }
    property var rowLabel: function(id, index) { return String(index + 1) }
    property string activeRowId: ""
    property string activeColumnId: ""
    property string anchorRowId: ""
    property string anchorColumnId: ""
    property int lastActiveRowIndex: 0
    property int lastActiveColumnIndex: 0
    property string selectionRestoreRowId: ""
    property string selectionRestoreColumnId: ""
    property bool rangePreview: false
    property real scrollX: 0
    property real scrollY: 0
    property bool showHeaders: true
    property bool alternatingRows: false
    property bool cellEditing: false
    property int dataEpoch: 0
    property string editText: ""
    property string editingRowId: ""
    property string editingColumnId: ""
    readonly property real bodyWidth: Math.max(0, width - (showHeaders ? rowHeaderWidth : 0))
    readonly property real bodyHeight: Math.max(0, height - (showHeaders ? headerHeight : 0))
    readonly property var visibleCellRecords: collectVisibleCells()
    readonly property int activeRow: model && dataEpoch >= 0 ? model.rowIndex(activeRowId) : -1
    readonly property int activeColumn: model && dataEpoch >= 0 ? model.columnIndex(activeColumnId) : -1

    signal currentCellChanged(string rowId, string columnId)
    signal editCommitted(string rowId, string columnId, var value)
    signal editCancelled()

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    activeFocusOnTab: enabled
    focusPolicy: Qt.StrongFocus
    clip: true
    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    Accessible.description: grid.accessibleDescription()
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    function accessibleDescription() {
        if (dataEpoch < 0) return ""
        if (!model) return qsTr("No grid model")
        if (activeRow < 0 || activeColumn < 0)
            return qsTr("%1 rows by %2 columns. Use arrow keys to navigate.")
                    .arg(model.rowCount).arg(model.columnCount)
        const value = model.cellValue(activeRowId, activeColumnId)
        const ranges = model.ranges
        const range = ranges.length ? ranges[ranges.length - 1] : null
        const selection = range && (range.firstRow !== range.lastRow
                                    || range.firstColumn !== range.lastColumn)
            ? qsTr(" Selected range: rows %1–%2, columns %3–%4.")
                .arg(range.firstRow + 1).arg(range.lastRow + 1)
                .arg(range.firstColumn + 1).arg(range.lastColumn + 1) : ""
        return qsTr("Row %1, column %2. Value: %3.%4 Use arrows to move, Shift plus arrows to extend, Control plus Home or End to jump, Enter to edit, Escape to restore selection.")
                .arg(activeRow + 1).arg(activeColumn + 1)
                .arg(value === undefined || value === null ? "" : String(value))
                .arg(selection)
    }

    function cellSelected(row, column) {
        if (!model) return false
        const ranges = model.ranges
        for (let i = 0; i < ranges.length; ++i)
            if (row >= ranges[i].firstRow && row <= ranges[i].lastRow
                    && column >= ranges[i].firstColumn && column <= ranges[i].lastColumn)
                return true
        return false
    }

    function collectVisibleCells() {
        const result = []
        if (dataEpoch < 0 || !model || bodyWidth <= 0 || bodyHeight <= 0) return result
        const regions = model.visibleRegions(bodyWidth, bodyHeight, scrollX, scrollY)
        for (let i = 0; i < regions.length; ++i) {
            const region = regions[i]
            const cells = model.visibleCells(region.firstRow, region.rowCount,
                                             region.firstColumn, region.columnCount)
            for (let c = 0; c < cells.length; ++c) {
                if (result.length >= 4096) return result
                const cell = cells[c]
                const rect = model.cellRect(cell.row, cell.column, scrollX, scrollY)
                cell.x = rect.x
                cell.y = rect.y
                cell.width = rect.width
                cell.height = rect.height
                cell.frozen = region.frozenRow || region.frozenColumn
                result.push(cell)
            }
        }
        return result
    }

    function moveCurrent(rowDelta, columnDelta, extend) {
        if (!model || model.rowCount < 1 || model.columnCount < 1) return
        let row = activeRow < 0 ? 0 : activeRow
        let column = activeColumn < 0 ? 0 : activeColumn
        row = Math.max(0, Math.min(model.rowCount - 1, row + rowDelta))
        column = Math.max(0, Math.min(model.columnCount - 1, column + columnDelta))
        setCurrent(row, column, extend)
        ensureCurrentVisible()
    }

    function setCurrent(row, column, extend) {
        if (!model || row < 0 || column < 0 || row >= model.rowCount
                || column >= model.columnCount) return
        const rowId = model.rowIdAt(row)
        const columnId = model.columnIdAt(column)
        if (extend && !rangePreview && activeRowId && activeColumnId) {
            selectionRestoreRowId = activeRowId
            selectionRestoreColumnId = activeColumnId
            rangePreview = true
        } else if (!extend) rangePreview = false
        if (!extend || anchorRowId.length === 0 || anchorColumnId.length === 0) {
            anchorRowId = rowId
            anchorColumnId = columnId
        }
        activeRowId = rowId
        activeColumnId = columnId
        lastActiveRowIndex = row
        lastActiveColumnIndex = column
        model.setRange(anchorRowId, anchorColumnId, rowId, columnId)
        currentCellChanged(rowId, columnId)
    }

    function reconcileSelection() {
        if (!model || model.rowCount === 0 || model.columnCount === 0) {
            if (cellEditing) cancelCellEdit()
            activeRowId = ""; activeColumnId = ""
            anchorRowId = ""; anchorColumnId = ""
            rangePreview = false
            if (model) model.clearRanges()
            return
        }
        if (cellEditing && (model.rowIndex(editingRowId) < 0
                            || model.columnIndex(editingColumnId) < 0)) cancelCellEdit()
        if (activeRowId && activeColumnId
                && (model.rowIndex(activeRowId) < 0 || model.columnIndex(activeColumnId) < 0)) {
            setCurrent(Math.min(lastActiveRowIndex, model.rowCount - 1),
                       Math.min(lastActiveColumnIndex, model.columnCount - 1), false)
        } else if (activeRowId && activeColumnId
                   && (model.rowIndex(anchorRowId) < 0 || model.columnIndex(anchorColumnId) < 0)) {
            anchorRowId = activeRowId; anchorColumnId = activeColumnId
            model.setRange(activeRowId, activeColumnId, activeRowId, activeColumnId)
            rangePreview = false
        }
        if (activeRowId && activeColumnId && model.rowIndex(activeRowId) >= 0
                && model.columnIndex(activeColumnId) >= 0) {
            lastActiveRowIndex = model.rowIndex(activeRowId)
            lastActiveColumnIndex = model.columnIndex(activeColumnId)
        }
    }

    function ensureCurrentVisible() {
        if (!model || activeRow < 0 || activeColumn < 0) return
        const rect = model.cellRect(activeRow, activeColumn, scrollX, scrollY)
        const frozenWidth = model.frozenColumns > 0
                                ? model.cellRect(model.frozenRows > 0 ? 0 : 0,
                                                 model.frozenColumns - 1, 0, 0).x
                                  + model.cellRect(0, model.frozenColumns - 1, 0, 0).width : 0
        const frozenHeight = model.frozenRows > 0
                                 ? model.cellRect(model.frozenRows - 1, 0, 0, 0).y
                                   + model.cellRect(model.frozenRows - 1, 0, 0, 0).height : 0
        if (activeColumn >= model.frozenColumns) {
            if (rect.x < frozenWidth) scrollX = Math.max(0, scrollX - (frozenWidth - rect.x))
            else if (rect.x + rect.width > bodyWidth)
                scrollX += rect.x + rect.width - bodyWidth
        }
        if (activeRow >= model.frozenRows) {
            if (rect.y < frozenHeight) scrollY = Math.max(0, scrollY - (frozenHeight - rect.y))
            else if (rect.y + rect.height > bodyHeight)
                scrollY += rect.y + rect.height - bodyHeight
        }
        clampScroll()
    }

    function clampScroll() {
        if (!model || model.rowCount < 1 || model.columnCount < 1) {
            scrollX = 0; scrollY = 0; return
        }
        const last = model.cellRect(model.rowCount - 1, model.columnCount - 1, 0, 0)
        scrollX = Math.max(0, Math.min(scrollX, Math.max(0, last.x + last.width - bodyWidth)))
        scrollY = Math.max(0, Math.min(scrollY, Math.max(0, last.y + last.height - bodyHeight)))
    }

    function beginCellEdit() {
        if (!model || activeRow < 0 || activeColumn < 0 || !enabled) return false
        editText = String(model.cellValue(activeRowId, activeColumnId) ?? "")
        editingRowId = activeRowId
        editingColumnId = activeColumnId
        cellEditing = true
        editor.forceActiveFocus()
        editor.selectAll()
        return true
    }

    function commitCellEdit(value) {
        if (!model || editingRowId.length === 0 || editingColumnId.length === 0) return false
        if (!model.beginEdit(qsTr("Edit cell"))) return false
        if (!model.stageCell(editingRowId, editingColumnId, value)) {
            model.cancelEdit()
            return false
        }
        if (!model.commitEdit()) return false
        cellEditing = false
        editCommitted(editingRowId, editingColumnId, value)
        editingRowId = ""
        editingColumnId = ""
        forceActiveFocus()
        return true
    }

    function cancelCellEdit() {
        if (!cellEditing) return
        if (model && model.editing) model.cancelEdit()
        cellEditing = false
        editingRowId = ""
        editingColumnId = ""
        editCancelled()
        forceActiveFocus()
    }

    function pasteMatrix(firstRow, firstColumn, rows) {
        if (!model || !enabled) return false
        return model.paste(firstRow, firstColumn, rows)
    }

    function fillSelection(targetRow, targetColumn) {
        if (!model || !enabled || model.ranges.length === 0) return false
        const range = model.ranges[model.ranges.length - 1]
        const rowCount = range.lastRow - range.firstRow + 1
        const columnCount = range.lastColumn - range.firstColumn + 1
        return model.fill(range.firstRow, range.firstColumn, rowCount, columnCount,
                          targetRow, targetColumn)
    }

    function onGeometryChanged() { clampScroll() }

    Keys.onPressed: function(event) {
        if (!enabled || !model) return
        const extend = (event.modifiers & Qt.ShiftModifier) !== 0
        switch (event.key) {
        case Qt.Key_Up: moveCurrent(-1, 0, extend); break
        case Qt.Key_Down: moveCurrent(1, 0, extend); break
        case Qt.Key_Left: moveCurrent(0, -1, extend); break
        case Qt.Key_Right: moveCurrent(0, 1, extend); break
        case Qt.Key_Home:
            setCurrent(event.modifiers & Qt.ControlModifier ? 0 : Math.max(0, activeRow),
                       0, extend); break
        case Qt.Key_End:
            setCurrent(event.modifiers & Qt.ControlModifier ? model.rowCount - 1
                       : Math.max(0, activeRow), model.columnCount - 1, extend); break
        case Qt.Key_PageUp: moveCurrent(-Math.max(1, Math.floor(bodyHeight / model.defaultRowSize) - 1), 0, extend); break
        case Qt.Key_PageDown: moveCurrent(Math.max(1, Math.floor(bodyHeight / model.defaultRowSize) - 1), 0, extend); break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_F2:
            beginCellEdit(); break
        case Qt.Key_Escape:
            if (cellEditing) cancelCellEdit()
            else if (rangePreview && model.rowIndex(selectionRestoreRowId) >= 0
                     && model.columnIndex(selectionRestoreColumnId) >= 0)
                setCurrent(model.rowIndex(selectionRestoreRowId),
                           model.columnIndex(selectionRestoreColumnId), false)
            break
        case Qt.Key_Z:
            if (event.modifiers & Qt.ControlModifier) model.undo()
            else return
            break
        case Qt.Key_Y:
            if (event.modifiers & Qt.ControlModifier) model.redo()
            else return
            break
        default: return
        }
        ensureCurrentVisible()
        event.accepted = true
    }

    onWidthChanged: clampScroll()
    onHeightChanged: clampScroll()
    onModelChanged: reconcileSelection()

    Connections {
        target: grid.model
        ignoreUnknownSignals: true
        function onStructureChanged() {
            grid.reconcileSelection(); grid.clampScroll(); grid.dataEpoch += 1
        }
        function onGeometryChanged() { grid.clampScroll(); grid.dataEpoch += 1 }
        function onCellsChanged() { grid.dataEpoch += 1 }
    }

    contentItem: Item {
        id: surface
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Tk.Theme.color.panel
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.divider
        }

        Tk.HeaderView {
            objectName: "gridColumnHeaders"
            visible: grid.showHeaders
            gridModel: grid.model
            orientation: Qt.Horizontal
            scrollX: grid.scrollX
            scrollY: grid.scrollY
            rowHeaderWidth: grid.rowHeaderWidth
            sectionLabel: grid.columnLabel
            x: grid.showHeaders ? grid.rowHeaderWidth : 0
            width: grid.bodyWidth
            height: grid.headerHeight
            z: 5
        }
        Tk.HeaderView {
            objectName: "gridRowHeaders"
            visible: grid.showHeaders
            gridModel: grid.model
            orientation: Qt.Vertical
            scrollX: grid.scrollX
            scrollY: grid.scrollY
            rowHeaderWidth: grid.rowHeaderWidth
            sectionLabel: grid.rowLabel
            y: grid.headerHeight
            width: grid.rowHeaderWidth
            height: grid.bodyHeight
            z: 5
        }
        Rectangle {
            objectName: "gridCornerHeader"
            visible: grid.showHeaders
            x: 0; y: 0
            width: grid.rowHeaderWidth; height: grid.headerHeight
            color: Tk.Theme.color.headerBg
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.divider
            z: 6
        }

        Item {
            id: cellViewport
            objectName: "gridCellViewport"
            x: grid.showHeaders ? grid.rowHeaderWidth : 0
            y: grid.showHeaders ? grid.headerHeight : 0
            width: grid.bodyWidth
            height: grid.bodyHeight
            clip: true

            WheelHandler {
                objectName: "gridWheel"
                enabled: grid.enabled
                blocking: false
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(event) {
                    const amountX = event.angleDelta.x / 120 * Tk.Theme.size.tableColumn
                    const amountY = event.angleDelta.y / 120 * Tk.Theme.size.row * 3
                    if (event.modifiers & Qt.ShiftModifier) {
                        grid.scrollX -= event.angleDelta.y / 120 * Tk.Theme.size.tableColumn
                    } else {
                        grid.scrollX -= amountX
                        grid.scrollY -= amountY
                    }
                    grid.clampScroll()
                    event.accepted = true
                }
            }

            Repeater {
                model: grid.visibleCellRecords
                delegate: Item {
                    id: cell
                    required property var modelData
                    required property int index
                    objectName: "gridCell_" + cell.modelData.rowId + "_" + cell.modelData.columnId
                    x: cell.modelData.x
                    y: cell.modelData.y
                    width: cell.modelData.width
                    height: cell.modelData.height
                    z: cell.modelData.frozen ? 2 : 1
                    Accessible.role: Accessible.Cell
                    Accessible.name: qsTr("Row %1, column %2: %3")
                                      .arg(cell.modelData.row + 1)
                                      .arg(cell.modelData.column + 1)
                                      .arg(cell.modelData.value === undefined
                                           || cell.modelData.value === null
                                           ? "" : String(cell.modelData.value))
                    Accessible.description: qsTr("Select this cell")
                    Accessible.focusable: true
                    Accessible.focused: grid.activeFocus && grid.activeRowId === cell.modelData.rowId
                                        && grid.activeColumnId === cell.modelData.columnId
                    Accessible.selected: grid.cellSelected(cell.modelData.row, cell.modelData.column)
                    Accessible.onPressAction: {
                        grid.setCurrent(cell.modelData.row, cell.modelData.column, false)
                        grid.forceActiveFocus()
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: cell.modelData.row % 2 === 1 && grid.alternatingRows
                               ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
                        border.width: Tk.Theme.size.border
                        border.color: Tk.Theme.color.divider
                    }
                    Tk.Label {
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.xs
                        anchors.rightMargin: Tk.Theme.space.xs
                        verticalAlignment: Text.AlignVCenter
                        text: cell.modelData.value === undefined || cell.modelData.value === null
                              ? "" : String(cell.modelData.value)
                        color: Tk.Theme.color.text
                        font.pixelSize: Tk.Theme.font.body
                        elide: Text.ElideRight
                        visible: !(grid.activeRowId === cell.modelData.rowId
                                   && grid.activeColumnId === cell.modelData.columnId
                                   && editor.activeFocus)
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onPressed: function(mouse) {
                            grid.setCurrent(cell.modelData.row, cell.modelData.column,
                                            (mouse.modifiers & Qt.ShiftModifier) !== 0)
                            grid.forceActiveFocus()
                        }
                        onDoubleClicked: grid.beginCellEdit()
                    }
                }
            }
            Tk.RangeSelectionOverlay {
                objectName: "gridRangeOverlay"
                anchors.fill: parent
                gridModel: grid.model
                scrollX: grid.scrollX
                scrollY: grid.scrollY
            }
            Tk.TextField {
                id: editor
                objectName: "gridCellEditor"
                visible: grid.cellEditing
                x: grid.model && grid.model.rowIndex(grid.editingRowId) >= 0
                   && grid.model.columnIndex(grid.editingColumnId) >= 0
                   ? grid.model.cellRect(grid.model.rowIndex(grid.editingRowId),
                                         grid.model.columnIndex(grid.editingColumnId),
                                         grid.scrollX, grid.scrollY).x : 0
                y: grid.model && grid.model.rowIndex(grid.editingRowId) >= 0
                   && grid.model.columnIndex(grid.editingColumnId) >= 0
                   ? grid.model.cellRect(grid.model.rowIndex(grid.editingRowId),
                                         grid.model.columnIndex(grid.editingColumnId),
                                         grid.scrollX, grid.scrollY).y : 0
                width: grid.model && grid.model.rowIndex(grid.editingRowId) >= 0
                       && grid.model.columnIndex(grid.editingColumnId) >= 0
                       ? grid.model.cellRect(grid.model.rowIndex(grid.editingRowId),
                                             grid.model.columnIndex(grid.editingColumnId),
                                             grid.scrollX, grid.scrollY).width : 0
                height: grid.model && grid.model.rowIndex(grid.editingRowId) >= 0
                        && grid.model.columnIndex(grid.editingColumnId) >= 0
                        ? grid.model.cellRect(grid.model.rowIndex(grid.editingRowId),
                                              grid.model.columnIndex(grid.editingColumnId),
                                              grid.scrollX, grid.scrollY).height : 0
                text: grid.editText
                tooltip: qsTr("Edit grid cell")
                onAccepted: grid.commitCellEdit(text)
                Keys.onEscapePressed: function(event) {
                    event.accepted = true
                    grid.cancelCellEdit()
                }
                onActiveFocusChanged: if (!activeFocus && visible && grid.model
                                          && grid.model.editing) grid.commitCellEdit(text)
            }
        }
    }

    Binding {
        target: grid.model
        property: "defaultRowSize"
        value: Tk.Theme.size.row
        when: grid.model !== null
    }
    Binding {
        target: grid.model
        property: "defaultColumnSize"
        value: Tk.Theme.size.tableColumn
        when: grid.model !== null
    }
}
