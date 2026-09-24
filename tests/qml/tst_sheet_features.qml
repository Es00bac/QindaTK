// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "SheetFeatures"
    when: windowShown
    visible: true
    width: 960
    height: 600

    Component {
        id: fixture
        Item {
            width: 940
            height: 580
            property alias book: book
            property alias sheet: sheet
            property alias formula: formula
            Tk.Workbook { id: book }
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.FormulaBar {
                    id: formula
                    sheetView: sheet
                }
                Tk.SheetView {
                    id: sheet
                    workbook: book
                    tooltip: "Features"
                    Tk.Flex.grow: 1
                }
            }
        }
    }
    Component {
        id: plainFixture
        Item {
            width: 600
            height: 400
            property alias grid: grid
            property alias model: model
            Tk.EditableGridModel { id: model }
            Tk.EditableGrid {
                id: grid
                model: model
                anchors.fill: parent
            }
        }
    }

    function make() {
        const host = createTemporaryObject(fixture, root)
        tryVerify(function() { return host.sheet.hasActiveCell })
        waitForRendering(host)
        return host
    }
    function cellOf(grid, row, column) {
        const m = grid.model
        return findChild(grid, "gridCell_" + m.rowIdAt(row) + "_" + m.columnIdAt(column))
    }
    function typeText(text) {
        for (let i = 0; i < text.length; ++i) keyClick(text[i])
    }
    function drag(from, to) {
        const start = from.mapToItem(root, from.width / 2, from.height / 2)
        const end = to.mapToItem(root, to.width / 2, to.height / 2)
        mousePress(root, start.x, start.y)
        mouseMove(root, (start.x + end.x) / 2, (start.y + end.y) / 2)
        mouseMove(root, end.x, end.y)
        return end
    }

    // A number too wide for its column shows whole # glyphs, never a cut
    // value; text still elides, and a wide enough column shows the number.
    function test_numbers_that_do_not_fit_show_hashes() {
        const host = make()
        const book = host.book, sheet = host.sheet, s = sheet.currentSheetId
        verify(book.pasteInputs(s, 0, 0, [["123456789.25", "a long piece of text"]]))
        verify(book.applyStyle(s, 0, 0, 0, 0, { numberFormat: "#,##0.00" }))
        verify(book.setColumnWidth(s, 0, 40))
        verify(book.setColumnWidth(s, 1, 40))
        // Resizing recreates delegates: look the cells up after the change.
        tryVerify(function() { const c = cellOf(sheet.grid, 0, 0); return c !== null && c.width === 40 && c.hashed })
        const number = cellOf(sheet.grid, 0, 0), text = cellOf(sheet.grid, 0, 1)
        verify(!findChild(number, "gridCellText").visible)
        const hashes = findChild(number, "gridCellHashes")
        tryVerify(function() { return hashes.item !== null && hashes.item.visible })
        const glyphs = hashes.item.children[0]
        verify(glyphs.width > 0 && glyphs.width <= hashes.width)
        compare(Math.round(glyphs.width / glyphs.glyph * 1000) % 1000, 0) // whole glyphs only
        verify(!text.hashed) // text is not a number: it elides
        verify(findChild(text, "gridCellText").truncated)
        compare(number.Accessible.name, "Row 1, column 1: 123,456,789.25") // the value, not ###
        verify(book.setColumnWidth(s, 0, 200))
        tryVerify(function() { return !cellOf(sheet.grid, 0, 0).hashed })
        compare(findChild(cellOf(sheet.grid, 0, 0), "gridCellText").text, "123,456,789.25")
    }

    function test_type_to_edit_and_source_editing() {
        const host = make()
        const book = host.book, sheet = host.sheet, s = sheet.currentSheetId
        verify(book.setCellInput(s, 0, 0, "=1+1"))
        sheet.setCurrent(0, 0)
        sheet.focusGrid()
        keyClick("7") // a printable key replaces the cell's content
        const editor = findChild(sheet.grid, "gridCellEditor")
        tryVerify(function() { return editor.visible && editor.activeFocus })
        compare(editor.text, "7")
        keyClick("5")
        keyClick(Qt.Key_Return)
        compare(book.cellInput(s, 0, 0), "75")
        verify(!sheet.grid.cellEditing) // Return commits; it does not reopen an editor
        verify(sheet.grid.activeFocus)
        verify(book.setCellInput(s, 1, 0, "=A1*2"))
        sheet.setCurrent(1, 0)
        sheet.focusGrid()
        keyClick(Qt.Key_F2) // F2 edits the formula source, not "150"
        tryCompare(editor, "text", "=A1*2")
        keyClick(Qt.Key_Escape)
        compare(book.cellInput(s, 1, 0), "=A1*2")
        sheet.focusGrid()
        keyClick(Qt.Key_B, Qt.ControlModifier) // shortcuts never start an edit
        verify(!sheet.grid.cellEditing)
        compare(book.cellFormat(s, 1, 0).bold, true)
    }

    function test_fill_handle_drag_fills_formulas() {
        const host = make()
        const book = host.book, sheet = host.sheet, s = sheet.currentSheetId
        verify(book.pasteInputs(s, 0, 0, [["1", "=A1*10"], ["2", ""], ["3", ""], ["4", ""]]))
        sheet.setCurrent(0, 1)
        const handle = findChild(sheet.grid, "gridFillHandle")
        tryVerify(function() { return handle.visible })
        compare(handle.Accessible.role, Accessible.Grip)
        const end = drag(handle, cellOf(sheet.grid, 3, 1))
        const preview = findChild(sheet.grid, "gridFillPreview")
        tryVerify(function() { return preview.visible })
        compare(handle.target.firstRow, 1)
        compare(handle.target.lastRow, 3)
        compare(handle.target.firstColumn, 1)
        mouseRelease(root, end.x, end.y)
        tryVerify(function() { return !preview.visible })
        compare(book.cellInput(s, 3, 1), "=A4*10")
        compare(book.cellDisplay(s, 3, 1), "40")
        compare(book.undoText, "Fill cells")
        compare(sheet.selectionAddress, "B1:B4")
        verify(book.undo())
        compare(book.cellInput(s, 3, 1), "")
    }

    function test_merged_cells_render_as_one_and_navigate() {
        const host = make()
        const book = host.book, sheet = host.sheet, s = sheet.currentSheetId
        verify(book.setCellInput(s, 1, 1, "Merged title"))
        sheet.selectRange(1, 1, 2, 3)
        verify(sheet.mergeSelection())
        tryVerify(function() { const c = cellOf(sheet.grid, 1, 1); return c !== null && c.merged })
        const anchor = cellOf(sheet.grid, 1, 1)
        const m = sheet.grid.model
        compare(anchor.width, m.cellRect(1, 1, 0, 0).width + m.cellRect(1, 2, 0, 0).width + m.cellRect(1, 3, 0, 0).width)
        compare(anchor.height, m.cellRect(1, 1, 0, 0).height + m.cellRect(2, 1, 0, 0).height)
        compare(anchor.Accessible.name, "Rows 2–3, columns 2–4: Merged title")
        verify(cellOf(sheet.grid, 2, 2) === null) // covered cells are not laid out
        sheet.setCurrent(1, 0)
        sheet.focusGrid()
        keyClick(Qt.Key_Right)
        compare(sheet.activeAddress, "B2")
        compare(sheet.selectionAddress, "B2:D3")
        keyClick(Qt.Key_Right) // leaves from the far edge
        compare(sheet.activeAddress, "E2")
        keyClick(Qt.Key_Left)
        compare(sheet.activeAddress, "B2")
        keyClick(Qt.Key_Down)
        compare(sheet.activeAddress, "B4")
        sheet.setCurrent(2, 3) // a covered position selects the anchor
        compare(sheet.activeAddress, "B2")
        sheet.focusGrid()
        keyClick(Qt.Key_F2)
        const editor = findChild(sheet.grid, "gridCellEditor")
        tryVerify(function() { return editor.visible })
        compare(editor.width, anchor.width)
        compare(editor.height, anchor.height)
        keyClick(Qt.Key_Escape)
        verify(sheet.unmergeSelection())
        tryVerify(function() { return cellOf(sheet.grid, 2, 2) !== null })
    }

    function test_validation_prompts_and_input_message() {
        const host = make()
        const book = host.book, sheet = host.sheet, formula = host.formula, s = sheet.currentSheetId
        const id = book.setValidation(s, { ranges: ["A1:A5"], type: "wholeNumber", operator: "lessOrEqual",
                                           value1: "10", errorStyle: "warning", errorTitle: "Large quantity",
                                           errorMessage: "More than 10 is unusual", inputTitle: "Quantity",
                                           inputMessage: "Enter 1 to 10" })
        verify(id.length > 0)
        sheet.setCurrent(0, 0)
        const message = findChild(sheet, "sheetInputMessage")
        tryVerify(function() { return message.visible })
        compare(findChild(message, "sheetInputMessageTitle").text, "Quantity")
        compare(findChild(message, "sheetInputMessageText").text, "Enter 1 to 10")
        compare(message.Accessible.name, "Quantity. Enter 1 to 10")

        sheet.focusGrid()
        typeText("12")
        keyClick(Qt.Key_Return)
        const dialog = findChild(sheet, "sheetValidationPrompt")
        tryVerify(function() { return dialog.opened })
        compare(dialog.title, "Large quantity")
        verify(sheet.validationPending)
        verify(!message.visible)
        compare(book.cellInput(s, 0, 0), "") // nothing applied before the answer
        mouseClick(findChild(dialog.footer, "dialogSecondary")) // No: keep editing
        tryVerify(function() { return !dialog.visible })
        const editor = findChild(sheet.grid, "gridCellEditor")
        tryVerify(function() { return editor.activeFocus })
        compare(editor.text, "12")
        keyClick(Qt.Key_Return)
        tryVerify(function() { return dialog.opened })
        mouseClick(findChild(dialog.footer, "dialogPrimary")) // Yes: keep the value
        tryVerify(function() { return !sheet.grid.cellEditing })
        compare(book.cellInput(s, 0, 0), "12")
        verify(!sheet.validationPending)

        // The formula bar asks the same question and finishes on the answer.
        sheet.setCurrent(2, 0)
        const formulaEditor = findChild(formula, "formulaEditor")
        verify(formula.beginEdit())
        formulaEditor.text = "15"
        verify(!formula.commit(true))
        tryVerify(function() { return dialog.opened })
        verify(formula.editing)
        compare(formula.errorText, "")
        mouseClick(findChild(dialog.footer, "dialogTertiary")) // Cancel: drop the entry
        tryVerify(function() { return !formula.editing })
        compare(book.cellInput(s, 2, 0), "")
        verify(formula.beginEdit())
        formulaEditor.text = "15"
        verify(!formula.commit(true))
        tryVerify(function() { return dialog.opened })
        mouseClick(findChild(dialog.footer, "dialogPrimary"))
        tryVerify(function() { return !formula.editing })
        compare(book.cellInput(s, 2, 0), "15")

        // A stop rule refuses with its message and keeps the grid editor open.
        compare(book.setValidation(s, { id: id, ranges: ["A1:A5"], type: "wholeNumber", operator: "lessOrEqual",
                                        value1: "10", errorMessage: "Too many" }), id)
        sheet.setCurrent(1, 0)
        sheet.focusGrid()
        typeText("99")
        keyClick(Qt.Key_Return)
        verify(sheet.grid.cellEditing)
        compare(sheet.errorText, "A2: Too many")
        verify(!dialog.opened)
        keyClick(Qt.Key_Escape)
        compare(book.cellInput(s, 1, 0), "")
    }

    function test_list_validation_enters_a_value() {
        const host = make()
        const book = host.book, sheet = host.sheet, s = sheet.currentSheetId
        verify(book.setValidation(s, { ranges: ["B1:B3"], type: "list", items: ["Red", "Green", "Blue"] }).length > 0)
        sheet.setCurrent(0, 0)
        const button = findChild(sheet, "sheetListButton")
        verify(!button.visible)
        sheet.setCurrent(0, 1)
        tryVerify(function() { return button.visible })
        compare(button.tooltip, "Choose a value for B1 (Alt+Down)")
        mouseClick(button)
        const menu = findChild(sheet, "sheetListMenu")
        tryVerify(function() { return menu.opened })
        compare(menu.count, 3)
        mouseClick(menu.itemAt(1))
        tryVerify(function() { return !menu.visible })
        compare(book.cellInput(s, 0, 1), "Green")
        sheet.focusGrid()
        keyClick(Qt.Key_Down, Qt.AltModifier)
        tryVerify(function() { return menu.opened })
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !menu.visible })
    }

    function test_formatting_freeze_and_protection() {
        const host = make()
        const book = host.book, sheet = host.sheet, s = sheet.currentSheetId
        verify(book.setCellInput(s, 0, 0, "1234.5"))
        sheet.setCurrent(0, 0)
        verify(sheet.applyStyle({ numberFormat: "#,##0.00", fillColor: "#ffeeaa" }))
        tryVerify(function() {
            const c = cellOf(sheet.grid, 0, 0)
            return c !== null && findChild(c, "gridCellText").text === "1,234.50"
        })
        let c = cellOf(sheet.grid, 0, 0)
        compare(findChild(c, "gridCellText").horizontalAlignment, Text.AlignRight)
        verify(Qt.colorEqual(findChild(c, "gridCellBackground").color, "#ffeeaa"))
        sheet.focusGrid()
        keyClick(Qt.Key_B, Qt.ControlModifier)
        tryVerify(function() { return findChild(cellOf(sheet.grid, 0, 0), "gridCellText").font.bold })
        compare(sheet.activeFormat.bold, true)
        keyClick(Qt.Key_B, Qt.ControlModifier)
        compare(sheet.activeFormat.bold, false)

        sheet.setCurrent(2, 2)
        verify(sheet.freezePanes())
        compare(sheet.grid.model.frozenRows, 2)
        compare(sheet.grid.model.frozenColumns, 2)
        verify(book.undo())
        compare(sheet.grid.model.frozenRows, 0)

        verify(book.setSheetProperties(s, { protected: true }))
        verify(sheet.readOnly)
        sheet.setCurrent(0, 0)
        verify(!sheet.beginEdit())
        verify(!findChild(sheet.grid, "gridFillHandle").visible)
        verify(!sheet.applyStyle({ bold: true }))
        verify(sheet.errorText.indexOf("protected") >= 0)
        verify(book.setSheetProperties(s, { protected: false }))
        verify(sheet.beginEdit())
        keyClick(Qt.Key_Escape)
    }

    function test_plain_grid_fill_handle_repeats_values() {
        const host = createTemporaryObject(plainFixture, root)
        const grid = host.grid, model = host.model
        verify(model.setDimensions(10, 4))
        verify(model.paste(0, 0, [["a"], ["b"]]))
        grid.setCurrent(0, 0, false)
        grid.setCurrent(1, 0, true)
        waitForRendering(host)
        const handle = findChild(grid, "gridFillHandle")
        tryVerify(function() { return handle.visible })
        const end = drag(handle, cellOf(grid, 5, 0))
        mouseRelease(root, end.x, end.y)
        compare(model.cellValue(model.rowIdAt(2), model.columnIdAt(0)), "a")
        compare(model.cellValue(model.rowIdAt(5), model.columnIdAt(0)), "b")
        verify(model.canUndo)
        verify(model.undo())
        compare(model.cellValue(model.rowIdAt(5), model.columnIdAt(0)), undefined)
    }
}
