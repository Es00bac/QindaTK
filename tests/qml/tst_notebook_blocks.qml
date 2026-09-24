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
        const split = findChild(editor, "blockSplit_text-9")
        verify(split !== null && split.enabled)
        const original = editor.blocks[0].data.text

        split.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "text-9")
        verify(requested.signalArguments[0][1] >= 0)
        verify(String(requested.signalArguments[0][2]).indexOf("notebook-1:text-9:split:") === 0)
        compare(editor.blocks[0].data.text, original)
    }
}
