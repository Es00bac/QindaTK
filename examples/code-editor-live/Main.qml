// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: fixture
    width: 1120
    height: 680

    Tk.CodeDocument {
        id: source
        text: "// QindaTK code editor — edit this Unicode source\n"
            + "function greet(name) {\n"
            + "    const message = \"Hello, \" + name;\n"
            + "    return message;\n"
            + "}\n\n"
            + "const café = greet(\"🦊 fox\");\n"
            + "console.log(café);\n"
            + "undefinedSymbol(); // live diagnostic\n"
    }

    function publishProviderResults() {
        const revision = source.revision
        const value = source.text
        const tokens = []
        const rules = [
            { kind: "comment", pattern: /\/\/[^\n]*/g },
            { kind: "string", pattern: /"(?:[^"\\]|\\.)*"/g },
            { kind: "keyword", pattern: /\b(?:function|const|return|let|var)\b/g },
            { kind: "number", pattern: /\b\d+\b/g }
        ]
        for (const rule of rules) {
            let hit
            while ((hit = rule.pattern.exec(value)) !== null)
                tokens.push({ start: hit.index, length: hit[0].length, kind: rule.kind })
        }
        source.publishSyntaxTokens(revision, tokens)
        const bad = value.indexOf("undefinedSymbol")
        source.publishDiagnostics(revision, bad < 0 ? [] : [
            { start: bad, length: "undefinedSymbol".length,
              severity: "error", message: "Unknown symbol in sample provider" }
        ])
    }

    function showAuthoringFeatures() {
        source.foldLines(2, 4)
        const start = source.text.indexOf("undefinedSymbol")
        const end = start + "undefinedSymbol".length
        editor.editor.cursorPosition = end
        editor.additionalCursors = [source.positionForLineColumn(7, 7)]
        source.publishCompletions(source.revision, start, end, [
            { label: "definedSymbol()", insertText: "definedSymbol" },
            { label: "documentSymbol()", insertText: "documentSymbol" }
        ])
    }

    Timer {
        id: providerTimer
        interval: 90
        onTriggered: fixture.publishProviderResults()
    }
    Connections {
        target: source
        function onTextChanged() { providerTimer.restart() }
    }
    Component.onCompleted: publishProviderResults()

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Label { text: qsTr("Live code editor"); font.pixelSize: Tk.Theme.font.title }
        Tk.Label {
            text: qsTr("Qt's native text buffer supplies Unicode editing and IME; revision-tagged providers supply colors and diagnostics.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.flex: 1
            Tk.CodeEditor {
                id: editor
                objectName: "liveCodeEditor"
                Tk.Flex.flex: 1
                document: source
                tooltip: qsTr("Editable source code")
                onDocumentAttached: {
                    fixture.publishProviderResults()
                    Qt.callLater(fixture.showAuthoringFeatures)
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Flex.basis: 270
                Tk.Flex.shrink: 0
                gap: Tk.Theme.space.sm
                Tk.Label { text: qsTr("Provider output"); font.pixelSize: Tk.Theme.font.body }
                Tk.Label { text: qsTr("Revision %1 · %2 lines").arg(source.revision).arg(source.lineCount) }
                Tk.Label {
                    text: qsTr("Syntax spans: %1").arg(source.syntaxTokens.length)
                    muted: true
                }
                Tk.Label {
                    text: source.diagnostics.length > 0
                          ? qsTr("Line %1: %2").arg(source.diagnostics[0].line)
                            .arg(source.diagnostics[0].message)
                          : qsTr("No diagnostics")
                    color: source.diagnostics.length > 0 ? Tk.Theme.color.danger : Tk.Theme.color.success
                    wrapMode: Text.Wrap
                }
                Tk.Label {
                    text: qsTr("The function body starts folded. Click its line number to expand it. Ctrl+Alt+Down adds a caret; Alt+click places one. Suggestions are revision-tagged and Tab applies one.")
                    muted: true
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
