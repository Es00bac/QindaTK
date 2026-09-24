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

    // TableEditor merge and split requests on a real document.
    Component {
        id: mergeTableComponent
        Tk.TableEditor {
            id: mergeEditor
            required property Tk.RichTextDocument document
            property var picked: []
            width: 620
            table: mergeEditor.document.tables.length ? mergeEditor.document.tables[0]
                                                      : ({ id: "", rows: [], columns: [], cells: [] })
            selectedCellIds: mergeEditor.picked
            onCellSelectionRequested: function(id, extend) {
                if (!extend) {
                    mergeEditor.picked = [id]
                    return
                }
                const next = mergeEditor.picked.slice()
                const index = next.indexOf(id)
                if (index >= 0) next.splice(index, 1)
                else next.push(id)
                mergeEditor.picked = next
            }
            onCellsMergeRequested: function(ids) {
                if (mergeEditor.document.mergeTableCells(mergeEditor.table.id, ids)) mergeEditor.picked = [ids[0]]
            }
            onCellSplitRequested: function(id) { mergeEditor.document.splitTableCell(mergeEditor.table.id, id) }
        }
    }

    // StyleManager requests on a real document's style graph.
    Tk.RichTextDocument { id: styleDocument }
    Component {
        id: styleManagerComponent
        Tk.StyleManager {
            id: manager
            required property Tk.RichTextDocument document
            property var targets: []
            width: 440
            styles: manager.document.styles
            styleKinds: manager.document.styleKinds
            targetIds: manager.targets
            directOverrides: {
                manager.document.revision
                return manager.document.directOverrides(manager.targets)
            }
            onStyleSelectionRequested: function(id) { manager.selectedStyleId = id }
            onCreateStyleRequested: function(name, kind, parentId) {
                manager.selectedStyleId = manager.document.createStyle(name, kind, parentId)
            }
            onUpdateStyleRequested: function(id, patch) {
                if (!manager.document.updateStyle(id, { name: patch.name, parentId: patch.parentId }))
                    manager.validationError = manager.document.lastError
            }
            onApplyStyleRequested: function(id, targets) { manager.document.applyStyle(id, targets) }
            onResetOverridesRequested: function(targets, keys) { manager.document.resetDirectFormatting(targets, keys) }
        }
    }

    // TextFrameEditor requests on a real document's frames.
    Tk.RichTextDocument { id: frameDocument }
    Component {
        id: frameEditorComponent
        Tk.TextFrameEditor {
            id: frameEditor
            required property Tk.RichTextDocument document
            width: 440
            frames: frameEditor.document.frames
            pageId: "page-1"
            newFrameGeometryMm: ({ xMm: 20, yMm: 20, widthMm: 80, heightMm: 60 })
            onFrameSelectionRequested: function(id) { frameEditor.selectedFrameId = id }
            onCreateFrameRequested: function(pageId, geometry) {
                frameEditor.selectedFrameId = frameEditor.document.createTextFrame(0, geometry)
            }
            onGeometryChangeRequested: function(id, geometry) { frameEditor.document.setTextFrameGeometry(id, geometry) }
            onLayoutChangeRequested: function(id, layout) { frameEditor.document.setTextFrameLayout(id, layout) }
            onConnectRequested: function(sourceId, targetId) { frameEditor.document.connectTextFrames(sourceId, targetId) }
            onDisconnectRequested: function(id) { frameEditor.document.disconnectTextFrame(id) }
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
        verify(!findChild(editor, "tableMergeCells").enabled) // one selected cell
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

    function test_merge_and_split_edit_the_document() {
        verify(tableDocument.loadJson(tableJson()))
        const editor = createTemporaryObject(mergeTableComponent, this, { document: tableDocument })
        verify(editor !== null)
        const paper = editor.cellAt(1, 0)
        const size = editor.cellAt(1, 1)
        editor.selectCell(paper.id)
        editor.extendSelection = true
        editor.selectCell(size.id)
        compare(editor.selectedCellIds.length, 2)
        const merge = findChild(editor, "tableMergeCells")
        verify(merge.enabled)
        merge.clicked()
        compare(tableDocument.undoText, "Merge table cells")
        compare(editor.table.cells.length, 3)
        const merged = editor.cellAt(1, 0)
        compare(merged.id, paper.id)
        compare(merged.columnSpan, 2)
        compare(merged.text, "Paper\nA4")
        verify(editor.canSplit)
        verify(findChild(editor, "tableCell_" + merged.id).tooltip.indexOf("merged across 1 rows and 2 columns") >= 0)
        findChild(editor, "tableSplitCell").clicked()
        compare(tableDocument.undoText, "Split table cell")
        compare(editor.table.cells.length, 4)
        compare(editor.cellAt(1, 1).id, size.id)
        verify(tableDocument.undo())
        compare(editor.cellAt(1, 0).columnSpan, 2)
        verify(tableDocument.undo())
        compare(editor.cellAt(1, 0).text, "Paper")
        compare(editor.cellAt(1, 1).text, "A4")
    }

    function test_style_manager_edits_the_document_style_graph() {
        verify(styleDocument.loadJson(tableJson())) // no styles yet
        const manager = createTemporaryObject(styleManagerComponent, this, { document: styleDocument })
        verify(manager !== null)
        compare(manager.styleKinds, ["paragraph", "character", "table"])
        const before = styleDocument.paragraphIds()[0]
        manager.targets = [before]
        manager.draftName = "Body"
        manager.draftKind = "paragraph"
        manager.createStyle()
        compare(styleDocument.styles.length, 1)
        const body = manager.selectedStyleId
        compare(manager.selectedStyle.name, "Body")
        verify(styleDocument.updateStyle(body, { paragraph: { spaceAfterPt: 9 } }))
        const apply = findChild(manager, "styleApply")
        verify(apply.enabled)
        apply.clicked()
        compare(styleDocument.undoText, "Apply style")
        compare(styleDocument.paragraphFormatMap(before).style, body)
        compare(styleDocument.paragraphFormatMap(before).spaceAfterPt, 9)
        // A child inherits the parent's values; making the parent a child of
        // its own child is refused and reported.
        manager.draftName = "Lead"
        manager.draftParentId = body
        manager.createStyle()
        const lead = manager.selectedStyleId
        compare(manager.selectedStyle.parentId, body)
        manager.selectedStyleId = body
        manager.draftName = "Body"
        manager.draftKind = "paragraph"
        manager.draftParentId = lead
        manager.updateSelected()
        verify(manager.validationError.indexOf("cycle") >= 0)
        compare(styleDocument.styles.length, 2)
        // A later change of the style reaches its paragraph, which has no
        // direct overrides to keep.
        verify(styleDocument.updateStyle(body, { paragraph: { align: "center" } }))
        compare(styleDocument.paragraphFormatMap(before).align, "center")
        compare(manager.directOverrides.length, 0)
        verify(styleDocument.undo())
        compare(styleDocument.paragraphFormatMap(before).align, "start")
    }

    function test_frame_editor_threads_document_frames() {
        verify(frameDocument.loadJson(tableJson())) // no frames yet
        const editor = createTemporaryObject(frameEditorComponent, this, { document: frameDocument })
        verify(editor !== null)
        const create = findChild(editor, "textFrameCreate")
        verify(create.enabled)
        create.clicked()
        const first = editor.selectedFrameId
        verify(first.length > 0)
        compare(frameDocument.storyFrameId, first)
        editor.newFrameGeometryMm = ({ xMm: 110, yMm: 20, widthMm: 80, heightMm: 60 })
        create.clicked()
        const second = editor.selectedFrameId
        compare(frameDocument.frames.length, 2)
        editor.selectedFrameId = first
        compare(editor.connectionTargets.length, 1)
        compare(editor.connectionTargets[0].id, second)
        editor.targetFrameId = second
        const connect = findChild(editor, "textFrameConnect")
        verify(connect.enabled)
        connect.clicked()
        compare(frameDocument.frames[0].nextFrameId, second)
        compare(frameDocument.undoText, "Connect text frames")
        editor.setGeometry("widthMm", 120)
        verify(Math.abs(frameDocument.frames[0].widthMm - 120) < 1e-6)
        compare(frameDocument.undoText, "Resize text frame")
        editor.setLayout("columns", 2)
        compare(frameDocument.frames[0].columns, 2)
        const disconnect = findChild(editor, "textFrameDisconnect")
        verify(disconnect.enabled)
        disconnect.clicked()
        compare(frameDocument.frames[0].nextFrameId, "")
        verify(frameDocument.undo())
        compare(frameDocument.frames[0].nextFrameId, second)
    }
}
