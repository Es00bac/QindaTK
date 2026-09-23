// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "WorkbookViews"
    when: windowShown
    visible: true
    width: 960
    height: 560

    Component {
        id: fixture
        Item {
            width: 940
            height: 540
            property alias book: book
            property alias sheet: sheet
            property alias formula: formula
            property alias tabs: tabs
            Tk.Workbook { id: book }
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.WorkbookTabs {
                    id: tabs
                    workbook: book
                    currentSheetId: sheet.currentSheetId
                    onSheetActivated: function(sheetId) { sheet.sheetId = sheetId }
                }
                Tk.FormulaBar {
                    id: formula
                    sheetView: sheet
                }
                Tk.SheetView {
                    id: sheet
                    workbook: book
                    tooltip: "Test workbook"
                    Tk.Flex.grow: 1
                }
            }
        }
    }

    function seeded() {
        const host = createTemporaryObject(fixture, root)
        const book = host.book
        verify(book.renameSheet(book.sheetIdAt(0), "Data"))
        compare(book.addSheet("Report", -1, 50, 8), "sheet-2")
        verify(book.pasteInputs("sheet-1", 0, 0, [["1", "=A1*2"], ["2", "=A2*2"], ["3", ""]]))
        verify(book.pasteInputs("sheet-2", 0, 0, [["=SUM(Data!B1:B3)", "report"]]))
        verify(book.restoreText(book.serializedText())) // reopen: clean history
        tryVerify(function() { return host.sheet.hasActiveCell })
        waitForRendering(host) // rebuilt tab delegates are laid out before clicks
        return host
    }
    function typeText(text) {
        for (let i = 0; i < text.length; ++i) keyClick(text[i])
    }

    function test_tabs_follow_the_workbook_by_stable_id() {
        const host = seeded()
        const book = host.book, sheet = host.sheet, tabs = host.tabs
        compare(sheet.currentSheetId, "sheet-1")
        mouseClick(findChild(tabs, "workbookTab_sheet-2"))
        tryCompare(sheet, "currentSheetId", "sheet-2")
        compare(sheet.grid.model.rowCount, 50)
        compare(sheet.grid.model.cellValue("r:1", "c:1"), "6")
        verify(findChild(sheet, "gridCell_r:1_c:2") !== null)

        verify(tabs.beginRename("sheet-1"))
        const editor = findChild(findChild(tabs, "workbookTabSlot_sheet-1"), "workbookTabEditor")
        tryVerify(function() { return editor.visible && editor.activeFocus })
        compare(editor.text, "Data")
        editor.text = "Input"
        keyClick(Qt.Key_Return)
        compare(book.sheetName("sheet-1"), "Input")
        compare(book.cellInput("sheet-2", 0, 0), "=SUM(Input!B1:B3)")
        compare(sheet.grid.model.cellValue("r:1", "c:1"), "6")

        findChild(tabs, "workbookTab_sheet-1").forceActiveFocus()
        keyClick(Qt.Key_Right, Qt.ControlModifier | Qt.ShiftModifier)
        compare(book.sheetIdAt(1), "sheet-1")
        compare(tabs.sheets[0].name, "Report")
        compare(tabs.addSheet(), "sheet-3")
        tryCompare(sheet, "currentSheetId", "sheet-3")
        verify(book.undo())
        tryCompare(sheet, "currentSheetId", "sheet-2")
        verify(book.undo() && book.undo())
        compare(book.sheetName("sheet-1"), "Data")
        compare(book.cellInput("sheet-2", 0, 0), "=SUM(Data!B1:B3)")
        let requested = ""
        tabs.removeRequested.connect(function(sheetId) { requested = sheetId })
        verify(tabs.requestRemove("sheet-2"))
        compare(requested, "sheet-2")
        compare(book.sheetCount, 2)
    }

    function test_formula_bar_edits_source_not_value() {
        const host = seeded()
        const book = host.book, sheet = host.sheet, formula = host.formula
        sheet.setCurrent(0, 1)
        const editor = findChild(formula, "formulaEditor")
        compare(formula.address, "B1")
        compare(editor.text, "=A1*2")
        compare(formula.result, "2")
        compare(findChild(formula, "formulaResult").text, "= 2")
        compare(findChild(formula, "formulaNameBox").text, "B1")
        mouseClick(editor)
        verify(formula.editing)
        editor.selectAll()
        typeText("=a1+10")
        keyClick(Qt.Key_Return)
        verify(!formula.editing)
        compare(book.cellInput("sheet-1", 0, 1), "=A1+10")
        compare(formula.result, "11")
        compare(editor.text, "=A1+10")
        compare(sheet.grid.model.cellValue("r:1", "c:2"), "11")
        compare(sheet.activeAddress, "B1")
        compare(book.undoText, "Edit B1")
    }

    function test_formula_error_span_and_cancel_restore_prior_input() {
        const host = seeded()
        const book = host.book, sheet = host.sheet, formula = host.formula
        sheet.setCurrent(0, 1)
        const editor = findChild(formula, "formulaEditor")
        verify(formula.beginEdit())
        editor.text = "=SUM(A1,,2)"
        verify(!formula.commit(true))
        verify(formula.editing)
        compare(formula.errorStart, 8)
        compare(formula.errorEnd, 9)
        compare(editor.selectedText, ",")
        const error = findChild(formula, "formulaError")
        verify(error.visible)
        verify(error.text.indexOf("“,”") > 0)
        compare(book.cellInput("sheet-1", 0, 1), "=A1*2")
        verify(editor.activeFocus)
        keyClick(Qt.Key_Escape)
        verify(!formula.editing)
        compare(editor.text, "=A1*2")
        verify(!error.visible)
        verify(!book.canUndo)
    }

    function test_point_mode_inserts_references_including_other_sheets() {
        const host = seeded()
        const book = host.book, sheet = host.sheet, formula = host.formula
        const editor = findChild(formula, "formulaEditor")
        sheet.setCurrent(2, 1)
        mouseClick(editor)
        typeText("=SUM(")
        compare(formula.functionHint, "SUM(value1, [value2], ...)")
        sheet.setCurrent(1, 1) // API moves are not pointing gestures
        compare(editor.text, "=SUM(")
        mouseClick(findChild(sheet.grid, "gridCell_r:1_c:1"))
        compare(editor.text, "=SUM(A1")
        mouseClick(findChild(sheet.grid, "gridCell_r:2_c:1"), 4, 4, Qt.LeftButton, Qt.ShiftModifier)
        compare(editor.text, "=SUM(A1:A2")
        compare(formula.targetAddress, "B3")
        tryVerify(function() { return editor.activeFocus })
        keyClick(")")
        compare(formula.referenceSpans.length, 1)
        keyClick(Qt.Key_Return)
        compare(book.cellInput("sheet-1", 2, 1), "=SUM(A1:A2)")
        compare(book.cellDisplay("sheet-1", 2, 1), "3")
        compare(sheet.activeAddress, "B3")

        sheet.setCurrent(3, 1)
        verify(formula.beginEdit())
        editor.text = "="
        sheet.sheetId = "sheet-2"
        tryCompare(editor, "text", "=Report!A1")
        verify(formula.commit(true))
        tryCompare(sheet, "currentSheetId", "sheet-1")
        compare(book.cellInput("sheet-1", 3, 1), "=Report!A1")
        compare(book.cellDisplay("sheet-1", 3, 1), "9")
        compare(sheet.activeAddress, "B4")
    }

    function test_cell_editor_shows_source_and_commits_to_workbook() {
        const host = seeded()
        const book = host.book, sheet = host.sheet
        sheet.setCurrent(0, 1)
        sheet.focusGrid()
        keyClick(Qt.Key_F2)
        const cellEditor = findChild(sheet.grid, "gridCellEditor")
        tryVerify(function() { return cellEditor.visible })
        compare(cellEditor.text, "=A1*2")
        cellEditor.text = "=A1*3"
        keyClick(Qt.Key_Return)
        compare(book.cellInput("sheet-1", 0, 1), "=A1*3")
        compare(sheet.grid.model.cellValue("r:1", "c:2"), "3")
        verify(!sheet.grid.model.canUndo)
        // Typing the displayed value over a formula still replaces the formula.
        sheet.focusGrid()
        keyClick(Qt.Key_F2)
        tryVerify(function() { return cellEditor.visible })
        cellEditor.text = "3"
        keyClick(Qt.Key_Return)
        compare(book.cellInput("sheet-1", 0, 1), "3")
        compare(book.cellType("sheet-1", 0, 1), "number")
    }

    function test_fill_and_history_shortcuts() {
        const host = seeded()
        const book = host.book, sheet = host.sheet
        sheet.selectRange(0, 1, 2, 1)
        verify(sheet.fillDown())
        compare(book.cellInput("sheet-1", 2, 1), "=A3*2")
        compare(book.cellDisplay("sheet-1", 2, 1), "6")
        compare(book.undoText, "Fill cells")
        sheet.focusGrid()
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(book.cellType("sheet-1", 2, 1), "blank")
        keyClick(Qt.Key_Y, Qt.ControlModifier)
        compare(book.cellInput("sheet-1", 2, 1), "=A3*2")
        verify(book.undo())
        sheet.selectRange(0, 1, 2, 1)
        sheet.focusGrid()
        keyClick(Qt.Key_D, Qt.ControlModifier)
        compare(book.cellInput("sheet-1", 2, 1), "=A3*2")
        sheet.selectRange(0, 1, 0, 3)
        sheet.focusGrid()
        keyClick(Qt.Key_R, Qt.ControlModifier)
        compare(book.cellInput("sheet-1", 0, 3), "=C1*2")
        compare(book.cellDisplay("sheet-1", 0, 3), "8")
    }

    function test_copy_paste_and_delete_keys() {
        const host = seeded()
        const book = host.book, sheet = host.sheet
        sheet.setCurrent(0, 1)
        sheet.focusGrid()
        keyClick(Qt.Key_C, Qt.ControlModifier)
        compare(sheet.adapter.copiedRange, "B1")
        sheet.setCurrent(4, 2)
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(book.cellInput("sheet-1", 4, 2), "=B5*2")
        compare(book.undoText, "Copy cells")
        keyClick(Qt.Key_Delete)
        compare(book.cellType("sheet-1", 4, 2), "blank")
        compare(book.undoText, "Clear cells")
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(book.cellInput("sheet-1", 4, 2), "=B5*2")
    }

    function test_paste_text_is_atomic() {
        const host = seeded()
        const book = host.book, sheet = host.sheet
        sheet.setCurrent(5, 0)
        const revision = book.revision
        verify(!sheet.pasteText("7\t=1+\n8\t9"))
        compare(book.revision, revision)
        compare(book.cellType("sheet-1", 5, 0), "blank")
        compare(sheet.errorText.indexOf("B6: "), 0)
        compare(sheet.errorDetail.start, 3)
        verify(sheet.pasteText("7\t=A6*2\n8\t9\n"))
        compare(book.cellDisplay("sheet-1", 5, 1), "14")
        compare(book.cellDisplay("sheet-1", 6, 1), "9")
        compare(book.undoText, "Paste cells")
        verify(book.undo())
        compare(book.cellType("sheet-1", 5, 0), "blank")
    }

    function test_reopen_restores_saved_state_in_the_views() {
        const host = seeded()
        const book = host.book, sheet = host.sheet, formula = host.formula
        const saved = book.serializedText()
        sheet.setCurrent(0, 1)
        verify(book.setCellInput("sheet-1", 0, 0, "100"))
        compare(formula.result, "200")
        compare(sheet.grid.model.cellValue("r:1", "c:2"), "200")
        verify(book.restoreText(saved))
        compare(book.serializedText(), saved)
        compare(sheet.grid.model.cellValue("r:1", "c:2"), "2")
        compare(formula.result, "2")
        verify(!book.canUndo)
        verify(!book.modified)
    }

    function test_visible_work_is_bounded_and_accessible() {
        const host = seeded()
        const sheet = host.sheet, formula = host.formula, tabs = host.tabs
        compare(sheet.grid.model.rowCount, 1000)
        verify(sheet.grid.visibleCellRecords.length > 0)
        verify(sheet.grid.visibleCellRecords.length < 600)
        sheet.adapter.resetSourceReads()
        sheet.grid.dataEpoch += 1
        // Every visible cell once, plus the table's accessible description of the active cell.
        const reads = sheet.adapter.sourceReadCount()
        verify(reads >= sheet.grid.visibleCellRecords.length)
        verify(reads <= sheet.grid.visibleCellRecords.length + 1)
        compare(sheet.Accessible.role, Accessible.Pane)
        compare(sheet.Accessible.name, "Test workbook, sheet Data")
        verify(sheet.Accessible.description.indexOf("Cell A1") === 0)
        compare(sheet.grid.Accessible.role, Accessible.Table)
        compare(sheet.grid.Accessible.name, "Sheet Data")
        compare(formula.Accessible.role, Accessible.ToolBar)
        compare(findChild(formula, "formulaResult").Accessible.role, Accessible.StaticText)
        compare(findChild(formula, "formulaEditor").Accessible.role, Accessible.EditableText)
        compare(tabs.Accessible.role, Accessible.PageTabList)
        compare(findChild(tabs, "workbookTab_sheet-1").Accessible.role, Accessible.PageTab)
        verify(findChild(tabs, "workbookTab_sheet-1").Accessible.selected)
        verify(findChild(tabs, "workbookTabAdd") !== null)
    }
}
