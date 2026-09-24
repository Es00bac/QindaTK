// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/workbook/Main.qml the way a user would: switch sheets,
// edit through the formula bar, fill a formula, undo, save and reopen.
TestCase {
    id: root
    name: "WorkbookExample"
    when: windowShown
    visible: true
    width: 1440
    height: 820

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("../../examples/workbook/Main.qml")
    }

    function part(name) {
        const item = findChild(loader.item, name)
        verify(item !== null, name)
        return item
    }

    function test_user_flow() {
        tryCompare(loader, "status", Loader.Ready)
        waitForRendering(loader.item)
        const sheet = part("sheetView"), formula = part("formulaBar")
        const book = sheet.workbook
        compare(sheet.activeAddress, "F2")
        compare(part("formulaEditor").text, "=SUM(B2:E2)")
        compare(formula.result, "4,900.00") // the example formats B2:F6 as #,##0.00
        compare(sheet.selectionAddress, "F2:F5")

        mouseClick(part("fillDownButton"))
        compare(book.cellInput(book.sheetIdAt(0), 4, 5), "=SUM(B5:E5)")
        compare(book.cellDisplay(book.sheetIdAt(0), 4, 5), "2,350.00")
        compare(book.undoText, "Fill cells")

        mouseClick(part("workbookTab_sheet-2"))
        tryCompare(sheet, "currentSheetId", "sheet-2")
        sheet.setCurrent(3, 1)
        compare(part("formulaEditor").text, "=AVERAGE(Totals)") // the workbook name Totals is Budget!F2:F5
        compare(formula.result, "7,718.88")

        mouseClick(part("undoButton"))
        compare(formula.result, "4,900.00")
        mouseClick(part("redoButton"))
        compare(formula.result, "7,718.88")

        mouseClick(part("formulaEditor"))
        verify(formula.editing)
        part("formulaEditor").selectAll()
        keyClick("4")
        keyClick("2")
        keyClick(Qt.Key_Return)
        compare(book.cellInput("sheet-2", 3, 1), "42")

        mouseClick(part("saveButton"))
        verify(!book.modified)
        verify(book.setCellInput("sheet-2", 3, 1, "changed"))
        verify(book.modified)
        mouseClick(part("reopenButton"))
        compare(book.cellInput("sheet-2", 3, 1), "42")
        compare(book.cellInput(book.sheetIdAt(0), 4, 5), "=SUM(B5:E5)")
        verify(!book.canUndo)
        verify(!book.modified)
        tryCompare(sheet, "activeAddress", "B4")
        compare(formula.result, "42.00")
        verify(part("workbookStatus").text.indexOf("saved") > 0)
    }
}
