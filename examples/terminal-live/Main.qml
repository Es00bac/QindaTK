// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 960
    height: 600
    color: Tk.Theme.color.bg

    Tk.TerminalSession { id: terminal }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg

        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Local terminal"); wide: true }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button {
                objectName: "terminalStartDemo"
                text: qsTr("Start demo")
                tooltip: qsTr("Start the explicit interactive shell demo")
                enabled: !terminal.running
                onClicked: terminal.start("/bin/sh", ["-c",
                    "printf '\\033[1;36mQindaTK PTY demo\\033[0m\\n'; printf 'Type a line and press Enter: '; IFS= read -r answer; printf '\\nYou entered: %s\\n' \"$answer\"; printf 'The process will remain open for 30 seconds.\\n'; sleep 30"], "/tmp")
            }
            Tk.Button {
                text: qsTr("Stop")
                variant: "outline"
                tooltip: qsTr("Stop the running terminal process")
                enabled: terminal.running
                onClicked: terminal.stop()
            }
        }

        Tk.TerminalView {
            objectName: "liveTerminalView"
            Tk.Flex.grow: 1
            width: parent.width
            session: terminal
        }

        Tk.Caption {
            width: parent.width
            text: qsTr("The PTY starts only when you click Start demo. Click the screen, type a response, and press Enter.")
        }
    }
}
