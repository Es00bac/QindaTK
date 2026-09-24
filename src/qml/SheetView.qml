// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/workbook.md): one sheet of a
// Tk.Workbook shown through Tk.EditableGrid via Tk.WorkbookSheetAdapter.
// The Workbook owns inputs, formulas, values, formats, rules, sheet
// properties and history; the grid (sourceCommits) owns only geometry and
// stable-ID selection, and its edits, pastes and fill-handle drags become
// Workbook transactions; this view keeps the active cell and a pending
// validation prompt. Cell editing shows the formula source, never the
// computed display value.
T.Control {
    id: control

    property Tk.Workbook workbook: null
    // Requested sheet; `currentSheetId` is the one shown (the adapter falls
    // back to the first sheet when the requested one is removed).
    property string sheetId: ""
    property string tooltip: qsTr("Spreadsheet")
    // Policy for pastes: "allOrNothing" or "skipInvalid" (Workbook.pasteInputs).
    property string pastePolicy: "allOrNothing"
    // Optional system clipboard: copies publish the range (Workbook.rangeTransfer)
    // with its tab-separated text; pastes take a workbook range from any
    // workbook (one undo step, sheets remapped) or plain text. Without it,
    // copy/paste stay inside this view (formula-aware copyRange/pasteCopied).
    property Tk.ClipboardService clipboard: null
    property int epoch: 0
    readonly property alias adapter: sheetAdapter
    readonly property alias grid: sheetGrid
    readonly property string currentSheetId: sheetAdapter.sheetId
    readonly property string sheetName: sheetAdapter.sheetName
    readonly property int activeRow: sheetGrid.activeRow
    readonly property int activeColumn: sheetGrid.activeColumn
    readonly property bool hasActiveCell: activeRow >= 0 && activeColumn >= 0
    readonly property string activeAddress: hasActiveCell ? sheetAdapter.address(activeRow, activeColumn) : ""
    // AGENT-GUARD: `epoch` must take part in each value (not an unused
    // local): the AOT-compiled binding otherwise drops the dependency and
    // these never refresh after an edit.
    readonly property string activeInput: control.epoch >= 0 && hasActiveCell
                                          ? sheetAdapter.inputAt(activeRow, activeColumn) : ""
    readonly property string activeDisplay: control.epoch >= 0 && hasActiveCell
                                            ? sheetAdapter.displayAt(activeRow, activeColumn) : ""
    // Explicit formats of the active cell (for toolbar state) and its rules.
    readonly property var activeFormat: control.epoch >= 0 && hasActiveCell && workbook
                                        ? workbook.cellFormat(currentSheetId, activeRow, activeColumn) : ({})
    readonly property var activeValidation: control.epoch >= 0 && hasActiveCell
                                            ? sheetAdapter.validationAt(activeRow, activeColumn) : ({})
    readonly property var activeMerge: control.epoch >= 0 && hasActiveCell && workbook
                                       ? workbook.mergeAt(currentSheetId, activeRow, activeColumn) : ({})
    readonly property var sheetProperties: control.epoch >= 0 && workbook ? workbook.sheetProperties(currentSheetId) : ({})
    readonly property bool readOnly: sheetProperties.protected === true
    readonly property bool validationPending: assist.pending !== null
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
    // A validation prompt closed: action is "commit", "retry" or "cancel".
    signal validationResolved(int row, int column, string action)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: sheetName.length > 0 ? qsTr("%1, sheet %2").arg(tooltip).arg(sheetName) : tooltip
    Accessible.description: hasActiveCell
        ? qsTr("Cell %1. Formula or value: %2. Result: %3").arg(activeAddress).arg(activeInput).arg(activeDisplay)
        : qsTr("No active cell")

    // AGENT-GUARD: an API selection marks its sheet as shown, so the
    // restoreCell queued by a sheet change does not replace it afterwards.
    function setCurrent(row, column, extend) {
        const quiet = apiSelection
        apiSelection = true
        shownSheetId = currentSheetId
        sheetGrid.setCurrent(row, column, extend === true)
        sheetGrid.ensureCurrentVisible()
        apiSelection = quiet
    }
    function selectRange(firstRow, firstColumn, lastRow, lastColumn) {
        const quiet = apiSelection
        apiSelection = true
        shownSheetId = currentSheetId
        sheetGrid.setCurrent(firstRow, firstColumn, false)
        sheetGrid.setCurrent(lastRow, lastColumn, true)
        apiSelection = quiet
    }
    function focusGrid() { sheetGrid.forceActiveFocus() }
    function beginEdit() { return sheetGrid.beginCellEdit() }
    // Applies one input. A warning/information rule the input breaks opens a
    // prompt first (validationPending; false now, validationResolved later);
    // `confirmed` skips it.
    function commitInputAt(row, column, text, confirmed) {
        if (confirmed !== true) {
            const check = sheetAdapter.validateInput(row, column, text)
            if (!check.ok && !check.syntax && check.style !== "stop") {
                assist.prompt(row, column, text, check, "api")
                return false
            }
        }
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
    // Runs a Workbook range operation on the selection and reports its result here.
    function onSelection(operation) {
        const r = selectionOrActive()
        return !!r && !!workbook
               && sheetAdapter.reportResult(operation(currentSheetId, r.firstRow, r.firstColumn, r.lastRow, r.lastColumn))
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
        if (!r || !sheetAdapter.copyRange(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)) return false
        if (!clipboard || !workbook) return true
        const transfer = workbook.rangeTransfer(currentSheetId, r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
        return transfer.ok && clipboard.copyRange(workbook.documentId, workbook.revision, transfer.range, transfer.text)
    }
    function pasteCopied() {
        if (!hasActiveCell) return false
        const data = clipboard ? clipboard.rangePasteData() : ({ kind: "" })
        if (data.kind === "range")
            return sheetAdapter.reportResult(workbook.pasteTransfer(currentSheetId, activeRow, activeColumn,
                                                                    data.range, pastePolicy))
        if (data.kind === "text") return pasteText(data.text)
        return sheetAdapter.pasteCopied(activeRow, activeColumn)
    }
    // Tab/newline text under `pastePolicy`: validated per cell, applied as one
    // "Paste cells" step; Workbook.lastPasteReport lists invalid cells.
    function pasteText(text) {
        return hasActiveCell && sheetAdapter.pasteInputs(activeRow, activeColumn,
                                                         sheetAdapter.parseTabularText(text), pastePolicy)
    }
    function clearSelection() {
        const r = selectionOrActive()
        return !!r && sheetAdapter.clearRange(r.firstRow, r.firstColumn, r.lastRow, r.lastColumn)
    }
    // Formatting of the selection (Workbook.applyStyle keys).
    function applyStyle(patch) {
        return onSelection(function(s, r1, c1, r2, c2) { return workbook.applyStyle(s, r1, c1, r2, c2, patch) })
    }
    // Toggles bold/italic/underline/strikeout from the active cell's state.
    function toggleStyle(key) {
        const patch = {}
        patch[key] = activeFormat[key] !== true
        return applyStyle(patch)
    }
    function clearFormats() {
        return onSelection(function(s, r1, c1, r2, c2) { return workbook.clearFormats(s, r1, c1, r2, c2) })
    }
    function mergeSelection() {
        return onSelection(function(s, r1, c1, r2, c2) { return workbook.mergeCells(s, r1, c1, r2, c2) })
    }
    function unmergeSelection() {
        return onSelection(function(s, r1, c1, r2, c2) { return workbook.unmergeCells(s, r1, c1, r2, c2) })
    }
    // Freezes the rows above and the columns left of the active cell (none at A1).
    function freezePanes() {
        return hasActiveCell && !!workbook && sheetAdapter.reportResult(
            workbook.setSheetProperties(currentSheetId, { frozenRows: activeRow, frozenColumns: activeColumn }))
    }
    function unfreezePanes() {
        return !!workbook && sheetAdapter.reportResult(
            workbook.setSheetProperties(currentSheetId, { frozenRows: 0, frozenColumns: 0 }))
    }
    function openValidationList() { return assist.openList() }
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
        onErrorChanged: if (lastError.length > 0 && !control.validationPending)
                            control.inputRejected(lastError, lastErrorDetail)
        onSheetIdChanged: {
            control.epoch += 1
            Qt.callLater(control.restoreCell)
        }
        onValidationPrompt: function(row, column, input, check) { assist.prompt(row, column, input, check, "grid") }
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

    // AGENT-NOTE: the grid's own Ctrl+Z/Y reach its model, which keeps no
    // history (sourceCommits: the Workbook owns it). These shortcuts reach
    // the Workbook first while the table itself has focus.
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
    Shortcut { sequences: [StandardKey.Bold]; enabled: control.tableFocused; onActivated: control.toggleStyle("bold") }
    Shortcut { sequences: [StandardKey.Italic]; enabled: control.tableFocused; onActivated: control.toggleStyle("italic") }
    Shortcut { sequences: [StandardKey.Underline]; enabled: control.tableFocused; onActivated: control.toggleStyle("underline") }
    Shortcut { sequences: ["Alt+Down"]; enabled: control.tableFocused; onActivated: control.openValidationList() }

    contentItem: Tk.EditableGrid {
        id: sheetGrid
        objectName: "sheetGrid"
        model: sheetAdapter.gridModel
        tooltip: control.sheetName.length > 0 ? qsTr("Sheet %1").arg(control.sheetName) : control.tooltip
        columnLabel: function(id, index) { return sheetAdapter.columnLabel(index) }
        rowLabel: function(id, index) { return String(index + 1) }
        onEditCommitted: function(rowId, columnId, value) {
            control.inputCommitted(sheetGrid.model.rowIndex(rowId), sheetGrid.model.columnIndex(columnId),
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

    Tk.SheetCellAssist {
        id: assist
        view: control
        x: control.leftPadding
        y: control.topPadding
        width: control.availableWidth
        height: control.availableHeight
        z: 20
    }

    Component.onCompleted: restoreCell()
}
