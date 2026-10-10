// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// QindaNote M84 gaps: typing keys win over a host's one-letter shortcuts
// (D-320), a host-kept history receives the area's undo and redo (D-321),
// the format state reports the highlight, and a whole table deletes as one
// undoable step (D-322).
TestCase {
    id: root
    name: "RichTextHostIntegration"
    when: windowShown
    visible: true
    width: 480
    height: 320

    property int pShortcuts: 0
    property int deleteShortcuts: 0
    Shortcut { sequence: "P"; onActivated: root.pShortcuts += 1 }
    Shortcut { sequence: "Del"; onActivated: root.deleteShortcuts += 1 }

    Component { id: spyComponent; SignalSpy {} }
    Component { id: documentComponent; Tk.RichTextDocument {} }
    Component {
        id: noteComponent
        Item {
            property alias document: noteDocument
            property alias editor: noteEditor
            width: 320
            height: 160
            Tk.RichTextDocument { id: noteDocument }
            Tk.RichTextEditor { id: noteEditor; width: 300; document: noteDocument; rows: 4; tooltip: "Note" }
        }
    }
    Tk.Button { id: elsewhere; x: 10; y: 260; text: "Elsewhere" }

    function makeNote() {
        const note = createTemporaryObject(noteComponent, root)
        verify(note !== null)
        waitForRendering(note)
        return note
    }

    function test_typing_keys_win_over_host_shortcuts() {
        const note = makeNote()
        const area = note.editor.area
        area.forceActiveFocus()
        verify(area.activeFocus)
        keyClick(Qt.Key_P)
        keyClick("P")
        compare(root.pShortcuts, 0)
        compare(note.document.text(), "pP")
        keyClick(Qt.Key_Left)
        keyClick(Qt.Key_Delete)
        compare(root.deleteShortcuts, 0)
        compare(note.document.text(), "p")
        // Read-only or unfocused, the host's shortcuts run.
        note.editor.readOnly = true
        keyClick(Qt.Key_P)
        compare(root.pShortcuts, 1)
        note.editor.readOnly = false
        elsewhere.forceActiveFocus()
        keyClick(Qt.Key_P)
        keyClick(Qt.Key_Delete)
        compare(root.pShortcuts, 2)
        compare(root.deleteShortcuts, 1)
        compare(note.document.text(), "p")
    }

    function test_external_history_receives_undo_and_redo() {
        const note = makeNote()
        const doc = note.document
        const area = note.editor.area
        doc.externalUndo = true
        const undo = createTemporaryObject(spyComponent, root, { target: doc, signalName: "undoRequested" })
        const redo = createTemporaryObject(spyComponent, root, { target: doc, signalName: "redoRequested" })
        verify(area.insertText("abc"))
        compare(area.commandState("edit.undo").enabled, true)
        compare(area.commandState("edit.redo").enabled, true)
        verify(area.invokeCommand("edit.undo"))
        compare(undo.count, 1)
        compare(doc.text(), "abc")
        area.forceActiveFocus()
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(undo.count, 2)
        keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
        compare(redo.count, 1)
        verify(doc.requestRedo())
        compare(redo.count, 2)
        // undo() itself keeps doing nothing for a host-kept history.
        verify(!doc.undo())
        compare(undo.count, 2)
        // A host that answers with requestUndo() itself does not recurse.
        let nested = true
        const answer = function() { nested = doc.requestUndo() }
        doc.undoRequested.connect(answer)
        verify(doc.requestUndo())
        doc.undoRequested.disconnect(answer)
        compare(nested, false)
        compare(undo.count, 3)
        // Without externalUndo the local history works as before.
        doc.externalUndo = false
        verify(area.insertText("d"))
        verify(area.invokeCommand("edit.undo"))
        compare(doc.text(), "abc")
        compare(undo.count, 3)
    }

    function test_format_state_reports_highlight() {
        const note = makeNote()
        const area = note.editor.area
        verify(area.insertText("one two"))
        area.selectAll()
        compare(area.formatState.highlightMixed, false)
        verify(area.invokeCommand("text.highlight", { value: "#ffff00" }))
        verify(Qt.colorEqual(area.formatState.highlight, "#ffff00"))
        verify(Qt.colorEqual(area.commandState("text.highlight").value, "#ffff00"))
        const paragraph = note.document.paragraphIds()[0]
        verify(area.selectRange(paragraph, 0, paragraph, 3))
        verify(area.invokeCommand("text.highlight", { value: "" }))
        area.selectAll()
        compare(area.formatState.highlightMixed, true)
        verify(area.selectRange(paragraph, 5, paragraph, 5))
        verify(Qt.colorEqual(area.formatState.highlight, "#ffff00"))
        compare(area.formatState.highlightMixed, false)
    }

    function test_delete_whole_table_is_one_undo_step() {
        const note = makeNote()
        const doc = note.document
        const area = note.editor.area
        verify(area.insertText("before"))
        compare(area.commandState("table.delete").enabled, false)
        verify(area.invokeCommand("insert.table", { rows: 2, columns: 3 }))
        compare(doc.tables.length, 1)
        verify(area.insertText("cell"))
        compare(area.commandState("table.delete").enabled, true)
        verify(area.invokeCommand("table.delete"))
        compare(doc.tables.length, 0)
        verify(doc.text().indexOf("cell") < 0)
        verify(doc.text().indexOf("before") === 0)
        compare(area.commandState("table.delete").enabled, false)
        verify(doc.undo())
        compare(doc.tables.length, 1)
        verify(doc.text().indexOf("cell") >= 0)
        verify(doc.redo())
        compare(doc.tables.length, 0)
        verify(doc.undo())
        verify(doc.removeTable(doc.tables[0].id))
        compare(doc.tables.length, 0)
        verify(!doc.removeTable("00000000-0000-4000-8000-000000000000"))
    }

    // D-325: per-paragraph host data (tags) is undoable, saved, and
    // inherited by a split like a list membership.
    function test_paragraph_data_is_saved_undoable_and_split_with_the_paragraph() {
        const note = makeNote()
        const doc = note.document
        const area = note.editor.area
        verify(area.insertText("Buy milk"))
        const id = doc.paragraphIds()[0]
        compare(Object.keys(doc.paragraphData(id)).length, 0)
        verify(doc.setParagraphData(id, { tags: ["todo"], done: false }))
        compare(doc.paragraphData(id).tags[0], "todo")
        compare(doc.paragraphData(id).done, false)
        compare(doc.undoText, "Paragraph data")
        verify(doc.undo())
        compare(Object.keys(doc.paragraphData(id)).length, 0)
        verify(doc.redo())
        compare(doc.paragraphData(id).tags[0], "todo")
        compare(doc.paragraphFormatMap(id).data.tags[0], "todo")
        const reopened = createTemporaryObject(documentComponent, root)
        verify(reopened.loadJson(doc.toJson()))
        compare(reopened.paragraphData(id).tags[0], "todo")
        area.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(doc.paragraphIds().length, 2)
        compare(doc.paragraphData(doc.paragraphIds()[1]).tags[0], "todo")
        verify(!doc.setParagraphData(id, { blob: "x".repeat(70000) }))
        verify(!doc.setParagraphData("00000000-0000-4000-8000-000000000000", { tags: [] }))
        compare(doc.paragraphData(id).tags[0], "todo")
    }
}
