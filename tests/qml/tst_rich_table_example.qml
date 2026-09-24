// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/rich-table/Main.qml the way a user would: the TableEditor's
// cell selection moves the page caret, its requests edit the paginated
// document one undo step at a time, a removed cell hands the selection to its
// neighbour, and a refused request shows the document's reason.
TestCase {
    id: root
    name: "RichTableExample"
    when: windowShown
    visible: true
    width: 1180
    height: 760

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("../../examples/rich-table/Main.qml")
    }

    function part(name) {
        const item = findChild(loader.item, name)
        verify(item !== null, name)
        return item
    }

    // The structure buttons sit above the cell grid, the text field below it.
    function clickStructure(name) {
        part("richTableEditorScroll").scrollToTop()
        mouseClick(part(name))
    }

    function clickCell(editor, row, column) {
        const button = part("tableCell_" + editor.cellAt(row, column).id)
        tryVerify(function() { return button.y > 0 }) // the grid has placed its rows
        mouseClick(button)
    }

    function submitText(text) {
        const field = part("tableCellText")
        field.forceActiveFocus()
        field.text = text
        keyClick(Qt.Key_Return)
    }

    function test_user_flow() {
        tryCompare(loader, "status", Loader.Ready)
        const example = loader.item
        const editor = part("liveTableEditor"), page = part("richTableDocument")
        tryCompare(editor.table.rows, "length", 30)
        tryVerify(function() { return example.revealed }) // the first cell is shown on the page
        compare(example.selectedCellId, editor.cellAt(0, 0).id)
        compare(page.area.caretParagraphId, editor.cellAt(0, 0).paragraphIds[0])
        verify(!part("tableMergeCells").enabled)
        verify(!part("richTableUndo").enabled)

        clickCell(editor, 5, 1)
        const reading = editor.cellAt(5, 1)
        compare(example.selectedCellId, reading.id)
        compare(page.area.caretParagraphId, reading.paragraphIds[0])
        submitText("21.30")
        compare(editor.cellAt(5, 1).text, "21.30")
        clickStructure("tableInsertRow")
        compare(editor.table.rows.length, 31)
        compare(editor.cellAt(6, 1).text, "") // after the selected row
        verify(part("richTableStatus").text.startsWith("31 rows"))

        // Deleting the selected column moves the page caret to the next
        // column of the same row, and the editor follows it.
        clickStructure("tableDeleteColumn")
        compare(editor.table.columns.length, 3)
        compare(example.selectedCellId, editor.cellAt(5, 1).id)
        compare(editor.cellAt(5, 1).text, "مرحبا")

        const undo = part("richTableUndo")
        compare(undo.tooltip, "Undo Delete table column")
        mouseClick(undo)
        compare(editor.table.columns.length, 4)
        mouseClick(undo)
        compare(editor.table.rows.length, 30)
        mouseClick(undo)
        compare(editor.cellAt(5, 1).text, "21.25")

        // A cell split into two paragraphs in the page is refused here.
        const site = editor.cellAt(2, 0)
        verify(page.area.goToParagraph(site.paragraphIds[0], 2))
        compare(example.selectedCellId, site.id)
        verify(page.area.insertText("\n"))
        submitText("S-02b")
        const reason = part("tableStructureStatus")
        verify(reason.visible)
        compare(reason.text, "Unknown or multi-paragraph table cell")
        compare(editor.cellAt(2, 0).text, "S-\n02")
    }
}
