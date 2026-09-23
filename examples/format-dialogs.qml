// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: Tk.Theme.size.dialogWidth * 2
    height: Tk.Theme.size.dialogWidth * 1.5
    property var currentSession: manager.activeSession

    Component.onCompleted: importDialog.open()

    Tk.FormatRegistry { id: registry }
    Tk.DocumentManager { id: manager }
    Tk.FileDialogAdapter { id: filePicker }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Overline { title: qsTr("Format jobs"); wide: true }
        Tk.Label {
            text: qsTr("Only providers that implement the selected operation are offered. This reference build includes native reference documents and UTF-8 text.")
            wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button { text: qsTr("Create reference document"); onClicked: manager.createReferenceDocument() }
            Tk.Button { text: qsTr("Import…"); onClicked: importDialog.open() }
            Tk.Button { text: qsTr("Export…"); onClicked: exportDialog.open() }
        }
        Tk.Label {
            text: root.currentSession ? qsTr("Active session: %1").arg(root.currentSession.title) : qsTr("No active session")
            color: Tk.Theme.color.textMuted
        }
    }

    Tk.ImportDialog {
        id: importDialog
        formatRegistry: registry
        fileDialogAdapter: filePicker
        documentManager: manager
        onImportAdopted: function(session) { manager.activate(session.documentId) }
    }
    Tk.ExportDialog {
        id: exportDialog
        formatRegistry: registry
        fileDialogAdapter: filePicker
        documentSession: root.currentSession
    }
}
