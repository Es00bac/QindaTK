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
                        readonly property int lineNumber: control.firstVisibleLine + index + 1
                        objectName: "codeLineNumber"
                        x: Tk.Theme.space.xs
                        y: (lineNumber - 1) * lineMetrics.height - sourceScroll.contentY
                        width: gutter.width - Tk.Theme.space.sm
                        height: lineMetrics.height
                        horizontalAlignment: Text.AlignRight
                        text: control.document && lineNumber <= control.document.lineCount
                              ? (control.hasDiagnostic(lineNumber) ? "● " : "") + lineNumber : ""
                        color: control.hasDiagnostic(lineNumber)
                               ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                        font.family: Tk.Theme.font.monoFamily
                        font.pixelSize: Tk.Theme.font.body
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
                    font.pixelSize: Tk.Theme.font.body
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
