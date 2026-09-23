// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// A flat layer list bound to CanvasDocument's current layer facade.
// Hierarchy, artboards and reordering require model support not present yet.
Tk.Box {
    id: control
    objectName: "layerTree"

    property var document: null
    property real listHeight: Tk.Theme.size.fieldWidth * 3

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Layers")

    function activateLayer(id) {
        if (control.document) control.document.activeLayer = id
    }
    function setVisibility(id, visible) {
        if (control.document) control.document.setLayerVisibleById(id, visible)
    }
    function setLock(id, locked) {
        if (control.document) control.document.setLayerLockedById(id, locked)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        width: parent ? parent.width : Tk.Theme.size.fieldWidth

        Tk.Overline { objectName: "layerTreeTitle"; title: qsTr("Layers"); wide: true }

        Item {
            id: listFrame
            objectName: "layerTreeListFrame"
            width: parent.width
            implicitHeight: Math.min(Math.max(layerList.contentHeight, Tk.Theme.size.row), control.listHeight)
            height: implicitHeight
            visible: layerList.count > 0
            Tk.Flex.shrink: 0

            ListView {
                id: layerList
                objectName: "layerTreeList"
                anchors.fill: parent
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Document layers")
                clip: true
                spacing: Tk.Theme.space.xs
                boundsBehavior: Flickable.StopAtBounds
                model: control.document ? control.document.layers : []

                delegate: T.AbstractButton {
                id: layerRow
                required property var modelData
                readonly property string layerId: String(modelData.id)
                readonly property bool isActive: control.document
                                                  && String(control.document.activeLayer) === layerId
                objectName: "layerRow_" + layerId
                width: layerList.width
                height: Tk.Theme.size.row
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name
                Accessible.description: (modelData.visible ? qsTr("Visible") : qsTr("Hidden"))
                                        + ", " + (modelData.locked ? qsTr("Locked") : qsTr("Unlocked"))
                Accessible.selected: isActive
                Accessible.selectable: true
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                Accessible.onPressAction: if (enabled) click()
                onClicked: control.activateLayer(modelData.id)

                background: Rectangle {
                    color: layerRow.down ? Tk.Theme.color.pressed
                         : layerRow.isActive ? Tk.Theme.color.selection
                         : layerRow.hovered ? Tk.Theme.color.hover
                         : Tk.Theme.alpha(Tk.Theme.color.panel, Tk.Theme.opacity.hidden)
                    radius: Tk.Theme.radius.sm
                }

                contentItem: Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    paddingLeft: Tk.Theme.space.xs
                    paddingRight: Tk.Theme.space.xs

                    Tk.Icon { name: "layers"; size: Tk.Theme.size.iconSm
                        color: layerRow.isActive ? Tk.Theme.color.accent : Tk.Theme.color.textMuted }
                    Tk.Label {
                        objectName: "layerName_" + layerRow.layerId
                        text: layerRow.modelData.name
                        elide: Text.ElideRight
                        Tk.Flex.grow: 1
                    }
                    Tk.IconButton {
                        small: true
                        objectName: "layerVisibility_" + layerRow.layerId
                        iconName: layerRow.modelData.visible ? "eye" : "eye-off"
                        checkable: true
                        checked: layerRow.modelData.visible
                        tooltip: layerRow.modelData.visible ? qsTr("Hide %1").arg(layerRow.modelData.name)
                                                          : qsTr("Show %1").arg(layerRow.modelData.name)
                        Accessible.role: Accessible.CheckBox
                        onClicked: control.setVisibility(layerRow.modelData.id, !layerRow.modelData.visible)
                    }
                    Tk.IconButton {
                        small: true
                        objectName: "layerLock_" + layerRow.layerId
                        iconName: layerRow.modelData.locked ? "lock" : "unlock"
                        checkable: true
                        checked: layerRow.modelData.locked
                        tooltip: layerRow.modelData.locked ? qsTr("Unlock %1").arg(layerRow.modelData.name)
                                                          : qsTr("Lock %1").arg(layerRow.modelData.name)
                        Accessible.role: Accessible.CheckBox
                        onClicked: control.setLock(layerRow.modelData.id, !layerRow.modelData.locked)
                    }
                }
                }
            }
        }

        Tk.Label {
            objectName: "layerTreeEmpty"
            visible: !control.document || control.document.layers.length === 0
            text: control.document ? qsTr("No layers") : qsTr("No document")
            muted: true
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }
    }
}
