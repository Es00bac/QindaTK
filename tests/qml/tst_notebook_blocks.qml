// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "NotebookBlocks"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component {
        id: editorComponent
        Tk.BlockEditor {
            width: 700
            height: 460
            notebookId: "notebook-1"
            pageId: "page-5"
            selectedBlockId: "text-9"
            blocks: [{ id: "text-9", type: "text", data: ({ text: "Preserve this host snapshot." }) }]
        }
    }
    Component { id: spyComponent; SignalSpy {} }
    Component { id: documentComponent; Tk.NotebookDocument {} }
    Component { id: boundEditorComponent; Tk.BlockEditor { width: 700; height: 460 } }
    Component { id: boundViewComponent; Tk.NotebookView { width: 700; height: 460 } }

    Component {
        id: notebookComponent
        Tk.NotebookView {
            width: 700
            height: 460
            notebookId: "notebook-1"
            sections: [{id:"section-1", title:"Field notes", collapsed:false}]
            pages: [{id:"page-1", sectionId:"section-1", title:"Field notes", tags:[], excerpt:""},
                    {id:"page-2", sectionId:"section-1", title:"Reference", tags:[], excerpt:""}]
            query: "hidden body term"
        }
    }

    // First visible item with this objectName; recycled delegates in the
    // ListView pool keep stale names but are invisible.
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
    function countVisible(item, prefix) {
        if (!item || !item.visible) return 0
        let count = String(item.objectName).indexOf(prefix) === 0 ? 1 : 0
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) count += countVisible(kids[i], prefix)
        return count
    }

    function test_indexed_search_can_return_body_hits_and_zero_results() {
        const notebook = createTemporaryObject(notebookComponent, this)
        verify(notebook !== null)
        compare(notebook.visiblePages.length, 0)
        notebook.searchResultPageIds = ["page-2"]
        notebook.searchState = "complete"
        compare(notebook.visiblePages.length, 1)
        compare(notebook.visiblePages[0].id, "page-2")
        notebook.searchResultPageIds = []
        compare(notebook.visiblePages.length, 0)
        notebook.query = ""
        compare(notebook.visiblePages.length, 2)
    }

    function test_split_emits_stable_block_and_cursor_request() {
        const editor = createTemporaryObject(editorComponent, this)
        verify(editor !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: editor, signalName: "blockSplitRequested" })
        tryVerify(function() { return visibleChild(editor, "blockSplit_text-9") !== null })
        const split = visibleChild(editor, "blockSplit_text-9")
        verify(split.enabled)
        const original = editor.blocks[0].data.text

        split.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "text-9")
        verify(requested.signalArguments[0][1] >= 0)
        verify(String(requested.signalArguments[0][2]).indexOf("notebook-1:text-9:split:") === 0)
        compare(editor.blocks[0].data.text, original)
    }

    function test_bound_editor_edits_the_persistent_model() {
        const doc = createTemporaryObject(documentComponent, this)
        const page = doc.addPage(doc.addSection("Notes"), "", "Day")
        const a = doc.insertBlock(page, "", "text", { text: "Hello world" })
        const editor = createTemporaryObject(boundEditorComponent, this, { notebook: doc, pageId: page })
        // Sized by its Tk.Flex on the first polish; a zero-height delegate is released.
        tryVerify(function() { return editor.listView.height > 0 })
        tryVerify(function() { return visibleChild(editor, "blockText_" + a) !== null })
        const field = visibleChild(editor, "blockText_" + a)
        field.cursorPosition = 5
        visibleChild(editor, "blockSplit_" + a).click()
        compare(doc.blocks(page).length, 2)
        compare(doc.block(a).data.text, "Hello")
        const tail = doc.blocks(page)[1].id
        compare(doc.block(tail).data.text, " world")
        tryVerify(function() { return editor.presentedIds.length === 2 })
        // The new block's field takes focus at its start.
        tryVerify(function() { const f = visibleChild(editor, "blockText_" + tail); return f !== null && f.activeFocus })
        compare(visibleChild(editor, "blockText_" + tail).cursorPosition, 0)

        visibleChild(editor, "blockMerge_" + tail).click()
        compare(doc.blocks(page).length, 1)
        compare(doc.block(a).data.text, "Hello world")
        verify(doc.canUndo)
        visibleChild(editor, "blockUndo").click()
        tryVerify(function() { return editor.presentedIds.length === 2 })

        editor.insertBlock("table")
        tryVerify(function() { return editor.presentedIds.length === 3 })
        const table = doc.blocks(page).filter(function(block) { return block.type === "table" })[0].id
        tryVerify(function() { return visibleChild(editor, "blockTableCell_" + table + "_0_1") !== null })
        const cell = visibleChild(editor, "blockTableCell_" + table + "_0_1")
        cell.text = "42"
        cell.editingFinished()
        compare(doc.block(table).data.rows[0][1], "42")
    }

    function test_drafts_survive_delegate_recycling() {
        const doc = createTemporaryObject(documentComponent, this)
        const page = doc.addPage(doc.addSection("Long"), "", "Many blocks")
        const ids = []
        let after = ""
        for (let i = 0; i < 120; ++i) {
            after = doc.insertBlock(page, after, "text", { text: "Block " + i })
            ids.push(after)
        }
        // Idle commits off: this checks that the draft itself survives reuse.
        const editor = createTemporaryObject(boundEditorComponent, this,
                                             { notebook: doc, pageId: page, height: 300, autoCommitDrafts: false })
        tryVerify(function() { return editor.listView.height > 0 })
        tryVerify(function() { return visibleChild(editor, "blockText_" + ids[0]) !== null })
        // Lazy presentation: only the viewport and its cache are built.
        verify(countVisible(editor, "notebookBlock_") < 40)
        verify(visibleChild(editor, "blockText_" + ids[119]) === null)

        editor.storeDraft(ids[0], "Edited but not committed", 8, "Block 0")
        editor.listView.positionViewAtEnd()
        tryVerify(function() { return visibleChild(editor, "blockText_" + ids[119]) !== null })
        // Still virtualized at the far end; block 0's card was released.
        verify(countVisible(editor, "notebookBlock_") < 40)
        verify(editor.listView.contentY > editor.listView.height)
        // Released once the list finishes its incremental refill.
        tryVerify(function() { return visibleChild(editor, "blockText_" + ids[0]) === null })
        editor.listView.positionViewAtBeginning()
        tryVerify(function() {
            const field = visibleChild(editor, "blockText_" + ids[0])
            return field !== null && field.text === "Edited but not committed"
        })
        compare(doc.block(ids[0]).data.text, "Block 0")
        verify(editor.commitDraft(ids[0]))
        compare(doc.block(ids[0]).data.text, "Edited but not committed")
    }

    function test_bound_notebook_view_structure_history_and_search() {
        const doc = createTemporaryObject(documentComponent, this)
        const section = doc.addSection("Research")
        const p1 = doc.addPage(section, "", "Tides")
        doc.insertBlock(p1, "", "text", { text: "salinity rose at dawn" })
        const p2 = doc.addPage(section, p1, "Recipes")
        const view = createTemporaryObject(boundViewComponent, this, { notebook: doc })
        compare(view.presentedPages.length, 2)

        view.query = "salinity"
        view.submitSearch()
        compare(view.searchState, "complete")
        compare(view.visiblePages.length, 1)
        compare(view.visiblePages[0].id, p1)
        view.query = ""

        view.navigateTo(p1)
        view.navigateTo(p2)
        compare(view.currentPageId, p2)
        compare(view.historyBackPageIds.length, 1)
        view.goBack()
        compare(view.currentPageId, p1)
        compare(view.historyForwardPageIds[0], p2)

        view.insertSection("Archive")
        compare(doc.sections.length, 2)
        view.insertPage(section, p1)
        compare(doc.pages.length, 3)
        compare(doc.pages[1].id, view.currentPageId)
        view.addTag("field")
        compare(doc.page(view.currentPageId).tags, ["field"])
        view.deletePage(view.currentPageId)
        compare(doc.pages.length, 2)
        verify(doc.undo())
        compare(doc.pages.length, 3)
    }

    // The live example (examples/notebook-authoring) stands in for a demo
    // app: it must run on the persistent model with real undo, search and save.
    function test_example_runs_on_the_persistent_notebook() {
        const component = Qt.createComponent(Qt.resolvedUrl("../../examples/notebook-authoring/Main.qml"))
        compare(component.status, Component.Ready, component.errorString())
        const example = createTemporaryObject(component, this)
        verify(example !== null)
        const doc = example.notebook
        verify(doc !== null)
        compare(doc.pages.length, 6)
        compare(doc.sections.length, 3)
        verify(!doc.canUndo)
        compare(example.currentPageId, "page-lab-log")
        compare(doc.chartSeries("lab-chart").valid, true)
        compare(doc.block("lab-todo").parentId, "lab-detail")
        compare(example.backlinkCount, 1)
        const editor = findChild(example, "notebookBlockEditor")
        tryVerify(function() { return editor.presentedIds.length === 8 })

        const view = findChild(example, "fieldNotebook")
        view.query = "salinity"
        view.submitSearch()
        compare(view.searchState, "complete")
        compare(view.visiblePages.length, 2)
        view.query = ""

        verify(doc.setBlockText("lab-intro", "Changed in the test"))
        verify(doc.canUndo)
        findChild(example, "notebookUndo").clicked()
        compare(doc.block("lab-intro").data.text.indexOf("The upper bank"), 0)

        verify(example.session.dirty)
        findChild(example, "notebookSave").clicked()
        tryVerify(function() { return !example.session.dirty && !example.session.saving }, 10000)
        compare(example.session.uri.length > 0, true)
    }
}
