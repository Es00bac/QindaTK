// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Two modes (docs/notebooks.md#views). Bound to a Tk.NotebookDocument through
// `notebook`, the editor applies every edit to that persistent model (undo,
// save and recovery included). Without it, the host owns block IDs, order,
// data and undo: the editor only emits identified requests.
// AGENT-GUARD: blocks are virtualized (ListView, reuseItems). Uncommitted
// text lives in `drafts` keyed by block ID, never only in a delegate, so a
// recycled or rebuilt delegate restores it; committing clears the draft.
Item {
    id: control
    property string notebookId: ""
    property string pageId: ""
    property var blocks: [] // host mode: [{id,type,data,locked,error,depth}]
    property string selectedBlockId: ""
    property string focusedBlockId: ""
    property int caretOffset: -1
    property var blockTypes: [
        { id: "text", label: qsTr("Text") }, { id: "heading", label: qsTr("Heading") },
        { id: "list", label: qsTr("List item") }, { id: "quote", label: qsTr("Quote") },
        { id: "code", label: qsTr("Code") }, { id: "table", label: qsTr("Table") },
        { id: "chart", label: qsTr("Chart") }, { id: "image", label: qsTr("Image") },
        { id: "embed", label: qsTr("Embed") }, { id: "ink", label: qsTr("Ink") }
    ]
    property var capabilities: ({ canEdit: true, canInsert: true, canMove: true, canSplit: true, canMerge: true, canDelete: true, canNest: true })
    property Tk.NotebookDocument notebook: null
    property Tk.ClipboardService clipboard: null
    property Tk.ScriptRuntime scriptRuntime: null
    property Tk.ResourceResolver resolver: null
    property var provider: null
    property var document: null
    property string viewId: ""
    property string errorText: ""
    property string tooltip: qsTr("Edit ordered notebook content blocks")
    // {anchorBlockId, anchorOffset, focusBlockId, focusOffset}; empty when none.
    property var selection: ({})
    property var drafts: ({})
    property var _notebookBlocks: []
    readonly property var presentedBlocks: notebook ? _notebookBlocks : blocks
    // The ListView model: block IDs only, replaced only when the order
    // changes, so a text commit never rebuilds (and unfocuses) delegates.
    property var presentedIds: []
    property var _positions: ({})
    property string pendingFocusId: ""
    readonly property var selectedBlock: findBlock(selectedBlockId)
    readonly property alias listView: blockList
    readonly property string activeError: errorText.length > 0 ? errorText : notebook ? notebook.lastError : ""

    signal blockSelectionRequested(string blockId)
    signal blockFocusRequested(string blockId)
    signal blockNavigateRequested(string blockId, string direction)
    signal blockInsertRequested(string afterBlockId, string type, var data, string requestId)
    signal blockMoveRequested(string blockId, int targetIndex, string requestId)
    signal blockNestRequested(string blockId, string direction, string requestId)
    signal blockContentEditRequested(string blockId, var data, string requestId)
    signal blockSplitRequested(string blockId, int cursorPosition, string requestId)
    signal blockMergeRequested(string firstBlockId, string secondBlockId, string requestId)
    signal blockDeleteRequested(string blockId)
    signal blockActionRequested(string blockId, string action, string requestId)
    signal blockOpenRequested(string blockId, string targetId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("Page %1, %2 content blocks").arg(pageId).arg(presentedBlocks.length)
    activeFocusOnTab: true

    function refreshNotebookBlocks() {
        _notebookBlocks = notebook && pageId.length > 0 ? notebook.blocks(pageId) : []
    }
    function syncIds() {
        const ids = [], positions = ({})
        for (let i = 0; i < presentedBlocks.length; ++i) {
            ids.push(presentedBlocks[i].id)
            positions[presentedBlocks[i].id] = i
        }
        _positions = positions
        if (ids.join("\n") !== presentedIds.join("\n")) presentedIds = ids
        const reveal = indexOf(pendingFocusId)
        if (reveal >= 0) blockList.positionViewAtIndex(reveal, ListView.Contain)
    }
    onPresentedBlocksChanged: syncIds()
    onNotebookChanged: refreshNotebookBlocks()
    onPageIdChanged: {
        drafts = ({})
        selection = ({})
        if (notebook) selectedBlockId = ""
        refreshNotebookBlocks()
    }
    Component.onCompleted: { refreshNotebookBlocks(); syncIds() }
    // Deferred so a structural edit made from a delegate's key handler never
    // releases that delegate while the handler runs.
    Connections {
        target: control.notebook
        function onChanged() { Qt.callLater(control.refreshNotebookBlocks) }
    }

    function requestId(kind, id) { return notebookId + ":" + (id || pageId) + ":" + kind + ":" + Date.now() }
    function indexOf(id) {
        const position = _positions[id]
        return position === undefined || position >= presentedBlocks.length
               || presentedBlocks[position].id !== id ? -1 : position
    }
    function findBlock(id) {
        const index = indexOf(id)
        return index >= 0 ? presentedBlocks[index] : null
    }
    function isTextType(type) {
        return ["text", "heading", "quote", "list", "todo", "code"].indexOf(type) >= 0
    }
    function makeDefaultData(type) {
        if (type === "text" || type === "quote" || type === "list") return { text: "" }
        if (type === "heading") return { text: qsTr("Section heading"), level: 2 }
        if (type === "code") return { text: "", language: "javascript" }
        if (type === "table") return { columns: [qsTr("Column 1"), qsTr("Column 2")], rows: [["", ""], ["", ""]] }
        if (type === "chart") {
            const source = presentedBlocks.find(function(block) { return block.type === "table" })
            return { sourceBlockId: source ? source.id : "", kind: "line", xColumn: 0, yColumn: 1, title: qsTr("Chart") }
        }
        if (type === "image") return { resourceId: "", caption: "" }
        if (type === "embed") return { targetId: "", title: qsTr("Linked content") }
        if (type === "ink") return { strokes: [] }
        return { raw: "", unsupported: true }
    }
    function draftText(id, committed) {
        const draft = drafts[id]
        return draft !== undefined ? draft.text : committed
    }
    function storeDraft(id, text, cursor, committed) {
        const next = Object.assign({}, drafts)
        if (text === committed) delete next[id]
        else next[id] = { text: text, cursor: cursor }
        drafts = next
    }
    function commitDraft(id) {
        const draft = drafts[id]
        const block = findBlock(id)
        if (draft === undefined || !block) return true
        return submitText(block, draft.text)
    }
    // Notebook mode: the block's text field takes focus when its delegate
    // exists (now or after the deferred refresh). Host mode: a request; the
    // host moves `focusedBlockId`.
    // AGENT-GUARD: applied through Qt.callLater, queued after the deferred
    // block refresh the edit triggered, so the field sees its new text first.
    property var _focusRequest: null
    function focusBlock(id, offset) {
        if (!notebook) {
            caretOffset = offset === undefined ? -1 : offset
            blockFocusRequested(id)
            return
        }
        _focusRequest = { id: id, offset: offset === undefined ? -1 : offset }
        Qt.callLater(control.applyFocusRequest)
    }
    function applyFocusRequest() {
        const request = _focusRequest
        _focusRequest = null
        if (!request) return
        caretOffset = request.offset
        selectedBlockId = request.id
        pendingFocusId = ""
        pendingFocusId = request.id
        const index = indexOf(request.id)
        if (index >= 0) blockList.positionViewAtIndex(index, ListView.Contain)
    }
    function insertBlock(type) {
        if (!enabled || !capabilities.canInsert) return
        const after = selectedBlockId || (presentedBlocks.length ? presentedBlocks[presentedBlocks.length - 1].id : "")
        if (!notebook) {
            blockInsertRequested(after, type, makeDefaultData(type), requestId("block", ""))
            return
        }
        const id = notebook.insertBlock(pageId, after, type, makeDefaultData(type))
        if (id.length > 0) focusBlock(id, 0)
    }
    function moveBlock(id, delta) {
        const from = indexOf(id), target = from + delta
        if (!enabled || !capabilities.canMove || from < 0 || target < 0 || target >= presentedBlocks.length) return
        commitDraft(id)
        if (!notebook) {
            blockMoveRequested(id, target, requestId("move", id))
            return
        }
        if (notebook.moveBlockBy(id, delta)) focusBlock(id, caretOffset)
    }
    function nestBlock(id, direction) {
        if (!enabled || !capabilities.canNest) return
        commitDraft(id)
        if (!notebook) blockNestRequested(id, direction, requestId(direction, id))
        else if (direction === "indent") notebook.indentBlock(id)
        else notebook.outdentBlock(id)
        focusBlock(id, caretOffset)
    }
    function deleteBlock(id) {
        if (!enabled || !capabilities.canDelete) return
        const next = Object.assign({}, drafts)
        delete next[id]
        drafts = next
        if (notebook) notebook.removeBlock(id)
        else blockDeleteRequested(id)
    }
    function submitText(block, text) {
        if (!capabilities.canEdit || block.locked) return false
        const next = Object.assign({}, drafts)
        delete next[block.id]
        drafts = next
        if (text === (block.data && block.data.text)) return true
        if (notebook) return notebook.setBlockText(block.id, text)
        blockContentEditRequested(block.id, Object.assign({}, block.data || ({}), { text: text }), requestId("edit", block.id))
        return true
    }
    function submitData(block, patch) {
        if (!capabilities.canEdit || block.locked) return
        if (notebook) notebook.setBlockData(block.id, Object.assign({}, block.data || ({}), patch))
        else blockContentEditRequested(block.id, Object.assign({}, block.data || ({}), patch), requestId("edit", block.id))
    }
    function splitBlock(block, cursorPosition) {
        if (!capabilities.canSplit || !isTextType(block.type) || block.locked) return
        if (!notebook) {
            blockSplitRequested(block.id, cursorPosition, requestId("split", block.id))
            return
        }
        commitDraft(block.id)
        const id = notebook.splitBlock(block.id, cursorPosition)
        if (id.length > 0) focusBlock(id, 0)
    }
    function mergeWithPrevious(block) {
        const index = indexOf(block.id)
        if (!capabilities.canMerge || index <= 0 || !isTextType(block.type) || !isTextType(presentedBlocks[index - 1].type)) return
        const previous = presentedBlocks[index - 1].id
        if (!notebook) {
            blockMergeRequested(previous, block.id, requestId("merge", ""))
            return
        }
        commitDraft(block.id)
        commitDraft(previous)
        const offset = notebook.mergeBlocks(previous, block.id)
        if (offset >= 0) focusBlock(previous, offset)
    }
    function navigate(id, offset, direction) {
        if (!notebook) {
            blockNavigateRequested(id, direction === "up" || direction === "left" ? "previous" : "next")
            return
        }
        commitDraft(id)
        const target = notebook.moveCaret(id, offset, direction)
        if (target.moved) focusBlock(target.blockId, target.offset)
    }
    function extendSelection(id, offset, direction) {
        const anchor = selection.anchorBlockId ? selection : { anchorBlockId: id, anchorOffset: offset }
        const target = notebook ? notebook.moveCaret(selection.focusBlockId || id,
                                                     selection.focusBlockId ? selection.focusOffset : offset, direction)
                                : { blockId: id, offset: offset }
        selection = { anchorBlockId: anchor.anchorBlockId, anchorOffset: anchor.anchorOffset,
                      focusBlockId: target.blockId, focusOffset: target.offset }
    }
    function hasBlockSelection() {
        return !!selection.anchorBlockId && selection.anchorBlockId !== selection.focusBlockId
    }
    function inSelection(id) {
        if (!hasBlockSelection()) return false
        const a = indexOf(selection.anchorBlockId), f = indexOf(selection.focusBlockId), i = indexOf(id)
        return i >= Math.min(a, f) && i <= Math.max(a, f)
    }
    // The cross-block selection, or the text selection inside one field.
    function effectiveSelection(id, start, end) {
        if (hasBlockSelection()) return selection
        return { anchorBlockId: id, anchorOffset: start, focusBlockId: id, focusOffset: end }
    }
    function copy(id, start, end) {
        if (!notebook || !clipboard) return false
        return notebook.copySelection(clipboard, effectiveSelection(id, start, end))
    }
    function cut(id, start, end) {
        if (!notebook || !clipboard) return false
        commitDraft(id)
        const caret = notebook.cutSelection(clipboard, effectiveSelection(id, start, end))
        selection = ({})
        if (caret.blockId) focusBlock(caret.blockId, caret.offset)
        return !!caret.pageId
    }
    function deleteSelection() {
        if (!notebook || !hasBlockSelection()) return false
        const caret = notebook.deleteSelection(selection)
        selection = ({})
        if (caret.blockId) focusBlock(caret.blockId, caret.offset)
        return true
    }
    function paste(id, offset) {
        if (!notebook || !clipboard) return false
        commitDraft(id)
        if (hasBlockSelection()) {
            const caret = notebook.deleteSelection(selection)
            selection = ({})
            id = caret.blockId || ""
            offset = caret.offset || 0
        }
        const result = notebook.paste(clipboard, pageId, id, offset)
        if (result.blockId) focusBlock(result.blockId, result.offset)
        return !!result.pageId
    }
    function formatSelection(id, start, end, patch) {
        if (!notebook || end <= start) return false
        commitDraft(id)
        return notebook.formatText(id, start, end - start, patch)
    }
    function evaluate(id) {
        commitDraft(id)
        if (notebook && scriptRuntime) notebook.evaluateBlock(id, scriptRuntime)
        else blockActionRequested(id, "evaluate", requestId("evaluate", id))
    }
    function tableEdit(block, action, row, column, text) {
        if (!capabilities.canEdit || block.locked) return
        if (!notebook && action === "cell") {
            const rows = ((block.data && block.data.rows) || []).map(function(cells) { return cells.slice() })
            if (!rows[row]) return
            rows[row][column] = text
            blockContentEditRequested(block.id, Object.assign({}, block.data, { rows: rows }), requestId("cell", block.id))
            return
        }
        if (!notebook) {
            blockActionRequested(block.id, action, requestId(action, block.id))
            return
        }
        if (action === "cell") notebook.setTableCell(block.id, row, column, text)
        else if (action === "add-row") notebook.insertTableRow(block.id, -1)
        else if (action === "remove-row") notebook.removeTableRow(block.id, row)
        else if (action === "add-column") notebook.insertTableColumn(block.id, -1, "")
        else if (action === "remove-column") notebook.removeTableColumn(block.id, column)
    }
    // Inline runs as StyledText for the unfocused preview of a text block.
    function runsHtml(data) {
        const text = String(data && data.text || "")
        const runs = data && data.runs ? data.runs : [{ length: text.length }]
        let html = "", position = 0
        for (let i = 0; i < runs.length; ++i) {
            let piece = text.substr(position, runs[i].length).replace(/&/g, "&amp;").replace(/</g, "&lt;")
                .replace(/>/g, "&gt;").replace(/\n/g, "<br>")
            position += runs[i].length
            if (runs[i].code) piece = "<code>" + piece + "</code>"
            if (runs[i].bold) piece = "<b>" + piece + "</b>"
            if (runs[i].italic) piece = "<i>" + piece + "</i>"
            if (runs[i].underline) piece = "<u>" + piece + "</u>"
            if (runs[i].strike) piece = "<s>" + piece + "</s>"
            if (runs[i].href) piece = "<a href=\"" + String(runs[i].href).replace(/"/g, "&quot;") + "\">" + piece + "</a>"
            html += piece
        }
        return html
    }
    function openLink(blockId, href) {
        blockOpenRequested(blockId, href)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            objectName: "blockEditorToolbar"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Overline { title: qsTr("PAGE CONTENT"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.IconButton {
                objectName: "blockUndo"
                visible: !!control.notebook
                iconName: "undo-2"
                tooltip: qsTr("Undo the last notebook edit")
                enabled: !!control.notebook && control.notebook.canUndo
                onClicked: control.notebook.undo()
            }
            Tk.IconButton {
                objectName: "blockRedo"
                visible: !!control.notebook
                iconName: "redo-2"
                tooltip: qsTr("Redo the notebook edit")
                enabled: !!control.notebook && control.notebook.canRedo
                onClicked: control.notebook.redo()
            }
            Tk.ComboBox {
                id: insertType
                objectName: "blockInsertType"
                model: control.blockTypes
                textRole: "label"
                currentIndex: 0
                tooltip: qsTr("Choose a block type to insert")
                enabled: control.capabilities.canInsert
            }
            Tk.Button {
                objectName: "blockInsert"
                text: qsTr("Insert")
                iconName: "plus"
                tooltip: qsTr("Insert a block after the selected block")
                enabled: control.capabilities.canInsert && insertType.currentIndex >= 0
                onClicked: control.insertBlock(control.blockTypes[insertType.currentIndex].id)
            }
        }
        Tk.Caption {
            objectName: "blockEditorError"
            visible: control.activeError.length > 0
            text: control.activeError
            color: Tk.Theme.color.warning
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            objectName: "blockEditorEmpty"
            visible: control.presentedBlocks.length === 0
            text: qsTr("This page is empty. Insert a block to begin.")
            color: Tk.Theme.color.textMuted
        }
        ListView {
            id: blockList
            objectName: "blockEditorList"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            clip: true
            model: control.presentedIds
            reuseItems: true
            cacheBuffer: Tk.Theme.size.panelMinHeight
            spacing: Tk.Theme.space.sm
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Blocks")
            T.ScrollBar.vertical: Tk.ScrollBar { }
            delegate: Tk.BlockEditorBlock {
                editor: control
                width: blockList.width - Tk.Theme.space.sm
            }
        }
    }
}
