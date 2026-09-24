// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Flex {
    id: control
    objectName: "scriptConsole"
    property Tk.ScriptRuntime runtime: null
    property string tooltip: qsTr("Script console")
    property alias sourceText: sourceEditor.text
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.md
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        width: parent.width
        Tk.Overline { title: qsTr("Script console"); wide: true; Tk.Flex.grow: 1 }
        Tk.Caption {
            objectName: "scriptStatus"
            text: control.runtime ? control.runtime.status : qsTr("No runtime")
        }
    }

    Tk.TextArea {
        id: sourceEditor
        objectName: "scriptSource"
        width: parent.width
        Tk.Flex.grow: 1
        rows: 7
        mono: true
        placeholderText: qsTr("app.print(app.inspect('title'));")
        tooltip: qsTr("JavaScript source. Control and Enter runs it")
        Keys.onPressed: function(event) {
            if ((event.modifiers & Qt.ControlModifier)
                    && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                if (control.runtime && !control.runtime.running)
                    control.runtime.execute(sourceEditor.text)
                event.accepted = true
            }
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        width: parent.width
        Tk.Button {
            objectName: "scriptRun"
            text: qsTr("Run")
            iconName: "play"
            variant: "accent"
            available: !!control.runtime && !control.runtime.running
            onClicked: control.runtime.execute(sourceEditor.text)
        }
        Tk.Button {
            objectName: "scriptCancel"
            text: qsTr("Cancel")
            iconName: "square"
            available: !!control.runtime && control.runtime.running
            onClicked: control.runtime.cancel()
        }
        Tk.Button {
            objectName: "scriptReplay"
            text: qsTr("Replay")
            iconName: "rotate-cw"
            available: !!control.runtime && !control.runtime.running
                     && control.runtime.recordedCommands.length > 0
            onClicked: control.runtime.replayRecorded()
        }
        Tk.Spacer { Tk.Flex.grow: 1 }
        Tk.Caption { text: qsTr("Ctrl+Enter to run") }
    }

    Tk.Overline { title: qsTr("Output"); wide: true }
    Tk.TextArea {
        objectName: "scriptOutput"
        width: parent.width
        height: Tk.Theme.size.row * 6
        rows: 6
        mono: true
        readOnly: true
        tooltip: qsTr("Script output and errors")
        error: !!control.runtime && control.runtime.errorString.length > 0
        text: !control.runtime ? ""
              : control.runtime.errorString.length > 0
                ? control.runtime.output + (control.runtime.output ? "\n" : "")
                  + control.runtime.errorString
                : control.runtime.output
    }
}
