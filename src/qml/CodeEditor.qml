// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the TextEdit's own QTextDocument is the
// edit buffer. CodeDocument attaches to it, so IME, cursor, selection, undo,
// diagnostics, syntax highlighting, and file I/O observe the same text.
T.Control {
    id: control
    objectName: "codeEditor"
    property Tk.CodeDocument document: null
    property string tooltip: qsTr("Code editor")
    property bool readOnly: false
    property bool showLineNumbers: true
    property bool showFindBar: true
    property bool ready: false
    property var attachedDocument: null
    property var additionalCursors: []
    property int completionIndex: 0
    property int foldRefreshOffset: 0
    property bool editingMultiple: false
    readonly property alias editor: sourceEdit
    readonly property int cursorPosition: sourceEdit.cursorPosition
    readonly property int selectionStart: sourceEdit.selectionStart
    readonly property int selectionEnd: sourceEdit.selectionEnd
    readonly property int firstVisibleLine: Math.max(0, Math.floor(sourceScroll.contentY / Math.max(1, lineMetrics.height)))
    readonly property int visibleLineCount: Math.min(150, Math.ceil(sourceScroll.height / Math.max(1, lineMetrics.height)) + 3)
    signal documentAttached()

    implicitWidth: Tk.Theme.size.fieldWidth * 3
    implicitHeight: Tk.Theme.size.fieldWidth * 2
    padding: Tk.Theme.space.xs
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    onActiveFocusChanged: if (activeFocus) sourceEdit.forceActiveFocus()
    onDocumentChanged: if (ready) syncDocument()
    Component.onCompleted: {
        ready = true
        syncDocument()
    }
    Component.onDestruction: if (attachedDocument) attachedDocument.detach()

    function syncDocument() {
        if (attachedDocument === document) return
        if (attachedDocument) attachedDocument.detach()
        attachedDocument = document
        additionalCursors = []
        sourceEdit.text = ""
        if (document) {
            document.attach(sourceEdit.textDocument)
            document.tokenColors = ({ keyword: Tk.Theme.color.accentText,
                                      string: Tk.Theme.color.success,
                                      number: Tk.Theme.color.warning,
                                      comment: Tk.Theme.color.textMuted,
                                      type: Tk.Theme.color.info })
            documentAttached()
        }
    }

    function findNext(query, caseSensitive) {
        if (!document) return false
        const hit = document.findNext(query, sourceEdit.selectionEnd,
                                      caseSensitive === undefined ? false : caseSensitive)
        if (hit.start === undefined) return false
        sourceEdit.select(hit.start, hit.end)
        sourceEdit.forceActiveFocus()
        return true
    }

    function goToLine(line) {
        if (!document || line < 1) return false
        const lines = document.text.split("\n")
        if (line > lines.length) return false
        let position = 0
        for (let i = 1; i < line; ++i) position += lines[i - 1].length + 1
        sourceEdit.cursorPosition = position
        sourceEdit.forceActiveFocus()
        return true
    }

    function hasDiagnostic(line) {
        if (!document) return false
        const items = document.diagnostics
        for (let i = 0; i < items.length; ++i)
            if (items[i].line === line) return true
        return false
    }

    function addCursorBelow() {
        if (!document || readOnly) return false
        const positions = [sourceEdit.cursorPosition].concat(additionalCursors)
        const last = positions[positions.length - 1]
        const place = document.lineColumn(last)
        const next = document.positionForLineColumn(place.line + 1, place.column)
        if (next < 0 || positions.indexOf(next) >= 0) return false
        additionalCursors = additionalCursors.concat([next])
        return true
    }

    function clearAdditionalCursors() { additionalCursors = [] }

    function editAllCursors(value, operation) {
        if (!document || readOnly || additionalCursors.length === 0) return false
        editingMultiple = true
        const result = document.editCursors([sourceEdit.cursorPosition].concat(additionalCursors),
                                            value, operation)
        editingMultiple = false
        if (result.length !== additionalCursors.length + 1) return false
        sourceEdit.cursorPosition = result[0]
        additionalCursors = result.slice(1)
        return true
    }

    function acceptCompletion(index) {
        if (!document || readOnly || !document.applyCompletion(index, sourceEdit.cursorPosition)) return false
        sourceEdit.forceActiveFocus()
        return true
    }

    function foldSelection() {
        if (!document) return false
        const anchorPosition = sourceEdit.selectionStart
        const first = document.lineColumn(anchorPosition).line
        const end = document.lineColumn(sourceEdit.selectionEnd)
        const last = end.column === 1 && sourceEdit.selectionEnd > anchorPosition
                     ? end.line - 1 : end.line
        if (last <= first) return false
        const folded = document.foldLines(first, last)
        if (folded) sourceEdit.cursorPosition = anchorPosition
        return folded
    }

    function unfoldAtCursor() {
        if (!document) return false
        return document.unfoldLines(document.lineColumn(sourceEdit.cursorPosition).line)
    }

    Connections {
        target: control.document
        function onTextChanged() {
            if (!control.editingMultiple) control.additionalCursors = []
        }
        function onCompletionsChanged() { control.completionIndex = 0 }
        function onFoldsChanged() {
            if (control.document) {
                const line = control.document.lineColumn(sourceEdit.cursorPosition).line
                for (const fold of control.document.folds) {
                    if (line > fold.start && line <= fold.end) {
                        sourceEdit.cursorPosition = control.document.positionForLineColumn(fold.start, 1)
                        break
                    }
                }
            }
            // A public font geometry change asks QtQuick TextEdit to recalculate
            // contentHeight after QTextDocument block visibility changes.
            control.foldRefreshOffset = 1
            Qt.callLater(() => { control.foldRefreshOffset = 0 })
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: readOnly ? qsTr("Read only code editor") : qsTr("Code editor")

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !sourceEdit.activeFocus
        delay: Tk.Theme.motion.tooltipDelay
    }

    background: Rectangle {
        color: Tk.Theme.color.inputBg
        radius: Tk.Theme.radius.sm
        border.width: Tk.Theme.size.border
        border.color: sourceEdit.activeFocus ? Tk.Theme.color.inputFocusBorder : Tk.Theme.color.inputBorder
    }

    FontMetrics {
        id: lineMetrics
        font.family: Tk.Theme.font.monoFamily
        font.pixelSize: Tk.Theme.font.body
    }

    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Flex {
            visible: control.showFindBar
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: findField
                objectName: "codeFindField"
                Tk.Flex.grow: 1
                placeholderText: qsTr("Find in file")
                tooltip: qsTr("Find text")
                onAccepted: control.findNext(text)
            }
            Tk.Button {
                objectName: "codeFindNext"
                text: qsTr("Next")
                tooltip: qsTr("Find next occurrence")
                onClicked: control.findNext(findField.text)
            }
        }

        Item {
            objectName: "codeViewport"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0

            Rectangle {
                id: gutter
                objectName: "codeLineGutter"
                visible: control.showLineNumbers
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.max(Tk.Theme.size.control,
                                lineMetrics.averageCharacterWidth * (String(Math.max(1, control.document ? control.document.lineCount : 1)).length + 2)
                                + Tk.Theme.space.md)
                color: Tk.Theme.color.panelAlt
                clip: true

                Repeater {
                    model: control.visibleLineCount
                    delegate: Tk.Label {
                        required property int index
                        readonly property int lineNumber: control.document
                            ? control.document.lineForVisualRow(control.firstVisibleLine + index) : -1
                        objectName: "codeLineNumber"
                        x: Tk.Theme.space.xs
                        y: (control.firstVisibleLine + index) * lineMetrics.height - sourceScroll.contentY
                        width: gutter.width - Tk.Theme.space.sm
                        height: lineMetrics.height
                        horizontalAlignment: Text.AlignRight
                        text: control.document && lineNumber > 0
                              ? (control.hasDiagnostic(lineNumber) ? "● " : "") + lineNumber : ""
                        color: control.hasDiagnostic(lineNumber)
                               ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                        font.family: Tk.Theme.font.monoFamily
                        font.pixelSize: Tk.Theme.font.body
                        TapHandler {
                            onTapped: if (control.document) control.document.unfoldLines(parent.lineNumber)
                        }
                    }
                }
            }

            Flickable {
                id: sourceScroll
                objectName: "codeScroll"
                function ensureVisible(x, y, width, height) {
                    if (x < contentX) contentX = x
                    else if (x + width > contentX + this.width) contentX = x + width - this.width
                    if (y < contentY) contentY = y
                    else if (y + height > contentY + this.height) contentY = y + height - this.height
                }
                anchors.left: gutter.visible ? gutter.right : parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                clip: true
                contentWidth: Math.max(width, sourceEdit.contentWidth + Tk.Theme.space.md)
                contentHeight: Math.max(height, sourceEdit.contentHeight + Tk.Theme.space.md)
                boundsBehavior: Flickable.StopAtBounds

                TextEdit {
                    id: sourceEdit
                    objectName: "codeSourceEdit"
                    width: Math.max(sourceScroll.width, contentWidth)
                    height: Math.max(sourceScroll.height, contentHeight)
                    color: Tk.Theme.color.text
                    selectionColor: Tk.Theme.color.selection
                    selectedTextColor: Tk.Theme.color.text
                    font.family: Tk.Theme.font.monoFamily
                    font.pixelSize: Tk.Theme.font.body + control.foldRefreshOffset
                    wrapMode: TextEdit.NoWrap
                    textFormat: TextEdit.PlainText
                    renderType: Text.NativeRendering
                    selectByMouse: true
                    persistentSelection: true
                    readOnly: control.readOnly
                    focus: true
                    leftPadding: Tk.Theme.space.xs
                    topPadding: Tk.Theme.space.xs
                    bottomPadding: Tk.Theme.space.xs
                    onCursorRectangleChanged: sourceScroll.ensureVisible(cursorRectangle.x, cursorRectangle.y,
                                                                         cursorRectangle.width, cursorRectangle.height)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Down
                            && event.modifiers === (Qt.ControlModifier | Qt.AltModifier)) {
                            event.accepted = control.addCursorBelow()
                            return
                        }
                        if (event.key === Qt.Key_BracketLeft
                            && event.modifiers === (Qt.ControlModifier | Qt.ShiftModifier)) {
                            event.accepted = control.foldSelection()
                            return
                        }
                        if (event.key === Qt.Key_BracketRight
                            && event.modifiers === (Qt.ControlModifier | Qt.ShiftModifier)) {
                            event.accepted = control.unfoldAtCursor()
                            return
                        }
                        if (event.key === Qt.Key_Escape && control.additionalCursors.length) {
                            control.clearAdditionalCursors()
                            event.accepted = true
                            return
                        }
                        if (completionPanel.visible && (event.key === Qt.Key_Tab || event.key === Qt.Key_Return)) {
                            event.accepted = control.acceptCompletion(control.completionIndex)
                            return
                        }
                        if (completionPanel.visible && event.key === Qt.Key_Down) {
                            control.completionIndex = Math.min(control.document.completions.length - 1,
                                                               control.completionIndex + 1)
                            event.accepted = true
                            return
                        }
                        if (completionPanel.visible && event.key === Qt.Key_Up) {
                            control.completionIndex = Math.max(0, control.completionIndex - 1)
                            event.accepted = true
                            return
                        }
                        if (!sourceEdit.inputMethodComposing && control.additionalCursors.length) {
                            let action = ""
                            if (event.key === Qt.Key_Backspace) action = "backspace"
                            else if (event.key === Qt.Key_Delete) action = "delete"
                            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) action = "insert"
                            else if (event.text.length && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)))
                                action = "insert"
                            if (action.length) {
                                event.accepted = control.editAllCursors(event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                                                                        ? "\n" : event.text, action)
                                return
                            }
                        }
                        if (event.key === Qt.Key_F && event.modifiers === Qt.ControlModifier && control.showFindBar) {
                            findField.forceActiveFocus()
                            findField.selectAll()
                            event.accepted = true
                        }
                    }
                    Accessible.role: Accessible.EditableText
                    Accessible.name: control.tooltip
                    Accessible.multiLine: true
                    Accessible.editable: !control.readOnly

                    TapHandler {
                        id: addCaretTap
                        acceptedModifiers: Qt.AltModifier
                        property int primaryBefore: -1
                        onPressedChanged: if (pressed) primaryBefore = sourceEdit.cursorPosition
                        onTapped: eventPoint => {
                            const position = sourceEdit.positionAt(eventPoint.position.x, eventPoint.position.y)
                            if (primaryBefore >= 0) sourceEdit.cursorPosition = primaryBefore
                            if (position >= 0 && control.additionalCursors.indexOf(position) < 0
                                && position !== primaryBefore)
                                control.additionalCursors = control.additionalCursors.concat([position])
                        }
                    }
                }

                Repeater {
                    model: control.additionalCursors
                    delegate: Rectangle {
                        required property int modelData
                        readonly property rect caret: sourceEdit.positionToRectangle(modelData)
                        x: caret.x
                        y: caret.y
                        width: Tk.Theme.size.border
                        height: caret.height
                        color: Tk.Theme.color.accent
                    }
                }

                Rectangle {
                    id: completionPanel
                    objectName: "codeCompletionPanel"
                    visible: !!control.document && control.document.completions.length > 0
                             && sourceEdit.cursorPosition === control.document.completionEnd
                    x: Math.max(sourceScroll.contentX,
                                Math.min(sourceScroll.contentX + sourceScroll.width - width,
                                         sourceEdit.cursorRectangle.x))
                    y: sourceEdit.cursorRectangle.y + sourceEdit.cursorRectangle.height + height
                       <= sourceScroll.contentY + sourceScroll.height
                       ? sourceEdit.cursorRectangle.y + sourceEdit.cursorRectangle.height
                       : sourceEdit.cursorRectangle.y - height
                    z: 2
                    width: Tk.Theme.size.fieldWidth
                    height: completionList.implicitHeight + Tk.Theme.space.xs * 2
                    color: Tk.Theme.color.panel
                    border.width: Tk.Theme.size.border
                    border.color: Tk.Theme.color.inputBorder
                    radius: Tk.Theme.radius.sm
                    Tk.Flex {
                        id: completionList
                        anchors.fill: parent
                        anchors.margins: Tk.Theme.space.xs
                        direction: Tk.Flex.Column
                        Repeater {
                            model: control.document ? control.document.completions : []
                            delegate: Tk.Label {
                                required property var modelData
                                required property int index
                                objectName: "codeCompletionItem"
                                width: completionList.width
                                text: modelData.label
                                color: index === control.completionIndex
                                       ? Tk.Theme.color.accentText : Tk.Theme.color.text
                                TapHandler { onTapped: control.acceptCompletion(index) }
                            }
                        }
                    }
                }
            }
        }

        Tk.Label {
            objectName: "codeStatus"
            text: {
                if (!control.document) return qsTr("No document")
                const place = control.document.lineColumn(sourceEdit.cursorPosition)
                return qsTr("Ln %1, Col %2 · rev %3%4")
                    .arg(place.line).arg(place.column).arg(control.document.revision)
                    .arg(control.document.modified ? " *" : "")
            }
            color: Tk.Theme.color.textMuted
            font.family: Tk.Theme.font.monoFamily
        }
    }
}
