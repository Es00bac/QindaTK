// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Master and per-slide override snapshots stay owned by the slide model.
Item {
    id: control
    property string documentId: ""
    property string masterId: ""
    property string layoutId: ""
    property string selectedSlideId: ""
    property var masters: [] // [{id,title,layouts:[{id,title}],themeId,placeholders:[{role,label,value,overrideCount}]}]
    property var placeholderOverrides: [] // [{slideId,role,value}]
    property var selectedSlideIds: []
    property var capabilities: ({ canEdit: true, canApply: true, canResetOverrides: true })
    property string tooltip: qsTr("Edit slide masters and placeholder defaults")
    readonly property var currentMaster: findMaster(masterId)
    readonly property var currentLayout: findLayout(layoutId)
    readonly property var placeholders: currentMaster ? currentMaster.placeholders || [] : []

    signal masterSelectionRequested(string masterId)
    signal layoutSelectionRequested(string layoutId)
    signal placeholderEditRequested(string masterId, string layoutId, string role, string value, string requestId)
    signal applyMasterRequested(string masterId, var slideIds, string requestId)
    signal overrideResetRequested(string slideId, string role)

    implicitWidth: Tk.Theme.size.panelMinWidth * 1.5
    implicitHeight: masterContent.implicitHeight
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: currentMaster ? qsTr("Editing master %1, layout %2").arg(currentMaster.title).arg(currentLayout ? currentLayout.title : qsTr("none"))
                                           : qsTr("No slide master selected")

    function findMaster(id) {
        for (let i = 0; i < masters.length; ++i) if (masters[i].id === id) return masters[i]
        return null
    }
    function findLayout(id) {
        if (!currentMaster) return null
        const layouts = currentMaster.layouts || []
        for (let i = 0; i < layouts.length; ++i) if (layouts[i].id === id) return layouts[i]
        return null
    }
    function overrideFor(role) {
        for (let i = 0; i < placeholderOverrides.length; ++i)
            if (placeholderOverrides[i].slideId === selectedSlideId && placeholderOverrides[i].role === role)
                return placeholderOverrides[i]
        return null
    }

    Tk.Flex {
        id: masterContent
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.ComboBox {
                objectName: "slideMasterChoice"
                model: control.masters
                textRole: "title"
                currentIndex: {
                    for (let i = 0; i < control.masters.length; ++i)
                        if (control.masters[i].id === control.masterId) return i
                    return -1
                }
                tooltip: qsTr("Choose slide master")
                enabled: control.masters.length > 0
                onActivated: function(index) { control.masterSelectionRequested(control.masters[index].id) }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.ComboBox {
                objectName: "slideLayoutChoice"
                model: control.currentMaster ? control.currentMaster.layouts || [] : []
                textRole: "title"
                currentIndex: {
                    const layouts = control.currentMaster ? control.currentMaster.layouts || [] : []
                    for (let i = 0; i < layouts.length; ++i) if (layouts[i].id === control.layoutId) return i
                    return -1
                }
                tooltip: qsTr("Choose layout within this master")
                enabled: !!control.currentMaster
                onActivated: function(index) { control.layoutSelectionRequested(control.currentMaster.layouts[index].id) }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
        }
        Tk.Flex {
            objectName: "slideMasterSummary"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Caption { text: control.currentMaster ? qsTr("Theme · %1").arg(control.currentMaster.themeId || qsTr("Inherited")) : qsTr("No master"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Badge { visible: control.selectedSlideIds.length > 1; text: qsTr("%1 slides").arg(control.selectedSlideIds.length); variant: "accent"; tooltip: qsTr("Slides selected for master application") }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "slideApplyMasterSelection"
                text: qsTr("Apply to selected slides")
                iconName: "layers"
                tooltip: qsTr("Request this master and layout for the selected slides")
                enabled: control.capabilities.canApply && !!control.currentMaster && control.selectedSlideIds.length > 0
                onClicked: control.applyMasterRequested(control.masterId, control.selectedSlideIds.slice(), control.documentId + ":master:" + Date.now())
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
        }
        Tk.Scroll {
            objectName: "slidePlaceholderList"
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.placeholders
                    delegate: Tk.Box {
                        id: placeholderRow
                        required property var modelData
                        objectName: "slidePlaceholder_" + modelData.role
                        color: Tk.Theme.color.panelAlt
                        radius: Tk.Theme.radius.sm
                        padding: Tk.Theme.space.xs
                        implicitHeight: Tk.Theme.size.control * 2 + Tk.Theme.space.xs
                        property var override: control.overrideFor(modelData.role)
                        property string editValue: override ? String(override.value) : String(modelData.value || "")
                        Tk.Flex {
                            anchors.fill: parent
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                Tk.Label { text: modelData.label || modelData.role; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                Tk.Badge {
                                    visible: placeholderRow.override !== null
                                    text: qsTr("Override")
                                    variant: "warning"
                                    tooltip: qsTr("The selected slide overrides this master placeholder")
                                }
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                gap: Tk.Theme.space.xs
                                Tk.TextField {
                                    objectName: "slidePlaceholderValue_" + modelData.role
                                    text: placeholderRow.editValue
                                    tooltip: qsTr("Default value for %1 placeholder").arg(modelData.label || modelData.role)
                                    enabled: control.capabilities.canEdit
                                    onTextEdited: placeholderRow.editValue = text
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                                Tk.Button {
                                    objectName: "slidePlaceholderApply_" + modelData.role
                                    text: qsTr("Set default")
                                    tooltip: qsTr("Edit this placeholder on the master")
                                    enabled: control.capabilities.canEdit && !!control.currentMaster
                                    onClicked: control.placeholderEditRequested(control.masterId, control.layoutId,
                                        modelData.role, placeholderRow.editValue, control.documentId + ":placeholder:" + Date.now())
                                }
                                Tk.IconButton {
                                    objectName: "slidePlaceholderReset_" + modelData.role
                                    iconName: "rotate-ccw"
                                    tooltip: qsTr("Remove selected slide override")
                                    enabled: control.capabilities.canResetOverrides && placeholderRow.override !== null
                                    onClicked: control.overrideResetRequested(control.selectedSlideId, modelData.role)
                                }
                            }
                        }
                    }
                }
                Tk.Caption { visible: control.placeholders.length === 0; text: qsTr("This layout has no placeholders.") }
            }
        }
    }
}
