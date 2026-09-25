// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Immutable checkpoints, local to this replica or shared with every replica
// (PropertyVersionHistory.shared). Restore saves a safety checkpoint and
// submits a new collaboration operation through PropertyVersionHistory.
Item {
    id: control
    objectName: "versionBrowser"
    property var history: null
    property string selectedId: ""
    property string draftName: ""
    property string tooltip: qsTr("Inspect and restore document versions")
    readonly property var selectedInfo: history && selectedId ? history.inspect(selectedId) : ({})
    readonly property var currentDiff: {
        const revision = history && history.client && history.client.document
                         ? history.client.document.revision : undefined
        return revision !== undefined && selectedId ? history.diffCurrent(selectedId) : []
    }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: body.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function createCheckpoint() {
        if (!history || !draftName.trim()) return
        const id = history.checkpoint(draftName.trim())
        if (id) { selectedId = id; draftName = "" }
    }

    Tk.Flex {
        id: body
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("VERSIONS"); Tk.Flex.grow: 1 }
            Tk.Caption { text: control.history ? String(control.history.count) : "0" }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.TextField {
                objectName: "versionName"
                text: control.draftName
                placeholderText: qsTr("Checkpoint name")
                tooltip: qsTr("Name the current document state")
                Tk.Flex.grow: 1
                onTextChanged: control.draftName = text
                onAccepted: control.createCheckpoint()
            }
            Tk.Button {
                objectName: "versionCreate"
                text: qsTr("Save")
                tooltip: qsTr("Save an immutable checkpoint")
                available: !!control.history && control.draftName.trim().length > 0
                onClicked: control.createCheckpoint()
            }
        }
        Tk.Scroll {
            objectName: "versionListScroll"
            implicitHeight: Math.min(versionList.implicitHeight, Tk.Theme.size.panelMinWidth)
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: versionList
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.history ? control.history.versions : []
                    delegate: Tk.Button {
                        required property var modelData
                        objectName: "version_" + modelData.id
                        text: modelData.shared
                              ? qsTr("%1 · shared").arg(modelData.name)
                              : modelData.name + " · " + modelData.revision
                        tooltip: qsTr("Inspect %1 from %2").arg(modelData.name).arg(modelData.createdAt)
                        variant: modelData.id === control.selectedId ? "outline" : "ghost"
                        onClicked: control.selectedId = modelData.id
                    }
                }
            }
        }
        Tk.EmptyState {
            objectName: "versionEmpty"
            visible: !control.history || control.history.count === 0
            title: qsTr("No checkpoints")
            text: qsTr("Save a named version to compare or restore later.")
        }
        Tk.Caption {
            objectName: "versionProvenance"
            visible: !!control.selectedId && !!control.selectedInfo.id
            text: control.selectedInfo.id
                  ? qsTr("Revision %1 · %2 · %3")
                    .arg(control.selectedInfo.revision)
                    .arg(control.selectedInfo.actor)
                    .arg(control.selectedInfo.createdAt) : ""
            wrapMode: Text.Wrap
        }
        Tk.Caption {
            objectName: "versionDiffSummary"
            visible: !!control.selectedId
            text: qsTr("%1 properties differ from current")
                  .arg(control.currentDiff.length)
        }
        Repeater {
            model: control.currentDiff
            delegate: Tk.Caption {
                required property var modelData
                objectName: "versionDifference_" + modelData.entity + "_" + modelData.property
                text: modelData.change === "added"
                      ? qsTr("%1: added in current (%2)").arg(modelData.property).arg(String(modelData.to))
                      : modelData.change === "removed"
                        ? qsTr("%1: removed from current (was %2)").arg(modelData.property)
                                                                    .arg(String(modelData.from))
                        : modelData.property + ": " + String(modelData.from) + " → "
                          + String(modelData.to)
                wrapMode: Text.Wrap
            }
        }
        Tk.Button {
            objectName: "versionRestore"
            text: qsTr("Restore selected")
            tooltip: qsTr("Save the current state, then restore the selected version as a new collaborative edit")
            available: !!control.history && !!control.selectedId
            onClicked: control.history.restore(control.selectedId)
        }
        Tk.Caption {
            objectName: "versionError"
            visible: !!control.history && control.history.lastError.length > 0
            text: control.history ? control.history.lastError : ""
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
        }
    }
}
