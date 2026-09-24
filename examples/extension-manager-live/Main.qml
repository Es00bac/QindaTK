// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 900
    height: 600
    color: Tk.Theme.color.bg

    Tk.ExtensionRegistry { id: extensions }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg
        Tk.Heading { text: qsTr("Local extension manager") }
        Tk.Caption {
            text: qsTr("Add a local directory to discover descriptors. Review each plugin before choosing Load. Saved records remain visible when missing.")
        }
        Tk.ExtensionManager {
            objectName: "liveExtensionManager"
            registry: extensions
            Tk.Flex.grow: 1
        }
    }

    Component.onCompleted: extensions.restoreState([
        {id: "org.qinda.example.editor", version: "1.4.0",
         descriptorPath: "/opt/qinda/extensions/editor/manifest.json", state: {}},
        {id: "org.qinda.example.media", version: "2.0.0",
         descriptorPath: "/opt/qinda/extensions/media/manifest.json", state: {}}
    ])
}
