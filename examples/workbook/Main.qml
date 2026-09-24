// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// WP-055 spreadsheet slice: a two-sheet budget. Switch sheets with the tabs
// (right-click a tab for color, hide and protect), select a cell and edit it
// in the formula bar (Enter commits, Escape restores) or in the cell (type,
// Enter or F2). Select F2:F5 and press "Fill down" (Ctrl+D) — or drag the
// fill handle — to copy the total formula with relative references moved.
// Format with the toolbar (Ctrl+B/I/U), merge, freeze panes, and edit
// conditional formats and validation in the side panel (B8 has a warning
// rule; enter 0.9 there). Undo, save, change something, then "Reopen saved".
//   qtk-preview examples/workbook/Main.qml
//   qtk-preview examples/workbook/Main.qml --dump --size 1440x820
Item {
    id: root
    objectName: "workbookExample"
    width: 1440
    height: 820

    // A local path: sessions save to and open paths.
    readonly property string filePath: decodeURIComponent(
        String(StandardPaths.writableLocation(StandardPaths.TempLocation)).replace(/^file:\/\//, ""))
        + "/qindatk-workbook-example.qtkwb"
    // The workbook is the model of a DocumentSession: the DocumentManager owns
    // the session, its single history (the manager's UndoService) and saves.
    property Tk.DocumentSession session: null
    readonly property Tk.Workbook book: session ? session.model : null
    property string status: qsTr("Select F2:F5 and fill down to complete the totals")
    readonly property var numberFormats: [
        { text: "General", value: "General" }, { text: "1,234.50", value: "#,##0.00" },
        { text: "$1,234.50", value: "$#,##0.00" }, { text: "12%", value: "0%" }, { text: "12.34%", value: "0.00%" },
        { text: "1.23E+03", value: "0.00E+00" }, { text: "2026-09-23", value: "yyyy-mm-dd" }, { text: "Text", value: "@" }]
    readonly property string currentNumberFormat: sheet.activeFormat.numberFormat || "General"

    function seed() {
        const budget = book.sheetIdAt(0)
        book.renameSheet(budget, "Budget")
        const summary = book.addSheet("Summary", -1, 200, 12)
        book.pasteInputs(budget, 0, 0, [
            ["Item", "Q1", "Q2", "Q3", "Q4", "Total"],
            ["Rent", "1200", "1200", "1250", "1250", "=SUM(B2:E2)"],
            ["Payroll", "5400", "5600", "5600", "5800", ""],
            ["Software", "310.5", "290", "305", "320", ""],
            ["Travel", "800", "0", "1150", "400", ""],
            ["Total", "=SUM(B2:B5)", "", "", "", ""],
            ["", "", "", "", "", ""],
            ["Tax rate", "0.08", "", "", "", ""],
            ["Tax", "=ROUND(B6*$B$8,2)", "", "", "", ""]])
        book.defineName("Totals", budget, 1, 5, 4, 5)
        book.pasteInputs(summary, 0, 0, [
            ["Metric", "Value"],
            ["Annual spend", "=SUM(Budget!B2:E5)"],
            ["Largest quarter", "=MAX(Budget!B6:E6)"],
            ["Average item", "=AVERAGE(Totals)"],
            ["Status", "=IF(B2>40000,\"Over plan\",\"Within plan\")"],
            ["Q1 tax", "=Budget!B9"],
            ["Items listed", "=COUNTA(Budget!A:A)-2"]])
        // Demo document formats (document colors are data, not theme roles).
        book.applyStyle(budget, 0, 0, 0, 5, { bold: true, fillColor: "#dbe5f1", horizontalAlignment: "center" })
        book.applyStyle(budget, 1, 1, 5, 5, { numberFormat: "#,##0.00" })
        book.applyStyle(budget, 5, 0, 5, 5, { bold: true })
        book.applyStyle(budget, 7, 1, 7, 1, { numberFormat: "0%" })
        book.applyStyle(budget, 8, 1, 8, 1, { numberFormat: "#,##0.00" })
        book.setConditionalFormat(budget, { ranges: ["F2:F5"], type: "dataBar", maxColor: "#5b9bd5" })
        book.setConditionalFormat(budget, { ranges: ["B2:E5"], type: "cellValue", operator: "greater",
                                            value1: "1200", style: { textColor: "#c0392b", bold: true } })
        book.setValidation(budget, { ranges: ["B8"], type: "decimal", operator: "between", value1: "0",
                                     value2: "0.5", errorStyle: "warning", inputTitle: "Tax rate",
                                     inputMessage: "A fraction from 0 to 0.5 (8% is 0.08)",
                                     errorTitle: "Unusual tax rate", errorMessage: "Rates above 50% are unusual" })
        book.setSheetProperties(budget, { frozenRows: 1, frozenColumns: 1, tabColor: "#4f81bd" })
        book.setColumnWidth(budget, 0, 110)
        book.applyStyle(summary, 0, 0, 0, 1, { bold: true, fillColor: "#e2efd9" })
        book.applyStyle(summary, 1, 1, 3, 1, { numberFormat: "#,##0.00" })
        book.mergeCells(summary, 8, 0, 8, 1)
        book.setCellInput(summary, 8, 0, "Figures follow the Budget sheet")
        book.applyStyle(summary, 8, 0, 8, 0, { italic: true, horizontalAlignment: "center" })
        book.setColumnWidth(summary, 0, 140)
        // Present the seeded document as a freshly opened one: same content,
        // no seed history.
        book.restoreText(book.serializedText())
        sheet.sheetId = book.sheetIdAt(0)
        sheet.selectRange(4, 5, 1, 5) // F5 up to F2: F2 is the active cell
        sheet.focusGrid()
    }
    function report(ok, done) { status = ok ? done : sheet.errorText }

    Component.onCompleted: {
        // Large pastes and sheet removals stay undoable (the default is 8 MiB).
        documents.undoService.memoryLimit = "268435456"
        session = documents.createDocument("qindatk.workbook.v1")
        seed()
    }
    // Save/Save As write on a worker; Reopen closes and reopens the file.
    Tk.DocumentManager { id: documents }
    readonly property Tk.UndoService history: documents.undoService
    Connections {
        target: root.session
        function onSaveFinished(ok, message, revision) { root.status = ok ? "Saved revision " + revision : message }
    }
    function reopenSaved() {
        const path = session.uri
        if (!documents.closeDocument(session.documentId, Tk.DocumentManager.Discard)) return documents.lastError
        const reopened = documents.openDocument(path, "qindatk.workbook.v1")
        if (!reopened) return documents.lastError
        session = reopened
        history.activeModel = book
        return "Reopened the saved workbook"
    }
    // Ctrl+C/Ctrl+V use the system clipboard: ranges paste into any workbook,
    // and other applications receive tab-separated text.
    Tk.ClipboardService { id: clipboard }
    Tk.MessageDialog {
        id: removal
        objectName: "removeSheetDialog"
        property string sheetId: ""
        title: "Remove sheet"
        variant: "warning"
        primaryText: "Remove"
        secondaryText: "Cancel"
        destructive: true
        onAccepted: root.status = book.removeSheet(sheetId) ? "Removed the sheet (Undo restores it)" : book.lastError
    }

    Tk.Box {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        color: Tk.Theme.color.canvas
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Flex {
                objectName: "workbookToolbar"
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Tk.Button {
                    objectName: "fillDownButton"
                    text: "Fill down"
                    iconName: "arrow-down-to-line"
                    tooltip: "Copy the top row of the selection down, moving relative references (Ctrl+D)"
                    enabled: !formula.editing
                    onClicked: root.report(sheet.fillDown(), "Filled " + sheet.selectionAddress)
                }
                Tk.Button {
                    objectName: "fillRightButton"
                    text: "Fill right"
                    iconName: "arrow-right"
                    tooltip: "Copy the left column of the selection right (Ctrl+R)"
                    enabled: !formula.editing
                    onClicked: root.report(sheet.fillRight(), "Filled " + sheet.selectionAddress)
                }
                Tk.IconButton {
                    objectName: "undoButton"
                    iconName: "undo-2"
                    tooltip: history.canUndo ? "Undo " + history.undoText : "Nothing to undo"
                    enabled: history.canUndo && !formula.editing
                    onClicked: history.undo()
                }
                Tk.IconButton {
                    objectName: "redoButton"
                    iconName: "redo-2"
                    tooltip: history.canRedo ? "Redo " + history.redoText : "Nothing to redo"
                    enabled: history.canRedo && !formula.editing
                    onClicked: history.redo()
                }
                Tk.Button {
                    objectName: "saveButton"
                    text: root.session && root.session.saving ? "Saving…" : "Save"
                    iconName: "save"
                    tooltip: "Save the workbook to " + root.filePath
                    enabled: !formula.editing && !!root.session && !root.session.saving
                    // The example's fixed file may exist from an earlier run.
                    onClicked: {
                        const started = root.session.uri.length > 0 ? root.session.requestSave()
                                                                    : root.session.requestSaveAs(root.filePath, true)
                        if (!started) root.status = root.session.lastError
                    }
                }
                Tk.Button {
                    objectName: "reopenButton"
                    text: "Reopen saved"
                    iconName: "folder-open"
                    tooltip: "Discard changes and reopen the saved workbook file"
                    enabled: !formula.editing && !!root.session && !root.session.saving && root.session.uri.length > 0
                    onClicked: root.status = root.reopenSaved()
                }
                Tk.Caption {
                    objectName: "workbookMessage"
                    Tk.Flex.grow: 1
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                    text: root.status
                }
            }
            Tk.Flex {
                objectName: "formatToolbar"
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                enabled: !formula.editing && !sheet.readOnly
                Repeater {
                    model: [{ key: "bold", icon: "bold", tip: "Bold (Ctrl+B)" }, { key: "italic", icon: "italic", tip: "Italic (Ctrl+I)" },
                            { key: "underline", icon: "underline", tip: "Underline (Ctrl+U)" },
                            { key: "strikeout", icon: "strikethrough", tip: "Strikethrough" }]
                    delegate: Tk.IconButton {
                        required property var modelData
                        objectName: modelData.key + "Button"
                        iconName: modelData.icon
                        tooltip: modelData.tip
                        checked: sheet.activeFormat[modelData.key] === true
                        Accessible.checkable: true
                        onClicked: sheet.toggleStyle(modelData.key)
                    }
                }
                Tk.Segmented {
                    objectName: "alignmentSegments"
                    small: true
                    tooltip: "Horizontal alignment of the selection"
                    model: [{ text: "Auto", value: "general" }, { iconName: "text-align-start", value: "left", tooltip: "Left" },
                            { iconName: "text-align-center", value: "center", tooltip: "Center" },
                            { iconName: "text-align-end", value: "right", tooltip: "Right" }]
                    currentIndex: ["general", "left", "center", "right"].indexOf(sheet.activeFormat.horizontalAlignment || "general")
                    onActivated: function(index) { sheet.applyStyle({ horizontalAlignment: model[index].value }) }
                }
                Tk.ComboBox {
                    objectName: "numberFormatBox"
                    small: true
                    tooltip: "Number format of the selection"
                    model: root.numberFormats
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: Math.max(0, root.numberFormats.findIndex(function(f) { return f.value === root.currentNumberFormat }))
                    onActivated: sheet.applyStyle({ numberFormat: currentValue })
                }
                Tk.IconButton {
                    objectName: "fillColorButton"
                    iconName: "paint-bucket"
                    tooltip: "Highlight the selection"
                    onClicked: sheet.applyStyle({ fillColor: "#fff2cc" })
                }
                Tk.IconButton {
                    objectName: "clearFormatsButton"
                    iconName: "eraser"
                    tooltip: "Clear the selection's formats"
                    onClicked: root.report(sheet.clearFormats(), "Cleared formats of " + sheet.selectionAddress)
                }
                Tk.Button {
                    objectName: "mergeButton"
                    small: true
                    text: sheet.activeMerge.range ? "Unmerge" : "Merge"
                    iconName: "grid-2x2"
                    tooltip: "Merge the selection into one cell (keeps the top-left value), or unmerge"
                    onClicked: root.report(sheet.activeMerge.range ? sheet.unmergeSelection() : sheet.mergeSelection(),
                                           "Merged " + sheet.selectionAddress)
                }
                Tk.Button {
                    objectName: "freezeButton"
                    small: true
                    enabled: true
                    text: sheet.sheetProperties.frozenRows > 0 || sheet.sheetProperties.frozenColumns > 0 ? "Unfreeze" : "Freeze panes"
                    iconName: "pin"
                    tooltip: "Keep the rows above and the columns left of the active cell in view"
                    onClicked: root.report(text === "Unfreeze" ? sheet.unfreezePanes() : sheet.freezePanes(), text + " done")
                }
            }
            Tk.WorkbookTabs {
                id: tabs
                objectName: "workbookTabs"
                workbook: book
                currentSheetId: sheet.currentSheetId
                onSheetActivated: function(sheetId) { sheet.sheetId = sheetId }
                onRemoveRequested: function(sheetId, name) {
                    removal.sheetId = sheetId
                    removal.text = "Remove “" + name + "”? Formulas on other sheets that refer to it show #REF!; Undo restores the sheet and those formulas."
                    removal.open()
                }
            }
            Tk.FormulaBar {
                id: formula
                objectName: "formulaBar"
                sheetView: sheet
                onCommitted: function(address, input) { root.status = "Set " + address + " to " + input }
            }
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.SheetView {
                    id: sheet
                    objectName: "sheetView"
                    Tk.Flex.grow: 1
                    workbook: book
                    tooltip: "Budget workbook"
                    pastePolicy: validationEditor.pastePolicy
                    clipboard: clipboard
                    onInputRejected: function(message) { root.status = message }
                }
                Tk.Card {
                    objectName: "rulesPanel"
                    padding: Tk.Theme.space.sm
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.sm
                        Tk.Segmented {
                            id: panelSwitch
                            objectName: "rulesPanelSwitch"
                            small: true
                            stretch: true
                            tooltip: "Rule editor"
                            model: ["Conditional formats", "Validation"]
                        }
                        Tk.Scroll {
                            objectName: "rulesPanelScroll"
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            Tk.Flex {
                                direction: Tk.Flex.Column
                                Tk.ConditionalFormatEditor {
                                    id: conditionalEditor
                                    objectName: "conditionalEditor"
                                    visible: panelSwitch.currentIndex === 0
                                    workbook: book
                                    sheetId: sheet.currentSheetId
                                    onApplied: root.status = "Conditional format saved"
                                    Component.onCompleted: newRule("B2:E5")
                                }
                                Tk.DataValidationEditor {
                                    id: validationEditor
                                    objectName: "validationEditor"
                                    visible: panelSwitch.currentIndex === 1
                                    workbook: book
                                    sheetId: sheet.currentSheetId
                                    onApplied: root.status = "Validation rule saved"
                                    Component.onCompleted: newRule("C2:C5")
                                }
                            }
                        }
                        Tk.Button {
                            objectName: "ruleForSelection"
                            small: true
                            text: "New rule for " + (sheet.selectionAddress || sheet.activeAddress)
                            onClicked: panelSwitch.currentIndex === 0
                                       ? conditionalEditor.newRule(sheet.selectionAddress || sheet.activeAddress)
                                       : validationEditor.newRule(sheet.selectionAddress || sheet.activeAddress)
                        }
                    }
                }
            }
            Tk.Caption {
                objectName: "workbookStatus"
                text: {
                    if (!root.book) return ""
                    const stats = book.statistics
                    return "Revision " + book.revision + (book.modified ? " · modified" : " · saved")
                        + " · " + stats.formulas + " formulas · last recalculation evaluated "
                        + stats.lastEvaluated + " of " + stats.lastDirty + " dirty cells in "
                        + stats.lastRecalcMicroseconds + " µs"
                        + " · " + sheet.grid.visibleCellRecords.length + " visible cell delegates"
                }
            }
        }
    }
}
