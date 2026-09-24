// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Live DocumentManager scene: edits use the shared history, Save As writes a
// session-header scene file, and Reopen creates a fresh model from that file.
Rectangle {
    id: root
    objectName: "sceneSessionExample"
    width: 1120
    height: 720
    color: Tk.Theme.color.bg
    Accessible.role: Accessible.Window
    Accessible.name: qsTr("3D scene document session example")
    property string statusText: qsTr("Creating scene session")
    readonly property var session: manager.activeSession
    readonly property var scene: session ? session.model : null

    Tk.DocumentManager { id: manager }
    Component.onCompleted: {
        const created = manager.createDocument("qindatk.scene3d.v1")
        if (!created) { statusText = manager.lastError; return }
        const cube = created.model.addEntity("Editable cube", "cube", Qt.vector3d(0, 0, 0), "#35bbc4")
        created.model.setMaterial(cube, "#35bbc4", 0.22, 0.75, "#080d10")
        created.model.select(cube)
        statusText = qsTr("Scene created. Edit, save, then reopen it.")
    }
    Connections {
        target: root.session
        function onSaveFinished(success, message, savedRevision) {
            root.statusText = success ? qsTr("Saved scene revision %1").arg(savedRevision) : message
        }
    }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.sm
        Tk.Heading { text: qsTr("3D scene document session"); level: 1 }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.TextField {
                id: pathField
                objectName: "sceneSessionPath"
                Tk.Flex.grow: 1
                text: "/tmp/qindatk-scene-session-demo.qtkscene"
                tooltip: qsTr("Native scene document path")
            }
            Tk.Button {
                text: qsTr("Save As")
                enabled: !!root.session && !root.session.saving
                onClicked: {
                    const started = root.session.requestSaveAs(pathField.text.trim(), true)
                    if (!started) root.statusText = root.session.lastError
                }
            }
            Tk.Button {
                text: qsTr("Reopen")
                tooltip: qsTr("Discard current edits and reopen the saved scene")
                onClicked: {
                    if (root.session && !manager.closeDocument(root.session.documentId, Tk.DocumentManager.Discard)) {
                        root.statusText = manager.lastError; return
                    }
                    const opened = manager.openDocument(pathField.text.trim(), "qindatk.scene3d.v1")
                    root.statusText = opened ? qsTr("Reopened scene revision %1").arg(opened.revision)
                                             : manager.lastError
                }
            }
            Tk.Button {
                text: qsTr("Undo")
                enabled: !!root.scene && root.scene.canUndo
                onClicked: manager.undoService.undo()
            }
            Tk.Button {
                text: qsTr("Redo")
                enabled: !!root.scene && root.scene.canRedo
                onClicked: manager.undoService.redo()
            }
        }
        Tk.Caption {
            text: root.session ? (root.session.dirty ? qsTr("Unsaved") : qsTr("Saved"))
                                 + " · " + root.statusText : root.statusText
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; Tk.Flex.grow: 1
            Tk.SceneOutliner {
                Tk.Flex.basis: 260; Tk.Flex.shrink: 0
                listHeight: 570
                scene: root.scene
                onReparentRequested: function(entityId, parentId, keepWorld) {
                    if (root.scene) root.scene.reparent(entityId, parentId, keepWorld)
                }
            }
            Tk.SceneView3D { scene: root.scene; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.MaterialPreview {
                scene: root.scene
                Tk.Flex.basis: Tk.Theme.size.panelMinWidth * 1.4
                Tk.Flex.shrink: 0
            }
        }
    }
}
