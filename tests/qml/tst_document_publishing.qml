// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "DocumentPublishing"
    when: windowShown
    visible: true
    width: 720
    height: 420

    Component {
        id: tableComponent
        Tk.TableEditor {
            width: 620
            table: ({
                id: "table-1", rows: [{ id: "row-a", label: "Row 1" }],
                columns: [{ id: "col-a", label: "A", width: 40 }, { id: "col-b", label: "B", width: 40 }],
                cells: [
                    { id: "cell-a", rowId: "row-a", columnId: "col-a", rowIndex: 0, columnIndex: 0, text: "A", rowSpan: 1, columnSpan: 1 },
                    { id: "cell-b", rowId: "row-a", columnId: "col-b", rowIndex: 0, columnIndex: 1, text: "B", rowSpan: 1, columnSpan: 1 }
                ]
            })
            selectedRowId: "row-a"
            selectedColumnId: "col-a"
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    // A TableEditor wired to a real rich-text document, as in examples/rich-table.
    Tk.RichTextDocument { id: tableDocument }
    Component {
        id: liveTableComponent
        Tk.TableEditor {
            id: liveEditor
            required property Tk.RichTextDocument document
            property string pickedCellId: ""
            function pickedField(field) {
                const cell = liveEditor.cellById(liveEditor.pickedCellId)
                return cell ? cell[field] : ""
            }
            width: 620
            table: liveEditor.document.tables.length ? liveEditor.document.tables[0]
                                                     : ({ id: "", rows: [], columns: [], cells: [] })
            selectedCellIds: liveEditor.pickedCellId ? [liveEditor.pickedCellId] : []
            selectedRowId: liveEditor.pickedField("rowId")
            selectedColumnId: liveEditor.pickedField("columnId")
            onCellSelectionRequested: function(id, extend) { liveEditor.pickedCellId = id }
            onCellTextChangeRequested: function(id, text) { liveEditor.document.setTableCellText(liveEditor.table.id, id, text) }
            onRowInsertRequested: function(index) { liveEditor.document.insertTableRow(liveEditor.table.id, index) }
            onColumnDeleteRequested: function(id) { liveEditor.document.removeTableColumn(liveEditor.table.id, id) }
        }
    }

    // Written before tables stored columnIds: the document derives them.
    function tableJson() {
        const uuid = n => "00000000-0000-4000-8000-" + n.toString(16).padStart(12, "0")
        const cell = (n, text) => ({ id: uuid(n), paragraphs: [{ id: uuid(n + 100), text: text, spans: [[text.length, 0]], mark: 0 }] })
        return JSON.stringify({ schema: "qindatk.richtext", version: 1, documentId: uuid(999),
                                page: { widthPt: 372, heightPt: 432, marginsPt: [36, 36, 36, 36] },
                                defaults: { family: "sans-serif", sizePt: 11 },
                                formats: [{}], lists: [], objects: [],
                                blocks: [{ paragraph: { id: uuid(1), text: "Before", spans: [[6, 0]], mark: 0 } },
                                         { table: { id: uuid(2), columns: [1, 1], headerRows: 1, rows: [
                                             { id: uuid(3), cells: [cell(10, "Name"), cell(11, "Size")] },
                                             { id: uuid(4), cells: [cell(12, "Paper"), cell(13, "A4")] }] } },
                                         { paragraph: { id: uuid(5), text: "", spans: [], mark: 0 } }] })
    }

    function test_add_row_emits_insertion_position_without_mutating_snapshot() {
        const editor = createTemporaryObject(tableComponent, this)
        verify(editor !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: editor, signalName: "rowInsertRequested" })
        const insert = findChild(editor, "tableInsertRow")
        verify(insert !== null && insert.enabled)

        insert.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], 1)
        compare(editor.table.rows.length, 1)
    }

    function test_editor_requests_edit_the_rich_text_document() {
        verify(tableDocument.loadJson(tableJson()))
        const editor = createTemporaryObject(liveTableComponent, this, { document: tableDocument })
        verify(editor !== null)
        compare(editor.table.rows.length, 2)
        compare(editor.table.columns.length, 2)
        verify(editor.table.columns[0].id.length > 0 && editor.table.columns[0].id !== editor.table.columns[1].id)
        const paper = editor.cellAt(1, 0)
        const rowId = paper.rowId
        compare(paper.text, "Paper")
        const paperButton = findChild(editor, "tableCell_" + paper.id)
        tryVerify(function() { return paperButton.y > 0 }) // the grid has placed row 1
        mouseClick(paperButton)
        compare(editor.selectedCellIds[0], paper.id)
        const field = findChild(editor, "tableCellText")
        compare(field.text, "Paper")
        field.forceActiveFocus()
        field.text = "Card"
        keyClick(Qt.Key_Return)
        compare(editor.cellAt(1, 0).text, "Card")
        compare(tableDocument.undoText, "Edit table cell")
        mouseClick(findChild(editor, "tableInsertRow"))
        compare(editor.table.rows.length, 3)
        compare(editor.table.rows[1].id, rowId)          // identities survive the edit
        compare(editor.cellAt(1, 0).id, paper.id)
        compare(editor.cellAt(2, 0).text, "")            // inserted after the selected row
        compare(tableDocument.undoText, "Insert table row")
        verify(!findChild(editor, "tableMergeCells").enabled) // no spans in the model
        mouseClick(findChild(editor, "tableDeleteColumn"))
        compare(editor.table.columns.length, 1)
        verify(tableDocument.undo())
        compare(editor.table.columns.length, 2)
        compare(editor.cellAt(1, 0).text, "Card")
        verify(tableDocument.undo())
        compare(editor.table.rows.length, 2)
        verify(tableDocument.undo())
        compare(editor.cellAt(1, 0).text, "Paper")
    }
}
