// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Structured table editing (WP-051): Tk.TableEditor bound to the paginated
// Tk.RichTextDocument loaded from the rich-document fixture. Row, column,
// cell-text and width requests call the document's table methods, so each
// one is a document undo step and the page reflows at once. Selecting a cell
// reveals it on the page; moving the page caret into a cell selects it in
// the editor. Merge stays disabled: the model has no spans (canMerge false).
// Run: qtk-preview examples/rich-table/Main.qml --size 1180x760
Rectangle {
    id: root
    width: 1180
    height: 760
    color: Tk.Theme.color.bg

    readonly property var emptyTable: ({ id: "", rows: [], columns: [], cells: [], canMerge: false })
    // The document's first table as a TableEditor snapshot with stable row,
    // column and cell IDs; rebuilt on every document change.
    readonly property var snapshot: {
        const tables = document.tables
        return tables.length ? tables[0] : emptyTable
    }
    property string selectedCellId: ""
    property string selectedRowId: ""
    property string selectedColumnId: ""
    property int selectedRowIndex: 0
    property int selectedColumnIndex: 0
    property string requestError: ""
    property bool revealed: false

    function cellWhere(test) {
        const cells = root.snapshot.cells
        for (let i = 0; i < cells.length; ++i)
            if (test(cells[i])) return cells[i]
        return null
    }
    function select(cell) {
        root.selectedCellId = cell.id
        root.selectedRowId = cell.rowId
        root.selectedColumnId = cell.columnId
        root.selectedRowIndex = cell.rowIndex
        root.selectedColumnIndex = cell.columnIndex
    }
    // Shows the selected cell on the page and puts the page caret in it.
    function reveal() {
        const cell = root.cellWhere(c => c.id === root.selectedCellId)
        if (cell && page.height > 0) root.revealed = page.area.goToParagraph(cell.paragraphIds[0], 0)
    }
    // Keeps a selection when rows or columns disappear: the same cell, else
    // the cell now at its row and column, clamped to the grid.
    function ensureSelection() {
        const rows = root.snapshot.rows.length
        const columns = root.snapshot.columns.length
        if (!rows || !columns) return
        const row = Math.min(root.selectedRowIndex, rows - 1)
        const column = Math.min(root.selectedColumnIndex, columns - 1)
        const cell = root.cellWhere(c => c.id === root.selectedCellId)
            || root.cellWhere(c => c.rowIndex === row && c.columnIndex === column)
        if (cell) root.select(cell)
        if (!root.revealed) root.reveal()
    }
    // A refused request shows the document's reason in the editor's status line.
    function request(accepted) {
        root.requestError = accepted ? "" : document.lastError
    }

    Tk.RichTextDocument {
        id: document
        Component.onCompleted: {
            loadFromFile(Qt.resolvedUrl("../rich-document/showcase.qtkrich"))
            Qt.callLater(root.ensureSelection)
        }
        onChanged: Qt.callLater(root.ensureSelection)
    }
    Connections {
        target: page.area
        // The page caret entering a cell selects that cell in the editor.
        function onCaretChanged() {
            const id = page.area.caretParagraphId
            const cell = root.cellWhere(c => c.paragraphIds.indexOf(id) >= 0)
            if (cell && cell.id !== root.selectedCellId) root.select(cell)
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            padding: Tk.Theme.space.sm
            gap: Tk.Theme.space.sm
            Tk.Flex.shrink: 0
            Tk.Heading { text: qsTr("Structured table editing"); Tk.Flex.grow: 1 }
            Tk.Caption {
                objectName: "richTableStatus"
                text: qsTr("%1 rows · %2 columns · page %3/%4")
                      .arg(root.snapshot.rows.length).arg(root.snapshot.columns.length)
                      .arg(page.currentPage + 1).arg(page.pageCount)
            }
            Tk.IconButton {
                objectName: "richTableUndo"
                iconName: "undo-2"
                tooltip: document.canUndo ? qsTr("Undo %1").arg(document.undoText) : qsTr("Undo")
                enabled: document.canUndo
                onClicked: document.undo()
            }
            Tk.IconButton {
                objectName: "richTableRedo"
                iconName: "redo-2"
                tooltip: document.canRedo ? qsTr("Redo %1").arg(document.redoText) : qsTr("Redo")
                enabled: document.canRedo
                onClicked: document.redo()
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.RichDocumentView {
                id: page
                objectName: "richTableDocument"
                document: document
                zoom: 0.78
                tooltip: qsTr("Editable paginated document")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                // The first selection is made before the view has its size.
                onHeightChanged: if (!root.revealed) root.reveal()
            }
            Tk.Panel {
                title: qsTr("TABLE EDITOR")
                implicitWidth: 380
                Tk.Flex.shrink: 0
                Tk.Scroll {
                    objectName: "richTableEditorScroll"
                    anchors.fill: parent
                    accessibleName: qsTr("Table editing controls")
                    Tk.TableEditor {
                        objectName: "liveTableEditor"
                        // Anchored, so Scroll keeps this width: a gutter for
                        // the overlay scroll bar beside the cell grid.
                        anchors.left: parent.left
                        anchors.top: parent.top
                        width: parent.width - Tk.Theme.size.scrollbar - Tk.Theme.space.xs
                        table: Object.assign({ validationMessage: root.requestError }, root.snapshot)
                        selectedCellIds: root.selectedCellId ? [root.selectedCellId] : []
                        selectedRowId: root.selectedRowId
                        selectedColumnId: root.selectedColumnId
                        onCellSelectionRequested: function(id, extend) {
                            const cell = root.cellWhere(c => c.id === id)
                            if (!cell) return
                            root.select(cell)
                            root.reveal()
                        }
                        onCellTextChangeRequested: function(id, text) {
                            root.request(document.setTableCellText(root.snapshot.id, id, text))
                        }
                        onRowInsertRequested: function(index) {
                            root.request(document.insertTableRow(root.snapshot.id, index))
                        }
                        onRowDeleteRequested: function(id) {
                            root.request(document.removeTableRow(root.snapshot.id, id))
                        }
                        onColumnInsertRequested: function(index) {
                            root.request(document.insertTableColumn(root.snapshot.id, index))
                        }
                        onColumnDeleteRequested: function(id) {
                            root.request(document.removeTableColumn(root.snapshot.id, id))
                        }
                        onColumnWidthChangeRequested: function(id, width) {
                            root.request(document.setTableColumnWidth(root.snapshot.id, id, width))
                        }
                    }
                }
            }
        }
    }
}
