// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// References are document fields owned by the host. This panel only emits
// stable-ID requests; resolution and formatting stay in the field engine.
Item {
    id: control
    property var references: [] // [{id,label,type,targetId,status,formattedText,error,location}]
    property var targets: [] // [{id,label,type}]
    property var supportedTypes: ["citation", "footnote", "caption", "cross-reference"]
    property string selectedReferenceId: ""
    property string draftLabel: ""
    property string draftType: "citation"
    property string draftTargetId: ""
    property string errorText: ""
    property string tooltip: qsTr("Manage citations, notes, captions and cross-references")
    readonly property var selectedReference: referenceById(selectedReferenceId)
    readonly property bool canAdd: supportedTypes.indexOf(draftType) >= 0 && draftTargetId.length > 0
    signal addReferenceRequested(string type, string targetId)
    signal editReferenceRequested(string referenceId, var patch)
    signal removeReferenceRequested(string referenceId)
    signal navigateRequested(string referenceId, string targetId)

    onSelectedReferenceIdChanged: syncDraftFromSelection()
    onReferencesChanged: syncDraftFromSelection()
    Component.onCompleted: syncDraftFromSelection()

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: content.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function referenceById(id) {
        for (let i = 0; i < references.length; ++i)
            if (references[i].id === id) return references[i]
        return null
    }
    function targetById(id) {
        for (let i = 0; i < targets.length; ++i)
            if (targets[i].id === id) return targets[i]
        return null
    }
    function syncDraftFromSelection() {
        const value = referenceById(selectedReferenceId)
        if (!value) return
        draftLabel = value.label || ""
        draftType = value.type || "citation"
        draftTargetId = value.targetId || ""
    }
    function selectReference(id) {
        if (!enabled || !referenceById(id)) return
        selectedReferenceId = id
        syncDraftFromSelection()
    }
    function addReference() {
        if (enabled && canAdd) addReferenceRequested(draftType, draftTargetId)
    }
    function saveEdit() {
        if (!enabled || !selectedReference) return
        editReferenceRequested(selectedReference.id, {
            label: draftLabel.trim(), type: draftType, targetId: draftTargetId
        })
    }
    function navigate() {
        if (enabled && selectedReference)
            navigateRequested(selectedReference.id, selectedReference.targetId || "")
    }

    Tk.Flex {
        id: content
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("REFERENCES"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { text: qsTr("%1 records").arg(control.references.length) }
        }
        Tk.Caption {
            objectName: "referenceError"
            visible: control.errorText.length > 0
            color: Tk.Theme.color.danger
            text: control.errorText
            wrapMode: Text.WordWrap
        }
        Tk.Scroll {
            objectName: "referenceListScroll"
            implicitHeight: Math.min(listColumn.implicitHeight, Tk.Theme.size.menuItem * 7)
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: listColumn
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.references
                    delegate: Tk.Button {
                        required property var modelData
                        objectName: "reference_" + modelData.id
                        text: (modelData.label || modelData.type) + " · " + (modelData.formattedText || modelData.targetId)
                        variant: modelData.id === control.selectedReferenceId ? "outline" : "ghost"
                        tooltip: qsTr("Select %1 reference").arg(modelData.label || modelData.type)
                        onClicked: control.selectReference(modelData.id)
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "referenceStatus"
            visible: control.selectedReference !== null
            color: control.selectedReference && control.selectedReference.status === "resolved"
                   ? Tk.Theme.color.success : Tk.Theme.color.warning
            text: control.selectedReference
                  ? qsTr("%1 · %2").arg(control.selectedReference.status || qsTr("unresolved"))
                    .arg(control.selectedReference.error || control.selectedReference.formattedText || "") : ""
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.ComboBox {
                id: typeSelect
                objectName: "referenceType"
                model: control.supportedTypes
                currentIndex: Math.max(0, model.indexOf(control.draftType))
                tooltip: qsTr("Reference field type")
                enabled: control.supportedTypes.length > 0
                onActivated: function(index) { control.draftType = model[index] }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.ComboBox {
                objectName: "referenceTarget"
                model: control.targets
                textRole: "label"
                currentIndex: {
                    for (let i = 0; i < model.length; ++i)
                        if (model[i].id === control.draftTargetId) return i
                    return -1
                }
                placeholderText: qsTr("Choose target")
                tooltip: qsTr("Target for this reference")
                onActivated: function(index) { control.draftTargetId = model[index].id }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
        }
        Tk.TextField {
            objectName: "referenceLabel"
            text: control.draftLabel
            placeholderText: qsTr("Reference label")
            tooltip: qsTr("Display label for the reference")
            onTextEdited: control.draftLabel = text
            enabled: control.selectedReference !== null
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "referenceAdd"
                text: qsTr("Add")
                iconName: "plus"
                variant: "accent"
                tooltip: qsTr("Add a reference to the selected target")
                enabled: control.canAdd
                onClicked: control.addReference()
            }
            Tk.Button {
                objectName: "referenceSave"
                text: qsTr("Save edit")
                iconName: "save"
                tooltip: qsTr("Apply label, type and target changes")
                enabled: control.selectedReference !== null
                onClicked: control.saveEdit()
            }
            Tk.Button {
                objectName: "referenceNavigate"
                text: qsTr("Go to target")
                iconName: "navigation"
                tooltip: qsTr("Navigate to this reference target")
                enabled: control.selectedReference !== null && control.selectedReference.status !== "unsupported"
                onClicked: control.navigate()
            }
            Tk.Button {
                objectName: "referenceRemove"
                text: qsTr("Remove")
                iconName: "trash-2"
                variant: "danger"
                tooltip: qsTr("Remove selected reference")
                enabled: control.selectedReference !== null
                onClicked: control.removeReferenceRequested(control.selectedReferenceId)
            }
        }
        Tk.Caption {
            visible: control.supportedTypes.length === 0
            color: Tk.Theme.color.warning
            text: qsTr("Reference editing is unavailable: the host exposes no supported field types.")
            wrapMode: Text.WordWrap
        }
    }
}
