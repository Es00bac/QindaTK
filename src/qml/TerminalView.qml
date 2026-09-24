// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: control
    objectName: "terminalView"
    property Tk.TerminalSession session: null
    property string tooltip: qsTr("Terminal screen and keyboard input")
    readonly property real _cellWidth: metrics.advanceWidth("M")
    readonly property real _cellHeight: Math.ceil(metrics.height)
    readonly property var _rows: {
        const result = []
        if (!session) return result
        for (let i = 0; i < session.scrollback.length; ++i) result.push(session.scrollback[i])
        for (let i = 0; i < session.screen.length; ++i) result.push(session.screen[i])
        return result
    }
    readonly property bool _hasOutput: {
        if (!session) return false
        for (let i = 0; i < session.scrollback.length; ++i)
            if (String(session.scrollback[i]).trim().length) return true
        for (let i = 0; i < session.screen.length; ++i) {
            const row = session.screen[i]
            if (row && String(row.text || "").trim().length) return true
        }
        return false
    }

    implicitWidth: Tk.Theme.size.fieldWidth * 4
    implicitHeight: Tk.Theme.size.control * 8
    Accessible.role: Accessible.Terminal
    Accessible.name: tooltip
    Accessible.description: session && session.running ? qsTr("Process running")
                                                       : qsTr("Process stopped")
    focusPolicy: Qt.StrongFocus

    FontMetrics {
        id: metrics
        font.family: Tk.Theme.font.monoFamily
        font.pixelSize: Tk.Theme.font.small
    }

    function updateGrid() {
        if (!session || viewport.viewportWidth <= 0 || viewport.viewportHeight <= 0) return
        const rows = Math.max(2, Math.min(100, Math.floor(viewport.viewportHeight / _cellHeight)))
        const columns = Math.max(2, Math.min(240, Math.floor(viewport.viewportWidth / _cellWidth)))
        if (session.rows !== rows || session.columns !== columns) session.resize(rows, columns)
    }

    function escapeHtml(value) {
        return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;")
            .replace(/>/g, "&gt;").replace(/\"/g, "&quot;").replace(/'/g, "&#39;")
    }

    function rowMarkup(row) {
        if (!row || typeof row === "string" || !row.spans || !row.spans.length)
            return escapeHtml(typeof row === "string" ? row : row ? row.text : "")
        let markup = ""
        for (let i = 0; i < row.spans.length; ++i) {
            const span = row.spans[i]
            let style = ""
            if (span.foreground) style += "color:" + span.foreground + ";"
            if (span.background) style += "background-color:" + span.background + ";"
            if (span.bold) style += "font-weight:bold;"
            if (span.italic) style += "font-style:italic;"
            if (span.underline) style += "text-decoration:underline;"
            if (span.reverse) style += "color:" + (span.background || Tk.Theme.color.inputBg)
                + ";background-color:" + (span.foreground || Tk.Theme.color.text) + ";"
            const text = escapeHtml(span.text)
            markup += style.length ? "<span style=\"" + style + "\">" + text + "</span>" : text
        }
        return markup
    }

    onWidthChanged: updateGrid()
    onHeightChanged: updateGrid()
    Component.onCompleted: updateGrid()

    background: Rectangle {
        color: Tk.Theme.color.inputBg
        radius: Tk.Theme.radius.sm
        border.width: Tk.Theme.size.border
        border.color: control.activeFocus ? Tk.Theme.color.inputFocusBorder
                                          : Tk.Theme.color.inputBorder
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.size.border

        Tk.Flex {
            objectName: "terminalStatusBar"
            width: parent.width
            height: Tk.Theme.size.control
            implicitHeight: Tk.Theme.size.control
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            padding: Tk.Theme.space.sm
            gap: Tk.Theme.space.sm
            Rectangle {
                width: Tk.Theme.size.iconSm / 2
                height: width
                radius: Tk.Theme.radius.full
                color: control.session && control.session.running ? Tk.Theme.color.success
                                                                  : Tk.Theme.color.textMuted
            }
            Tk.Label {
                objectName: "terminalProcessStatus"
                text: !control.session ? qsTr("No session")
                    : control.session.running ? qsTr("Running")
                    : control.session.errorString.length ? control.session.errorString
                    : control.session.exitCode >= 0 ? qsTr("Exited · %1").arg(control.session.exitCode)
                    : qsTr("Ready")
                mono: true
                font.pixelSize: Tk.Theme.font.small
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption {
                text: control.session ? control.session.rows + " × " + control.session.columns : ""
                mono: true
            }
        }

        Tk.Scroll {
            id: viewport
            objectName: "terminalViewport"
            Tk.Flex.grow: 1
            width: parent.width
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Auto
            padding: Tk.Theme.space.sm
            accessibleName: control.tooltip
            onViewportWidthChanged: control.updateGrid()
            onViewportHeightChanged: control.updateGrid()
            onContentHeightChanged: Qt.callLater(scrollToBottom)

            Item {
                id: screenSurface
                implicitWidth: terminalRows.width
                implicitHeight: terminalRows.implicitHeight
                Tk.Flex {
                    id: terminalRows
                    objectName: "terminalRows"
                    width: Math.max(viewport.viewportWidth,
                                    control.session ? control.session.columns * control._cellWidth : 0)
                    direction: Tk.Flex.Column
                    gap: 0
                    Repeater {
                        model: control._rows
                        delegate: Tk.Label {
                            required property int index
                            required property var modelData
                            objectName: "terminalLine_" + index
                            width: Math.max(viewport.viewportWidth,
                                            control.session ? control.session.columns * control._cellWidth : 0)
                            height: control._cellHeight
                            text: control.rowMarkup(modelData)
                            mono: true
                            font.pixelSize: Tk.Theme.font.small
                            wrapMode: Text.NoWrap
                            elide: Text.ElideNone
                            selectable: true
                            textFormat: Text.RichText
                            Accessible.name: qsTr("Terminal line %1").arg(index + 1) + ": " + text
                        }
                    }
                }
                Rectangle {
                    objectName: "terminalCursor"
                    x: control.session ? control.session.cursorColumn * control._cellWidth : 0
                    y: control.session
                        ? (control.session.scrollback.length + control.session.cursorRow) * control._cellHeight : 0
                    width: control._cellWidth
                    height: control._cellHeight
                    color: Tk.Theme.color.accent
                    opacity: Tk.Theme.opacity.muted
                    visible: control.session && control.session.cursorVisible
                             && (control.session.running || control._hasOutput)
                    Accessible.ignored: true
                }
            }
        }

        // TextInput owns IME and system clipboard paste. Its short-lived text
        // buffer forwards inserted text to the PTY, then clears immediately.
        TextInput {
            id: inputProxy
            objectName: "terminalInput"
            anchors.left: parent.left
            anchors.top: parent.top
            z: 2
            width: 1
            height: 1
            opacity: 0
            color: "transparent"
            activeFocusOnTab: true
            Accessible.role: Accessible.EditableText
            Accessible.name: control.tooltip
            onTextEdited: {
                if (control.session && text.length) control.session.sendText(text)
                clear()
            }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                if (!control.session || !control.session.running) return
                const keys = {}
                keys[Qt.Key_Return] = "\r"; keys[Qt.Key_Enter] = "\r"
                keys[Qt.Key_Backspace] = "\x7f"; keys[Qt.Key_Tab] = "\t"
                keys[Qt.Key_Escape] = "\x1b"; keys[Qt.Key_Up] = "\x1b[A"
                keys[Qt.Key_Down] = "\x1b[B"; keys[Qt.Key_Right] = "\x1b[C"
                keys[Qt.Key_Left] = "\x1b[D"; keys[Qt.Key_Home] = "\x1b[H"
                keys[Qt.Key_End] = "\x1b[F"; keys[Qt.Key_Delete] = "\x1b[3~"
                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) {
                    control.session.sendText("\x03"); event.accepted = true
                } else if (keys[event.key] !== undefined) {
                    control.session.sendText(keys[event.key]); event.accepted = true
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: inputProxy.forceActiveFocus()
    }

    Connections {
        target: control.session
        function onScreenChanged() {
            if (viewport.contentY + viewport.viewportHeight >= viewport.contentHeight - control._cellHeight)
                Qt.callLater(viewport.scrollToBottom)
        }
    }

    Tk.Caption {
        objectName: "terminalEmptyPrompt"
        anchors.centerIn: control
        visible: !control._hasOutput && (!control.session || !control.session.running)
        text: qsTr("No process output · start a process from the host")
        color: Tk.Theme.color.textMuted
        Accessible.role: Accessible.StatusBar
    }
}
