// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Tk.BlockEditor bound to a Tk.NotebookDocument, driven by real key events
// (docs/notebooks.md#keyboard): typing reaches the model without a focus
// change, and the cross-block keys keep structure and undo (OFF-27).
TestCase {
    name: "NotebookKeyboard"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component { id: documentComponent; Tk.NotebookDocument {} }
    Component { id: managerComponent; Tk.DocumentManager {} }
    Component { id: clipboardComponent; Tk.ClipboardService {} }
    Component { id: resolverComponent; Tk.ResourceResolver {} }
    Component { id: editorComponent; Tk.BlockEditor { width: 700; height: 460 } }

    function visibleChild(item, name) {
        if (!item) return null
        if (item.objectName === name && item.visible) return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) {
            const found = visibleChild(kids[i], name)
            if (found) return found
        }
        return null
    }
    function field(editor, id) { return visibleChild(editor, "blockText_" + id) }
    // The list is sized by its Tk.Flex on the first polish; a delegate built
    // before that (at zero height) is released, so wait for the laid-out one.
    function laidOut(editor) { tryVerify(function() { return editor.listView.height > 0 }) }
    function focusField(editor, id, cursor) {
        laidOut(editor)
        tryVerify(function() { return field(editor, id) !== null })
        const f = field(editor, id)
        f.forceActiveFocus()
        f.cursorPosition = cursor === undefined ? f.length : cursor
        verify(f.activeFocus)
        return f
    }
    function focused(editor, id) {
        const f = field(editor, id)
        return f !== null && f.activeFocus
    }
    function ids(doc, page) { return doc.blocks(page).map(function(block) { return block.id }) }
    function textOf(doc, id) { return doc.block(id).data.text }
    function setup(texts) {
        const doc = createTemporaryObject(documentComponent, this)
        const page = doc.addPage(doc.addSection("Keys"), "", "Keys")
        const blocks = []
        for (let i = 0; i < texts.length; ++i)
            blocks.push(doc.insertBlock(page, blocks.length ? blocks[i - 1] : "", "text", { text: texts[i] }))
        const editor = createTemporaryObject(editorComponent, this, { notebook: doc, pageId: page })
        return { doc: doc, page: page, blocks: blocks, editor: editor }
    }

    // Audit gap: typing used to live only in `drafts` until focus left the
    // field, so the session stayed clean and autosave/recovery missed it.
    function test_typing_reaches_the_model_without_a_focus_change() {
        const manager = createTemporaryObject(managerComponent, this)
        const session = manager.createDocument("qindatk.notebook.v1")
        verify(session !== null)
        session.autosaveDelayMs = 600000
        const doc = session.model
        const section = doc.addSection("Typing")
        const p1 = doc.addPage(section, "", "One")
        const a = doc.insertBlock(p1, "", "text", { text: "Hello" })
        const p2 = doc.addPage(section, p1, "Two")
        doc.insertBlock(p2, "", "text", { text: "Elsewhere" })
        const editor = createTemporaryObject(editorComponent, this, { notebook: doc, pageId: p1 })
        const f = focusField(editor, a)
        const revision = session.revision
        keyClick("!")
        compare(f.text, "Hello!")
        tryVerify(function() { return textOf(doc, a) === "Hello!" }, 3000)
        verify(f.activeFocus)
        verify(session.revision !== revision) // what autosave and recovery capture
        // Typing inside the idle window commits before the page changes.
        keyClick("?")
        editor.pageId = p2
        compare(textOf(doc, a), "Hello!?")
        compare(Object.keys(editor.drafts).length, 0)
        // Both keystrokes merged into one typing step.
        verify(doc.undo())
        compare(textOf(doc, a), "Hello")
    }

    function test_split_merge_and_caret_crossing_by_key() {
        const t = setup(["Hello world", "Second"])
        const a = t.blocks[0], b = t.blocks[1]
        focusField(t.editor, a, 5)
        keyClick(Qt.Key_Return, Qt.ControlModifier)
        compare(ids(t.doc, t.page).length, 3)
        compare(textOf(t.doc, a), "Hello")
        const tail = ids(t.doc, t.page)[1]
        compare(textOf(t.doc, tail), " world")
        tryVerify(function() { return focused(t.editor, tail) })
        compare(field(t.editor, tail).cursorPosition, 0)

        keyClick(Qt.Key_Backspace)
        compare(ids(t.doc, t.page), [a, b])
        compare(textOf(t.doc, a), "Hello world")
        tryVerify(function() { return focused(t.editor, a) })
        compare(field(t.editor, a).cursorPosition, 5)
        verify(t.doc.undo())
        compare(ids(t.doc, t.page).length, 3)
        verify(t.doc.redo())

        focusField(t.editor, a)
        keyClick(Qt.Key_Right)
        tryVerify(function() { return focused(t.editor, b) })
        compare(field(t.editor, b).cursorPosition, 0)
        keyClick(Qt.Key_Left)
        tryVerify(function() { return focused(t.editor, a) })
        compare(field(t.editor, a).cursorPosition, 11)
        field(t.editor, a).cursorPosition = 3
        keyClick(Qt.Key_Down)
        tryVerify(function() { return focused(t.editor, b) })
        compare(field(t.editor, b).cursorPosition, 3)
        keyClick(Qt.Key_Up)
        tryVerify(function() { return focused(t.editor, a) })
        compare(field(t.editor, a).cursorPosition, 3)
    }

    function test_nesting_moves_and_selection_delete_by_key() {
        const t = setup(["Alpha", "Beta", "Gamma"])
        const a = t.blocks[0], b = t.blocks[1], c = t.blocks[2]
        focusField(t.editor, b, 0)
        keyClick(Qt.Key_Tab)
        compare(t.doc.block(b).parentId, a)
        tryVerify(function() { return focused(t.editor, b) })
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier)
        compare(t.doc.block(b).parentId, "")
        tryVerify(function() { return focused(t.editor, b) })
        keyClick(Qt.Key_Up, Qt.AltModifier)
        compare(ids(t.doc, t.page), [b, a, c])
        tryVerify(function() { return focused(t.editor, b) })
        keyClick(Qt.Key_Down, Qt.AltModifier)
        compare(ids(t.doc, t.page), [a, b, c])

        focusField(t.editor, a, 2)
        keyClick(Qt.Key_Down, Qt.ShiftModifier)
        keyClick(Qt.Key_Down, Qt.ShiftModifier)
        compare(t.editor.selection.anchorBlockId, a)
        compare(t.editor.selection.focusBlockId, c)
        compare(t.editor.selection.focusOffset, 2)
        verify(t.editor.inSelection(b))
        keyClick(Qt.Key_Backspace)
        compare(ids(t.doc, t.page), [a])
        compare(textOf(t.doc, a), "Almma")
        verify(!t.editor.hasBlockSelection())
        verify(t.doc.undo())
        compare(ids(t.doc, t.page), [a, b, c])
        compare(textOf(t.doc, a), "Alpha")
    }

    function test_clipboard_and_format_keys_across_blocks() {
        const t = setup(["Alpha", "Beta", "Gamma"])
        const a = t.blocks[0], b = t.blocks[1], c = t.blocks[2]
        t.editor.clipboard = createTemporaryObject(clipboardComponent, this)
        focusField(t.editor, a, 2)
        keyClick(Qt.Key_Down, Qt.ShiftModifier)
        keyClick(Qt.Key_C, Qt.ControlModifier)
        compare(t.doc.lastError, "")
        focusField(t.editor, c)
        keyClick(Qt.Key_End) // an unmodified key ends the cross-block selection
        verify(!t.editor.hasBlockSelection())
        keyClick(Qt.Key_V, Qt.ControlModifier)
        const order = ids(t.doc, t.page)
        compare(order.length, 4)
        compare(textOf(t.doc, c), "Gammapha")
        compare(textOf(t.doc, order[3]), "Be")
        tryVerify(function() { return focused(t.editor, order[3]) })
        verify(t.doc.undo())
        compare(ids(t.doc, t.page), [a, b, c])
        compare(textOf(t.doc, c), "Gamma")
        // The editor rebuilds its rows on the next event pass after the undo.
        tryVerify(function() { return t.editor.presentedIds.length === 3 })

        const f = focusField(t.editor, b, 0)
        f.select(0, 2)
        keyClick(Qt.Key_B, Qt.ControlModifier)
        const runs = t.doc.block(b).data.runs
        compare(runs[0].length, 2)
        compare(runs[0].bold, true)
    }

    // Audit gap: with only a resolver (no host AssetProvider) a present file
    // is drawn; a missing one keeps the relink notice.
    function test_ready_image_renders_from_the_resolver() {
        const doc = createTemporaryObject(documentComponent, this)
        const page = doc.addPage(doc.addSection("Media"), "", "Media")
        const ready = doc.insertBlock(page, "", "image", { resourceId: "sketch", caption: "Bank" })
        const missing = doc.insertBlock(page, ready, "image", { resourceId: "gone" })
        const resolver = createTemporaryObject(resolverComponent, this)
        verify(resolver.registerResource("sketch", String(Qt.resolvedUrl("../../examples/notebook-authoring/bank-sketch.png"))))
        resolver.registerResource("gone", "/nonexistent/qindatk/gone.png")
        const editor = createTemporaryObject(editorComponent, this, { notebook: doc, pageId: page, resolver: resolver })
        laidOut(editor)
        tryVerify(function() { return visibleChild(editor, "blockMediaImage_" + ready) !== null })
        const image = visibleChild(editor, "blockMediaImage_" + ready)
        tryVerify(function() { return image.loadState === "ready" })
        verify(visibleChild(editor, "blockMediaMissing_" + ready) === null)
        verify(visibleChild(editor, "blockMediaImage_" + missing) === null)
        verify(visibleChild(editor, "blockMediaMissing_" + missing) !== null)
    }
}
