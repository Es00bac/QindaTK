// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 900
    height: 600
    color: Tk.Theme.color.bg

    Tk.ScriptRuntime {
        id: runtime
        documentSnapshot: ({title: "Untitled canvas", revision: 7,
                            selection: {kind: "rectangle", count: 3}})
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg
        Tk.Overline { title: qsTr("Live local scripting runtime"); wide: true }
        Tk.Caption {
            text: qsTr("Only deliberate JSON snapshots and host-exposed commands enter scripts.")
        }
        Tk.ScriptConsole {
            objectName: "liveScriptConsole"
            width: parent.width
            Tk.Flex.grow: 1
            runtime: runtime
            sourceText: "app.print('Document: ' + app.inspect('title'));\n"
                        + "app.print('Revision: ' + app.inspect('revision'));\n"
                        + "app.print('Selected: ' + app.inspect('selection').count);\n"
                        + "return {ready: true, selection: app.inspect('selection').kind};"
        }
    }

    Component.onCompleted: runtime.execute(
        "app.print('Document: ' + app.inspect('title'));" +
        "app.print('Revision: ' + app.inspect('revision'));" +
        "app.print('Selected: ' + app.inspect('selection').count);" +
        "return {ready: true};")
}
