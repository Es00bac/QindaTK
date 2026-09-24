// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Extensions"
    note: "ExtensionManager · saved-state diagnostics · explicit native load"

    Tk.ExtensionRegistry {
        id: registry
        Component.onCompleted: restoreState([
            {id: "org.qinda.example.editor", version: "1.4.0",
             descriptorPath: "/opt/qinda/extensions/editor/manifest.json", state: {}}
        ])
    }
    Item {
        implicitWidth: Tk.Theme.size.panelMinWidth * 2
        implicitHeight: Tk.Theme.size.row * 11
        Tk.ExtensionManager {
            anchors.fill: parent
            registry: registry
        }
    }
}
