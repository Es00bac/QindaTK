// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/workbook.md): one sheet of a
// Tk.Workbook shown through Tk.EditableGrid via Tk.WorkbookSheetAdapter.
// The Workbook owns inputs, formulas, values and history; the grid owns only
// geometry and stable-ID selection; this view keeps the active cell. Cell
// editing shows the formula source, never the computed display value.
T.Control {
    id: control

    property Tk.Workbook workbook: null
    // Requested sheet; `currentSheetId` is the one shown (the adapter falls
    // back to the first sheet when the requested one is removed).
    property string sheetId: ""
    property string tooltip: qsTr("Spreadsheet")
    property int epoch: 0
    readonly property alias adapter: sheetAdapter
    readonly property alias grid: sheetGrid
    readonly property string currentSheetId: sheetAdapter.sheetId
    readonly property string sheetName: sheetAdapter.sheetName
    // AGENT-NOTE: EditableGrid.activeRow/activeColumn bind to an invokable
    // and do not re-evaluate when rows move; these follow the stable IDs
    // through structural edits via `epoch` (docs/workbook.md gap G-3).
    // AGENT-GUARD: `epoch` must take part in each value (not an unused
    // local): the AOT-compiled binding otherwise drops the dependency and
    // the active cell never refreshes after an edit.
    readonly property int activeRow: control.epoch >= 0 && sheetGrid.model
                                     ? sheetGrid.model.rowIndex(sheetGrid.activeRowId) : -1
    readonly property int activeColumn: control.epoch >= 0 && sheetGrid.model
                                        ? sheetGrid.model.columnIndex(sheetGrid.activeColumnId) : -1
    readonly property bool hasActiveCell: activeRow >= 0 && activeColumn >= 0
    readonly property string activeAddress: hasActiveCell ? sheetAdapter.address(activeRow, activeColumn) : ""
    readonly property string activeInput: control.epoch >= 0 && hasActiveCell
                                          ? sheetAdapter.inputAt(activeRow, activeColumn) : ""
    readonly property string activeDisplay: control.epoch >= 0 && hasActiveCell
                                            ? sheetAdapter.displayAt(activeRow, activeColumn) : ""
    readonly property var selection: {
        const ranges = sheetAdapter.gridModel.ranges
        return ranges.length > 0 ? ranges[ranges.length - 1] : null
    }
    readonly property string selectionAddress: selection && workbook
        ? workbook.rangeAddress(selection.firstRow, selection.firstColumn, selection.lastRow, selection.lastColumn) : ""
    readonly property string errorText: sheetAdapter.lastError
    readonly property var errorDetail: sheetAdapter.lastErrorDetail
    property var rememberedCells: ({})
    property string shownSheetId: ""
    // True while this view's own API moves the selection; such moves are not
    // reported as `selectionEdited` (that signal means the user edited it).
    property bool apiSelection: false

    signal currentCellChanged(int row, int column, string address)
    // The user changed the selection in the grid (pointer or keyboard).
    signal selectionEdited()
    // A different sheet is shown and its remembered active cell restored.
    signal sheetShown(string sheetId)
    signal inputCommitted(int row, int column, string input)
    signal inputRejected(string message, var detail)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: sheetName.length > 0 ? qsTr("%1, sheet %2").arg(tooltip).arg(sheetName) : tooltip
    Accessible.description: hasActiveCell
        ? qsTr("Cell %1. Formula or value: %2. Result: %3").arg(activeAddress).arg(activeInput).arg(activeDisplay)
        : qsTr("No active cell")

    function setCurrent(row, column, extend) {
        const quiet = apiSelection
        apiSelection = true
        sheetGrid.setCurrent(row, column, extend === true)
        sheetGrid.ensureCurrentVisible()
        apiSelection = quiet
    }
    function selectRange(firstRow, firstColumn, lastRow, lastColumn) {
        const quiet = apiSelection
        apiSelection = true
        sheetGrid.setCurrent(firstRow, firstColumn, false)
        sheetGrid.setCurrent(lastRow, lastColumn, true)
        apiSelection = quiet
    }
    function focusGrid() { sheetGrid.forceActiveFocus() }
    function beginEdit() { return sheetGrid.beginCellEdit() }
    function commitInputAt(row, column, text) {
        if (!sheetAdapter.commitInput(row, column, text)) return false
        inputCommitted(row, column, text)
        return true
    }
    function commitInput(text) {
        return hasActiveCell && commitInputAt(activeRow, activeColumn, text)
    }
    function selectionOrActive() {
        if (selection) return selection
        return hasActiveCell ? { firstRow: activeRow, firstColumn: activeColumn,
                                 lastRow: activeRow, lastColumn: activeColumn } : null
    }
    function fillDown() {
        const r = selectionOrActive()
        return !!r && sheetAdapter.fillDown(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
    }
    function fillRight() {
        const r = selectionOrActive()
        return !!r && sheetAdapter.fillRight(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
    }
    function copySelection() {
        const r = selectionOrActive()
        return !!r && sheetAdapter.copyRange(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
    }
    function pasteCopied() { return hasActiveCell && sheetAdapter.pasteCopied(activeRow, activeColumn) }
    // Tab/newline text through EditableGrid.pasteMatrix: validated atomically,
    // then applied to the workbook as one "Paste cells" transaction.
    function pasteText(text) {
        return hasActiveCell && sheetGrid.pasteMatrix(activeRow, activeColumn, sheetAdapter.parseTabularText(text))
    }
    function clearSelection() {
        const r = selectionOrActive()
        return !!r && sheetAdapter.clearRange(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
    }
    function undo() { return !!workbook && !sheetGrid.cellEditing && workbook.undo() }
    function redo() { return !!workbook && !sheetGrid.cellEditing && workbook.redo() }
    // Restores the remembered active cell when a different sheet is shown;
    // selections made on the shown sheet are left alone.
    function restoreCell() {
        if (shownSheetId === currentSheetId && hasActiveCell) return
        shownSheetId = currentSheetId
        const saved = rememberedCells[currentSheetId]
        const row = saved ? Math.min(saved.row, sheetAdapter.rowCount - 1) : 0
        const column = saved ? Math.min(saved.column, sheetAdapter.columnCount - 1) : 0
        if (row >= 0 && column >= 0) setCurrent(row, column, false)
        sheetShown(currentSheetId)
    }

    Tk.WorkbookSheetAdapter {
        id: sheetAdapter
        workbook: control.workbook
        sheetId: control.sheetId
        onCellsChanged: control.epoch += 1
        onDimensionsChanged: control.epoch += 1
        onErrorChanged: if (lastError.length > 0) control.inputRejected(lastError, lastErrorDetail)
        onSheetIdChanged: {
            control.epoch += 1
            Qt.callLater(control.restoreCell)
        }
    }
    Connections {
        target: control.workbook
        function onSheetsChanged() { control.epoch += 1 }
        function onResetOccurred() { control.epoch += 1 }
    }
    Connections {
        target: sheetAdapter.gridModel
        function onSelectionChanged() { if (!control.apiSelection) control.selectionEdited() }
    }

    // AGENT-NOTE: EditableGrid's own Ctrl+Z/Y call its model history, which
    // is disabled (externalUndo) because the Workbook owns history. These
    // shortcuts reach the Workbook first while the table itself has focus.
    readonly property bool tableFocused: sheetGrid.activeFocus && !sheetGrid.cellEditing
    Shortcut { sequences: [StandardKey.Undo]; enabled: control.tableFocused; onActivated: control.undo() }
    // Explicit sequences: StandardKey.Redo may already contain Ctrl+Y, and a
    // duplicate sequence makes the shortcut ambiguous (never activated).
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: control.tableFocused; onActivated: control.redo() }
    Shortcut { sequences: ["Ctrl+D"]; enabled: control.tableFocused; onActivated: control.fillDown() }
    Shortcut { sequences: ["Ctrl+R"]; enabled: control.tableFocused; onActivated: control.fillRight() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: control.tableFocused; onActivated: control.copySelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: control.tableFocused; onActivated: control.pasteCopied() }
    Shortcut { sequences: [StandardKey.Delete]; enabled: control.tableFocused; onActivated: control.clearSelection() }

    contentItem: Tk.EditableGrid {
        id: sheetGrid
        objectName: "sheetGrid"
        model: sheetAdapter.gridModel
        tooltip: control.sheetName.length > 0 ? qsTr("Sheet %1").arg(control.sheetName) : control.tooltip
        columnLabel: function(id, index) { return sheetAdapter.columnLabel(index) }
        rowLabel: function(id, index) { return String(index + 1) }
        onCellEditingChanged: {
            if (!sheetGrid.cellEditing || !sheetGrid.model) return
            // Present the source input; the grid pre-filled the display text.
            sheetGrid.editText = sheetAdapter.inputAt(sheetGrid.model.rowIndex(sheetGrid.editingRowId),
                                                      sheetGrid.model.columnIndex(sheetGrid.editingColumnId))
        }
        // AGENT-GUARD: EditableGridModel skips a commit whose text equals the
        // displayed value (a formula showing 6 replaced by the literal 6), so
        // every committed editor value is also applied here; an unchanged
        // input is a no-op without history (docs/workbook.md gap G-1).
        onEditCommitted: function(rowId, columnId, value) {
            control.commitInputAt(sheetGrid.model.rowIndex(rowId), sheetGrid.model.columnIndex(columnId),
                                  value === undefined || value === null ? "" : String(value))
        }
        onCurrentCellChanged: function(rowId, columnId) {
            const row = sheetGrid.model.rowIndex(rowId), column = sheetGrid.model.columnIndex(columnId)
            const next = Object.assign({}, control.rememberedCells)
            next[control.currentSheetId] = { row: row, column: column }
            control.rememberedCells = next
            control.currentCellChanged(row, column, sheetAdapter.address(row, column))
        }
    }
    Component.onCompleted: restoreCell()
}
