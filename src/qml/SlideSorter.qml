// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Slide identity, ordering, selection and preview resources belong to the host.
Item {
    id: control
    property string documentId: ""
    property var slides: [] // [{id,title,previewUrl,section,hidden,status}]
    property var selectedSlideIds: []
    property string currentSlideId: ""
    property string filterText: ""
    property var capabilities: ({ canReorder: true, canDuplicate: true, canDelete: true, canHide: true })
    property string tooltip: qsTr("Arrange and navigate slides")
    readonly property var filteredSlides: slides.filter(function(slide) {
        const query = filterText.trim().toLocaleLowerCase()
        return query.length === 0 || String(slide.title || "").toLocaleLowerCase().indexOf(query) >= 0
    })

    signal selectionRequested(string slideId, bool additive)
    signal reorderRequested(string slideId, int targetIndex)
    signal duplicateRequested(string slideId)
    signal deleteRequested(string slideId)
    signal hiddenChangeRequested(string slideId, bool hidden)
    signal insertRequested(string afterSlideId, string requestId)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.List
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 slides, %2 selected").arg(slides.length).arg(selectedSlideIds.length)

    function indexOf(id) {
        for (let i = 0; i < slides.length; ++i) if (slides[i].id === id) return i
        return -1
    }
    function move(id, delta) {
        const sourceIndex = indexOf(id)
        const targetIndex = sourceIndex + delta
        if (capabilities.canReorder && sourceIndex >= 0 && targetIndex >= 0 && targetIndex < slides.length)
            reorderRequested(id, targetIndex)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.TextField {
                objectName: "slideSorterFilter"
                placeholderText: qsTr("Filter slides")
                tooltip: qsTr("Filter slides by title")
                text: control.filterText
                clearable: true
                onTextEdited: control.filterText = text
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "slideSorterInsert"
                text: qsTr("Add")
                iconName: "plus"
                tooltip: qsTr("Insert a slide after the current slide")
                enabled: control.slides.length < 10000
                onClicked: control.insertRequested(control.currentSlideId, control.documentId + ":slide:new:" + Date.now())
            }
        }
        Tk.Scroll {
            objectName: "slideSorterList"
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.filteredSlides
                    delegate: Tk.Box {
                        id: row
                        required property var modelData
                        objectName: "slideSorterRow_" + modelData.id
                        color: control.currentSlideId === modelData.id ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
                        borderColor: control.selectedSlideIds.indexOf(modelData.id) >= 0 ? Tk.Theme.color.accent : Tk.Theme.color.border
                        radius: Tk.Theme.radius.sm
                        padding: Tk.Theme.space.xs
                        implicitHeight: Tk.Theme.size.thumbnailHeight + Tk.Theme.size.control + Tk.Theme.space.sm * 2
                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("Slide %1: %2%3").arg(control.indexOf(modelData.id) + 1)
                            .arg(modelData.title || qsTr("Untitled")).arg(modelData.hidden ? qsTr(", hidden") : "")
                        Accessible.selected: control.selectedSlideIds.indexOf(modelData.id) >= 0

                        Tk.Flex {
                            anchors.fill: parent
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.Button {
                                    objectName: "slideSorterSelect_" + modelData.id
                                    text: String(control.indexOf(modelData.id) + 1)
                                    tooltip: qsTr("Select slide %1").arg(modelData.title || modelData.id)
                                    variant: control.currentSlideId === modelData.id ? "accent" : "outline"
                                    onClicked: control.selectionRequested(modelData.id, false)
                                }
                                Tk.CheckBox {
                                    objectName: "slideMultiSelect_" + modelData.id
                                    text: ""
                                    Accessible.name: qsTr("Add slide %1 to multi-selection").arg(modelData.title || modelData.id)
                                    checked: control.selectedSlideIds.indexOf(modelData.id) >= 0
                                    tooltip: qsTr("Toggle slide in multi-selection")
                                    onToggled: control.selectionRequested(modelData.id, true)
                                }
                                Tk.Thumbnail {
                                    objectName: "slidePreview_" + modelData.id
                                    source: modelData.previewUrl || ""
                                    tooltip: qsTr("Preview of %1").arg(modelData.title || qsTr("Untitled slide"))
                                    implicitWidth: Tk.Theme.size.thumbnail
                                    implicitHeight: Tk.Theme.size.thumbnailHeight * 0.72
                                }
                                Tk.Flex {
                                    direction: Tk.Flex.Column
                                    align: Tk.Flex.Stretch
                                    Tk.Label { text: modelData.title || qsTr("Untitled slide"); elide: Text.ElideRight }
                                    Tk.Caption { text: modelData.section || qsTr("Unsectioned"); elide: Text.ElideRight }
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                                Tk.Badge {
                                    visible: !!modelData.hidden
                                    text: qsTr("Hidden")
                                    variant: "warning"
                                    tooltip: qsTr("Hidden from audience presentation")
                                }
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.Caption { text: qsTr("Slide %1").arg(control.indexOf(modelData.id) + 1); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                Tk.IconButton {
                                    objectName: "slideMoveUp_" + modelData.id
                                    iconName: "arrow-up"
                                    tooltip: qsTr("Move slide earlier")
                                    enabled: control.capabilities.canReorder && control.indexOf(modelData.id) > 0
                                    onClicked: control.move(modelData.id, -1)
                                }
                                Tk.IconButton {
                                    objectName: "slideMoveDown_" + modelData.id
                                    iconName: "arrow-down"
                                    tooltip: qsTr("Move slide later")
                                    enabled: control.capabilities.canReorder && control.indexOf(modelData.id) < control.slides.length - 1
                                    onClicked: control.move(modelData.id, 1)
                                }
                                Tk.IconButton {
                                    objectName: "slideDuplicate_" + modelData.id
                                    iconName: "copy"
                                    tooltip: qsTr("Duplicate this slide")
                                    enabled: control.capabilities.canDuplicate
                                    onClicked: control.duplicateRequested(modelData.id)
                                }
                                Tk.IconButton {
                                    objectName: "slideDelete_" + modelData.id
                                    iconName: "trash-2"
                                    tooltip: qsTr("Delete this slide")
                                    enabled: control.capabilities.canDelete && control.slides.length > 1
                                    onClicked: control.deleteRequested(modelData.id)
                                }
                                Tk.IconButton {
                                    objectName: "slideVisibility_" + modelData.id
                                    iconName: modelData.hidden ? "eye" : "eye-off"
                                    tooltip: modelData.hidden ? qsTr("Show slide in presentation") : qsTr("Hide slide from presentation")
                                    enabled: control.capabilities.canHide
                                    onClicked: control.hiddenChangeRequested(modelData.id, !modelData.hidden)
                                }
                            }
                        }
                    }
                }
                Tk.Caption {
                    visible: control.filteredSlides.length === 0
                    text: control.slides.length === 0 ? qsTr("No slides") : qsTr("No slides match the filter")
                    color: Tk.Theme.color.textMuted
                }
            }
        }
    }
}
