// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Navigates the authoritative per-document undo history. The service owns
// grouping, clean markers, branch invalidation and history retention.
Tk.Box {
    id: control
    objectName: "historyView"
    property var undoService: null
    property real listHeight: Tk.Theme.size.fieldWidth * 3
    readonly property var historyRows: {
        if (!undoService || undoService.historyEntries.length === 0) return []
        const entries = [{entryId: "history.initial", label: qsTr("Initial state"), index: 0,
                          applied: undoService.historyIndex === 0, clean: undoService.cleanIndex === 0,
                          initial: true}]
        for (let i = 0; i < undoService.historyEntries.length; ++i) {
            const item = undoService.historyEntries[i]
            entries.push({entryId: item.entryId, label: item.label, index: item.index,
                          applied: item.applied, clean: item.clean, initial: false})
        }
        return entries
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Document history")

    function navigate(entryId) {
        return undoService && entryId ? undoService.navigateToHistoryEntry(entryId) : false
    }
    function goToHistoryIndex(index) {
        return undoService ? undoService.navigateToHistoryIndex(index) : false
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.fieldWidth
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("History"); wide: true; Tk.Flex.grow: 1 }
            Tk.Button {
                objectName: "historyUndo"
                iconName: "undo-2"
                text: qsTr("Undo")
                tooltip: control.undoService ? control.undoService.undoText || qsTr("Undo") : qsTr("Undo")
                available: !!control.undoService && control.undoService.canUndo
                onClicked: control.undoService.undo()
            }
            Tk.Button {
                objectName: "historyRedo"
                iconName: "redo-2"
                text: qsTr("Redo")
                tooltip: control.undoService ? control.undoService.redoText || qsTr("Redo") : qsTr("Redo")
                available: !!control.undoService && control.undoService.canRedo
                onClicked: control.undoService.redo()
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label {
                objectName: "historyPosition"
                text: control.undoService
                    ? qsTr("Position %1 · Saved at %2").arg(control.undoService.historyIndex).arg(control.undoService.cleanIndex)
                    : qsTr("No history service")
                color: Tk.Theme.color.textMuted
                Tk.Flex.grow: 1
            }
            Tk.Badge {
                objectName: "historyCleanState"
                text: !control.undoService ? qsTr("Unavailable")
                    : control.undoService.clean ? qsTr("Saved") : qsTr("Modified")
                variant: !control.undoService ? "default" : control.undoService.clean ? "success" : "warning"
                tooltip: qsTr("Whether the current state matches the saved history position")
            }
        }
        ListView {
            id: historyList
            objectName: "historyEntries"
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Undo history entries")
            model: control.historyRows
            clip: true
            spacing: Tk.Theme.space.xs
            implicitHeight: Math.min(contentHeight, control.listHeight)
            height: implicitHeight
            Tk.Flex.shrink: 0
            boundsBehavior: Flickable.StopAtBounds
            delegate: Tk.Flex {
                required property string entryId
                required property string label
                required property int index
                required property bool applied
                required property bool clean
                required property bool initial
                objectName: "historyRow_" + entryId
                width: historyList.width
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Tk.Button {
                    objectName: "historyEntry_" + parent.entryId
                    text: parent.label
                    tooltip: parent.initial ? qsTr("Navigate to the document's initial state")
                        : parent.applied ? qsTr("Applied history step") : qsTr("Redoable history step")
                    variant: parent.applied ? "outline" : "ghost"
                    Tk.Flex.grow: 1
                    onClicked: parent.initial ? control.goToHistoryIndex(0) : control.navigate(parent.entryId)
                }
                Tk.Badge {
                    visible: parent.clean
                    text: qsTr("Saved")
                    variant: "success"
                    tooltip: qsTr("This history entry is the clean saved position")
                }
                Tk.Badge {
                    visible: parent.applied
                    text: qsTr("Applied")
                    variant: "accent"
                    tooltip: qsTr("This change is included in the current state")
                }
            }
        }
        Tk.EmptyState {
            objectName: "historyEmpty"
            visible: !control.undoService || control.undoService.historyEntries.length === 0
            title: qsTr("No changes yet")
            text: qsTr("Document edits will appear in history.")
        }
        Tk.Label {
            objectName: "historyLastError"
            visible: !!control.undoService && control.undoService.lastError.length > 0
            text: control.undoService ? control.undoService.lastError : ""
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
        }
    }
}
