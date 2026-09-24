// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    property var host
    property var activeSession
    property string activeDocumentId: ""
    property string inspectedRowId: ""
    property alias documentPath: pathField.text
    property alias workspacePath: workspacePathField.text
    signal statusRequested(string message)
    implicitHeight: actions.implicitHeight

    Tk.Flex {
        id: actions
        width: parent.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Divider {}
        Tk.Caption {
            text: qsTr("Grid and Inspector edits commit to the active native document. Undo uses its shared transaction history.")
            wrapMode: Text.Wrap
        }
        Tk.TextField {
            id: pathField
            objectName: "workbenchPathField"
            placeholderText: qsTr("Absolute .qtk path")
            tooltip: qsTr("Native reference document Save As, open and recovery path")
        }
        Tk.Button {
            objectName: "workbenchSaveAsAction"
            text: qsTr("Save As path")
            tooltip: qsTr("Save the active document to this native reference path")
            enabled: !!root.activeSession && !root.activeSession.saving
            onClicked: {
                const started = root.activeSession.requestSaveAs(pathField.text.trim(), false)
                root.statusRequested(started ? qsTr("Saving native reference document")
                                             : root.activeSession.lastError)
            }
        }
        Tk.Button {
            objectName: "workbenchOpenAction"
            text: qsTr("Open path")
            tooltip: qsTr("Open a saved native reference document from this path")
            onClicked: { root.host.openReferencePath(pathField.text); root.statusRequested(root.host.workflowMessage) }
        }
        Tk.Button {
            objectName: "workbenchRecoveryAction"
            text: qsTr("Restore recovery")
            tooltip: qsTr("Replace the active document with its latest failed-save recovery revision")
            enabled: !!root.activeSession && !root.activeSession.saving
            onClicked: { root.host.restoreRecovery(root.activeDocumentId, pathField.text); root.statusRequested(root.host.workflowMessage) }
        }
        Tk.TextField {
            id: workspacePathField
            objectName: "workbenchWorkspacePathField"
            placeholderText: qsTr("Absolute workspace path")
            tooltip: qsTr("Workspace state file path")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "workbenchWorkspaceSaveAction"
                text: qsTr("Save layout")
                tooltip: qsTr("Capture and save current documents, views, active document and dock state")
                onClicked: { root.host.saveWorkspace(workspacePathField.text); root.statusRequested(root.host.workflowMessage) }
            }
            Tk.Button {
                objectName: "workbenchWorkspaceRestoreAction"
                text: qsTr("Restore")
                tooltip: qsTr("Restore documents, views, active document and dock state")
                onClicked: { root.host.restoreWorkspace(workspacePathField.text); root.statusRequested(root.host.workflowMessage) }
            }
        }
        Tk.Button {
            objectName: "workbenchAssetTransferAction"
            text: qsTr("Copy asset to other document")
            tooltip: qsTr("Copy selected transferable asset; the destination receives a new stable ID")
            enabled: !!root.activeSession && !!root.inspectedRowId
            onClicked: {
                root.host.copyAsset(root.inspectedRowId)
                const copied = root.host.workflowMessage
                if (root.host.pasteAssetToOtherDocument())
                    root.statusRequested(copied + qsTr(" · ") + root.host.workflowMessage)
                else root.statusRequested(root.host.workflowMessage)
            }
        }
        Tk.Button {
            objectName: "workbenchCloseJobGuardAction"
            text: qsTr("Close during task demo")
            tooltip: qsTr("Close the active sample document while its delayed task is running")
            enabled: !!root.activeSession && !root.activeSession.saving
            onClicked: { root.host.startCloseDuringJob(); root.statusRequested(root.host.workflowMessage) }
        }
        Tk.Button {
            text: qsTr("Close · cancel")
            tooltip: qsTr("Keep a dirty reference document open")
            enabled: !!root.activeSession
            onClicked: root.activeSession && root.host.manager.closeDocument(
                           root.activeDocumentId, Tk.DocumentManager.Cancel)
        }
    }
}
