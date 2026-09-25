// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Internal part of Tk.BlockEditor: the text field of a text, heading, list,
// quote, to-do or code block, with the editor's cross-block keyboard rules
// (docs/notebooks.md#keyboard).
Item {
    id: root
    property var editor: null // the owning Tk.BlockEditor (var: a typed property would make a load cycle)
    property var block: ({})
    readonly property alias textEditor: field
    readonly property string blockId: block && block.id ? block.id : ""
    readonly property string blockType: block && block.type ? block.type : "text"
    readonly property var blockData: block && block.data ? block.data : ({})
    readonly property string committedText: blockData.text !== undefined ? String(blockData.text) : ""
    readonly property bool formatted: !!blockData.runs && blockData.runs.length > 0
    readonly property bool hasDraft: !!editor && editor.drafts[blockId] !== undefined
    readonly property bool locked: !!(block && block.locked)

    implicitWidth: field.implicitWidth
    implicitHeight: field.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("%1 text").arg(blockType)

    // Shows the committed text or this block's draft. A delegate recycled
    // for another block, or rebuilt after a structural edit, calls it again.
    function load() {
        if (!editor || blockId.length === 0) return
        const text = editor.draftText(blockId, committedText)
        if (field.text !== text) field.text = text
        claimFocus()
    }
    // Takes focus and restores the caret when the editor asked for this
    // block (after split, merge, paste or navigation) or, in host mode, when
    // the host's focusedBlockId names it.
    function claimFocus() {
        if (!editor || blockId.length === 0) return
        const wanted = editor.notebook ? editor.pendingFocusId === blockId : editor.focusedBlockId === blockId
        if (!wanted) return
        const draft = editor.drafts[blockId]
        const target = draft !== undefined ? draft.cursor : editor.caretOffset
        if (editor.notebook) editor.pendingFocusId = ""
        field.forceActiveFocus()
        field.cursorPosition = target >= 0 ? Math.min(target, field.length) : field.length
    }
    onBlockChanged: load()
    Component.onCompleted: load()
    Connections {
        target: root.editor
        function onFocusedBlockIdChanged() { root.claimFocus() }
        function onPendingFocusIdChanged() { root.claimFocus() }
    }

    function firstLine() { return field.positionToRectangle(field.cursorPosition).y <= field.positionToRectangle(0).y }
    function lastLine() { return field.positionToRectangle(field.cursorPosition).y >= field.positionToRectangle(field.length).y }

    Tk.TextArea {
        id: field
        objectName: "blockText_" + root.blockId
        anchors.left: parent.left
        anchors.right: parent.right
        placeholderText: root.blockType === "code" ? qsTr("Write JavaScript") : qsTr("Write in this block")
        rows: root.blockType === "code" ? 4 : root.blockType === "heading" ? 1 : 2
        mono: root.blockType === "code"
        font.pixelSize: root.blockType !== "heading" ? Tk.Theme.font.body
                        : root.blockData.level === 1 ? Tk.Theme.font.title
                        : root.blockData.level === 2 ? Tk.Theme.font.large : Tk.Theme.font.medium
        font.bold: root.blockType === "heading"
        font.italic: root.blockType === "quote"
        readOnly: root.locked || !root.editor || !root.editor.capabilities.canEdit
        color: preview.visible ? Tk.Theme.alpha(Tk.Theme.color.text, Tk.Theme.opacity.hidden) : Tk.Theme.color.text
        tooltip: root.blockType === "code" ? qsTr("Code block content") : qsTr("Text block content")
        onTextChanged: if (activeFocus && root.editor)
                           root.editor.storeDraft(root.blockId, text, cursorPosition, root.committedText)
        onEditingFinished: if (root.editor && !root.locked) root.editor.submitText(root.block, text)
        onActiveFocusChanged: if (activeFocus && root.editor) {
            if (root.editor.notebook) root.editor.selectedBlockId = root.blockId
            root.editor.blockFocusRequested(root.blockId)
            root.editor.blockSelectionRequested(root.blockId)
        }
        Keys.onPressed: function(event) {
            const ed = root.editor
            if (!ed) return
            const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
            const shift = (event.modifiers & Qt.ShiftModifier) !== 0
            const alt = (event.modifiers & Qt.AltModifier) !== 0
            const start = field.selectionStart, end = field.selectionEnd
            // Structural edits rebuild this card; they return the caret here.
            if (ed.notebook) ed.caretOffset = field.cursorPosition
            let handled = true
            if (ctrl && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                ed.storeDraft(root.blockId, field.text, field.cursorPosition, root.committedText)
                ed.splitBlock(root.block, field.cursorPosition)
            } else if (alt && event.key === Qt.Key_Up) {
                ed.moveBlock(root.blockId, -1)
            } else if (alt && event.key === Qt.Key_Down) {
                ed.moveBlock(root.blockId, 1)
            } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                ed.nestBlock(root.blockId, event.key === Qt.Key_Backtab || shift ? "outdent" : "indent")
            } else if (shift && event.key === Qt.Key_Up && root.firstLine()) {
                ed.extendSelection(root.blockId, field.cursorPosition, "up")
            } else if (shift && event.key === Qt.Key_Down && root.lastLine()) {
                ed.extendSelection(root.blockId, field.cursorPosition, "down")
            } else if (!shift && event.key === Qt.Key_Up && root.firstLine()) {
                ed.navigate(root.blockId, field.cursorPosition, "up")
            } else if (!shift && event.key === Qt.Key_Down && root.lastLine()) {
                ed.navigate(root.blockId, field.cursorPosition, "down")
            } else if (!shift && event.key === Qt.Key_Left && start === end && field.cursorPosition === 0) {
                ed.navigate(root.blockId, 0, "left")
            } else if (!shift && event.key === Qt.Key_Right && start === end && field.cursorPosition === field.length) {
                ed.navigate(root.blockId, field.length, "right")
            } else if (event.key === Qt.Key_Backspace && ed.hasBlockSelection()) {
                ed.deleteSelection()
            } else if (event.key === Qt.Key_Backspace && start === end && field.cursorPosition === 0) {
                ed.storeDraft(root.blockId, field.text, 0, root.committedText)
                ed.mergeWithPrevious(root.block)
            } else if (ctrl && event.key === Qt.Key_C && ed.notebook && ed.clipboard) {
                ed.storeDraft(root.blockId, field.text, field.cursorPosition, root.committedText)
                ed.commitDraft(root.blockId)
                ed.copy(root.blockId, start, end)
            } else if (ctrl && event.key === Qt.Key_X && ed.notebook && ed.clipboard) {
                ed.storeDraft(root.blockId, field.text, field.cursorPosition, root.committedText)
                ed.cut(root.blockId, start, end)
            } else if (ctrl && event.key === Qt.Key_V && ed.notebook && ed.clipboard) {
                ed.storeDraft(root.blockId, field.text, field.cursorPosition, root.committedText)
                ed.paste(root.blockId, start)
            } else if (ctrl && (event.key === Qt.Key_B || event.key === Qt.Key_I || event.key === Qt.Key_U) && ed.notebook) {
                const key = event.key === Qt.Key_B ? "bold" : event.key === Qt.Key_I ? "italic" : "underline"
                const patch = ({})
                patch[key] = true
                ed.storeDraft(root.blockId, field.text, field.cursorPosition, root.committedText)
                ed.formatSelection(root.blockId, start, end, patch)
            } else {
                handled = false
                if (ed.hasBlockSelection() && !shift) ed.selection = ({})
            }
            event.accepted = handled
        }
    }
    // Formatted runs shown while the field is not being edited.
    Tk.Label {
        id: preview
        objectName: "blockRichPreview_" + root.blockId
        anchors.fill: field
        leftPadding: field.leftPadding
        rightPadding: field.rightPadding
        topPadding: field.topPadding
        bottomPadding: field.bottomPadding
        verticalAlignment: Text.AlignTop
        visible: root.formatted && !field.activeFocus && !root.hasDraft
        textFormat: Text.StyledText
        text: root.editor ? root.editor.runsHtml(root.blockData) : ""
        mono: field.mono
        font.pixelSize: field.font.pixelSize
        font.bold: field.font.bold
        font.italic: field.font.italic
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        linkColor: Tk.Theme.color.accentText
        onLinkActivated: function(link) { if (root.editor) root.editor.openLink(root.blockId, link) }
        TapHandler { onTapped: field.forceActiveFocus() }
    }
}
