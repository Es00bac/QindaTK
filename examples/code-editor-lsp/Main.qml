// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

Item {
    id: fixture
    width: 1120
    height: 680
    property bool autoStart: false

    Tk.CodeDocument {
        id: source
        text: "// Explicit local clangd session through QindaTK\n"
              + "int alphaSymbol = 3;\n"
              + "int main() {\n"
              + "    int value = unknownName;\n"
              + "    alp\n"
              + "    return value;\n"
              + "}\n"
    }

    Tk.CodeLanguageService {
        id: service
        document: source
        program: "clangd"
        arguments: ["--background-index=false", "--log=error"]
        languageId: "cpp"
        onStatusChanged: if (fixture.autoStart && status === "ready") completionTimer.start()
    }

    Timer {
        id: completionTimer
        interval: 350
        onTriggered: {
            const end = source.text.indexOf("alp\n") + 3
            editor.editor.cursorPosition = end
            service.requestCompletion(end, end - 3)
        }
    }

    Component.onCompleted: if (autoStart) service.start()

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Label { text: qsTr("Code editor + local language service"); font.pixelSize: Tk.Theme.font.title }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button {
                text: qsTr("Start clangd")
                tooltip: qsTr("Launch the configured local language server")
                enabled: service.status === "stopped" || service.status === "error"
                onClicked: service.start()
            }
            Tk.Button {
                text: qsTr("Stop")
                tooltip: qsTr("Close the language server and release its process")
                enabled: service.status === "ready" || service.status === "starting"
                onClicked: service.stop()
            }
            Tk.Label { text: qsTr("Server: %1").arg(service.status) }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.flex: 1
            Tk.CodeEditor {
                id: editor
                objectName: "liveLspEditor"
                Tk.Flex.flex: 1
                document: source
                languageService: service
                tooltip: qsTr("C++ source editor with optional local clangd")
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Flex.basis: 270
                Tk.Flex.shrink: 0
                gap: Tk.Theme.space.sm
                Tk.Label { text: qsTr("Live provider results"); font.pixelSize: Tk.Theme.font.body }
                Tk.Label { text: qsTr("Diagnostics: %1").arg(source.diagnostics.length) }
                Tk.Label {
                    text: source.diagnostics.length
                          ? source.diagnostics[0].message : qsTr("No current diagnostic")
                    wrapMode: Text.Wrap
                    color: source.diagnostics.length ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                }
                Tk.Label {
                    text: service.errorString.length ? service.errorString
                          : qsTr("Start clangd to see a real C++ diagnostic. Place the caret after 'alp' and press Ctrl+Space for completion. Edits and process stop clear stale results.")
                    muted: true
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
