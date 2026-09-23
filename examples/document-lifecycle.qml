// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Reference document lifecycle: create, switch, duplicate a view, save, open,
// inspect Save All results, and exercise cancel/discard close decisions.
// Rich domain edits are supplied by an application's C++ model adapter.
Rectangle {
    id: root
    width: 1120
    height: 720
    color: Tk.Theme.color.bg
    Accessible.role: Accessible.Window
    Accessible.name: qsTr("Document lifecycle example")

    property string statusText: qsTr("Create a document to begin.")
    property string saveAllText: qsTr("Save All has not run.")

    function activeId() {
        return manager.activeSession ? manager.activeSession.documentId : ""
    }

    function saveActive() {
        const session = manager.activeSession
        if (!session) {
            statusText = qsTr("Select a document first.")
            return
        }
        const started = session.uri.length > 0
                     ? session.requestSave()
                     : session.requestSaveAs(pathField.text.trim(), false)
        statusText = started ? qsTr("Save started for %1.").arg(session.title)
                             : session.lastError
    }

    function saveAllSummary(results) {
        let saved = 0
        let failed = 0
        for (let i = 0; i < results.length; ++i) {
            if (results[i].success) ++saved
            else ++failed
        }
        saveAllText = qsTr("Save All: %1 saved, %2 failed.").arg(saved).arg(failed)
        if (failed > 0) {
            const failures = []
            for (let j = 0; j < results.length; ++j) {
                if (!results[j].success)
                    failures.push(results[j].documentId + ": " + results[j].message)
            }
            saveAllText += " " + failures.join("; ")
        }
    }

    Tk.DocumentManager {
        id: manager
        onSaveAllFinished: function(results) { root.saveAllSummary(results) }
        onCloseBlocked: function(documentId, reason) {
            root.statusText = qsTr("Close kept %1 open: %2").arg(documentId).arg(reason)
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { title: qsTr("DOCUMENT LIFECYCLE"); wide: true; color: Tk.Theme.color.accent }
                Tk.Caption { text: qsTr("Reference persistence and session state") }
            }
            Tk.Spacer {}
            Tk.Button {
                text: qsTr("New document")
                iconName: "file-plus"
                variant: "accent"
                tooltip: qsTr("Create an unsaved reference document")
                onClicked: {
                    const session = manager.createReferenceDocument()
                    root.statusText = session ? qsTr("Created dirty document %1.").arg(session.documentId)
                                              : manager.lastError
                }
            }
            Tk.Button {
                text: qsTr("Save All")
                iconName: "save"
                tooltip: qsTr("Save every dirty document and report each result")
                onClicked: {
                    if (!manager.requestSaveAll())
                        root.saveAllText = qsTr("Save All is already running.")
                }
            }
        }

        Tk.Box {
            color: Tk.Theme.color.surface
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            implicitHeight: Tk.Theme.size.control + Tk.Theme.space.md * 2

            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                paddingLeft: Tk.Theme.space.sm
                paddingRight: Tk.Theme.space.sm

                Tk.Caption { text: qsTr("Reference file path") }
                Tk.TextField {
                    id: pathField
                    objectName: "referencePath"
                    placeholderText: qsTr("Absolute path for Save As or Open")
                    tooltip: qsTr("Enter the local reference document path")
                    implicitWidth: Tk.Theme.size.fieldWidth * 2
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Button {
                    text: qsTr("Save / Save As")
                    iconName: "save"
                    tooltip: qsTr("Save active document, or Save As when it is untitled")
                    enabled: !!manager.activeSession && !manager.activeSession.saving
                    onClicked: root.saveActive()
                }
                Tk.Button {
                    text: qsTr("Open")
                    iconName: "folder-open"
                    tooltip: qsTr("Open a reference document from this path")
                    onClicked: {
                        const session = manager.openReferenceDocument(pathField.text.trim())
                        root.statusText = session ? qsTr("Opened %1.").arg(session.title)
                                                  : manager.lastError
                    }
                }
            }
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0

            Tk.Box {
                objectName: "sessionListPanel"
                color: Tk.Theme.color.panel
                borderRight: Tk.Theme.size.border
                borderColor: Tk.Theme.color.divider
                implicitWidth: Tk.Theme.size.fieldWidth * 1.6
                Tk.Flex.shrink: 0

                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    padding: Tk.Theme.space.sm

                    Tk.Overline { title: qsTr("OPEN SESSIONS"); wide: true }
                    Tk.Scroll {
                        accessibleName: qsTr("Open document sessions")
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0

                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Repeater {
                                model: manager.sessions
                                delegate: Tk.Button {
                                    required property var modelData
                                    text: (modelData.dirty ? "● " : "○ ") + modelData.title
                                    variant: manager.activeSession === modelData ? "outline" : "ghost"
                                    tooltip: qsTr("Activate %1").arg(modelData.title)
                                    onClicked: manager.activate(modelData.documentId)
                                }
                            }
                            Tk.Caption {
                                visible: manager.sessions.length === 0
                                text: qsTr("No open documents")
                            }
                        }
                    }
                    Tk.Caption {
                        text: qsTr("Views: %1").arg(manager.activeSession
                            ? manager.viewsFor(manager.activeSession.documentId).length : 0)
                    }
                }
            }

            Tk.Box {
                color: Tk.Theme.color.panel
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0

                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    padding: Tk.Theme.space.md

                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.Overline {
                                title: manager.activeSession ? manager.activeSession.title : qsTr("NO DOCUMENT")
                                wide: true
                            }
                            Tk.Caption {
                                text: manager.activeSession
                                      ? qsTr("Document ID %1 · revision %2").arg(manager.activeSession.documentId)
                                                                                 .arg(manager.activeSession.revision)
                                      : qsTr("Create or open a reference document")
                            }
                        }
                        Tk.Spacer {}
                        Tk.Badge {
                            text: manager.activeSession
                                  ? manager.activeSession.dirty ? qsTr("Dirty") : qsTr("Clean")
                                  : qsTr("Idle")
                            variant: manager.activeSession && manager.activeSession.dirty ? "warning" : "default"
                        }
                    }

                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        Tk.Badge { text: manager.activeSession && manager.activeSession.saving ? qsTr("Saving") : qsTr("Not saving"); variant: manager.activeSession && manager.activeSession.saving ? "info" : "default" }
                        Tk.Badge { text: manager.activeSession && manager.activeSession.externalChanged ? qsTr("External change") : qsTr("No external change"); variant: manager.activeSession && manager.activeSession.externalChanged ? "danger" : "default" }
                        Tk.Badge { text: manager.activeSession && manager.activeSession.recovered ? qsTr("Recovered") : qsTr("Not recovered"); variant: manager.activeSession && manager.activeSession.recovered ? "warning" : "default" }
                        Tk.Badge { text: manager.activeSession && manager.activeSession.readOnly ? qsTr("Read only") : qsTr("Writable"); variant: "default" }
                    }

                    Tk.Caption {
                        text: manager.activeSession
                              ? qsTr("Saved revision %1 · %2").arg(manager.activeSession.savedRevision)
                                                               .arg(manager.activeSession.uri.length > 0 ? manager.activeSession.uri : qsTr("no file yet"))
                              : ""
                        wrapMode: Text.Wrap
                    }

                    Tk.Divider {}
                    Tk.Overline { title: qsTr("SESSION ACTIONS"); wide: true }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        wrap: Tk.Flex.Wrap
                        gap: Tk.Theme.space.xs
                        Tk.Button {
                            text: qsTr("Duplicate view")
                            iconName: "copy"
                            tooltip: qsTr("Add another view identity for this document")
                            enabled: !!manager.activeSession
                            onClicked: {
                                const viewId = manager.duplicateView(root.activeId())
                                root.statusText = viewId.length > 0 ? qsTr("Added view %1.").arg(viewId)
                                                                   : manager.lastError
                            }
                        }
                        Tk.Button {
                            text: qsTr("Check external change")
                            iconName: "refresh-cw"
                            tooltip: qsTr("Recheck the active file against its saved baseline")
                            enabled: !!manager.activeSession && manager.activeSession.uri.length > 0
                            onClicked: root.statusText = manager.activeSession.checkExternalChange()
                                      ? qsTr("The file changed outside this session.")
                                      : qsTr("No external change was found.")
                        }
                        Tk.Button {
                            text: qsTr("Close · Cancel")
                            iconName: "x"
                            tooltip: qsTr("Attempt close with Cancel policy; dirty sessions remain open")
                            enabled: !!manager.activeSession
                            onClicked: manager.closeDocument(root.activeId(), Tk.DocumentManager.Cancel)
                        }
                        Tk.Button {
                            text: qsTr("Close · Discard")
                            variant: "danger"
                            iconName: "trash-2"
                            tooltip: qsTr("Discard active session and close it")
                            enabled: !!manager.activeSession
                            onClicked: manager.closeDocument(root.activeId(), Tk.DocumentManager.Discard)
                        }
                    }

                    Tk.Box {
                        color: Tk.Theme.color.surface
                        borderWidth: Tk.Theme.size.border
                        borderColor: Tk.Theme.color.divider
                        radius: Tk.Theme.radius.sm
                        implicitHeight: Tk.Theme.size.row * 4
                        Tk.Flex {
                            anchors.fill: parent
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            padding: Tk.Theme.space.sm
                            Tk.Overline { title: qsTr("ACTIVITY"); wide: true }
                            Tk.Caption { text: root.statusText; wrapMode: Text.Wrap }
                            Tk.Caption { text: root.saveAllText; wrapMode: Text.Wrap }
                        }
                    }

                    Tk.Spacer {}
                    Tk.Caption {
                        text: qsTr("A new reference session starts dirty until its first save. Domain property editing is supplied by an application's C++ adapter.")
                        wrapMode: Text.Wrap
                        color: Tk.Theme.color.textMuted
                    }
                }
            }
        }
    }
}
