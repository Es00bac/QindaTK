// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: root
    objectName: "virtualList"
    property var model: null
    property var identityModel: null
    property Tk.SelectionModel selectionModel: null
    property string idRole: "id"
    property string labelRole: "label"
    property string secondaryRole: "secondary"
    property string iconRole: "icon"
    property string tooltip: ""
    property string emptyText: qsTr("No items")
    property alias listView: rows
    readonly property alias collectionAdapter: adapter
    signal activated(string id)
    signal contextRequested(string id, point position)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: Tk.Theme.space.xs
    activeFocusOnTab: enabled
    Accessible.role: Accessible.List
    Accessible.name: tooltip.length > 0 ? tooltip : qsTr("List")
    Accessible.focusable: enabled

    Tk.VirtualCollectionAdapter {
        id: adapter
        model: root.model
        identityModel: root.identityModel ? root.identityModel : root.model
        selectionModel: root.selectionModel
        idRole: root.idRole
    }
    background: Rectangle { color: Tk.Theme.color.surface; border.color: Tk.Theme.color.border; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.sm }
    contentItem: Item {
        ListView {
            id: rows
            objectName: "virtualListView"
            anchors.fill: parent
            clip: true
            model: root.model
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            Accessible.role: Accessible.List
            Accessible.name: root.Accessible.name
            delegate: Item {
                id: rowHost
                required property int index
                objectName: "virtualListDelegate"
                width: rows.width
                height: Tk.Theme.size.row
                property string stableId: ""
                property string labelText: ""
                property string secondaryText: ""
                property string iconName: ""
                function refreshRoles() {
                    stableId = adapter.idAt(index)
                    labelText = String(adapter.dataAt(index, root.labelRole) || "")
                    secondaryText = String(adapter.dataAt(index, root.secondaryRole) || "")
                    iconName = String(adapter.dataAt(index, root.iconRole) || "")
                }
                Component.onCompleted: refreshRoles()
                onIndexChanged: refreshRoles()
                Connections { target: adapter; function onChanged() { rowHost.refreshRoles() } }
                Tk.ListRow {
                    anchors.fill: parent
                    text: rowHost.labelText
                    secondaryText: rowHost.secondaryText
                    iconName: rowHost.iconName
                    selected: root.selectionModel && root.selectionModel.selectedIds.indexOf(rowHost.stableId) >= 0
                    active: root.selectionModel && root.selectionModel.currentId === rowHost.stableId
                    tooltip: rowHost.labelText
                    onClicked: root.selectRow(rowHost.index, Qt.NoModifier)
                    onRightClicked: function(position) { root.contextRequested(rowHost.stableId, position) }
                    onDoubleClicked: root.activated(rowHost.stableId)
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: function(mouse) {
                        root.selectRow(rowHost.index, mouse.modifiers)
                        root.forceActiveFocus()
                    }
                    onDoubleClicked: root.activated(rowHost.stableId)
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: rowHost.labelText
                Accessible.selected: root.selectionModel && root.selectionModel.selectedIds.indexOf(rowHost.stableId) >= 0
                Accessible.focusable: root.enabled
                Accessible.focused: root.activeFocus && root.selectionModel
                                    && root.selectionModel.currentId === rowHost.stableId
                Accessible.onPressAction: {
                    root.selectRow(rowHost.index, Qt.NoModifier)
                    root.forceActiveFocus()
                    root.activated(rowHost.stableId)
                }
            }
        }
        Tk.EmptyState {
            objectName: "virtualListEmpty"
            anchors.centerIn: parent
            visible: adapter.count === 0
            title: root.emptyText
        }
    }
    Keys.onPressed: function(event) {
        if (!root.enabled || !root.selectionModel || adapter.count === 0) return
        let index = adapter.indexOfId(root.selectionModel.currentId)
        if (index < 0) index = 0
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            index = Math.max(0, Math.min(adapter.count - 1, index + (event.key === Qt.Key_Down ? 1 : -1)))
            root.selectionModel.select(adapter.idAt(index))
            rows.positionViewAtIndex(index, ListView.Contain)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.activated(adapter.idAt(index)); event.accepted = true
        }
    }
    function selectRow(index, modifiers) {
        if (!selectionModel || index < 0 || index >= adapter.count) return false
        const id = adapter.idAt(index)
        if (modifiers & Qt.ShiftModifier) {
            const anchor = adapter.indexOfId(selectionModel.anchorId)
            if (anchor >= 0) return selectionModel.extendTo(id, adapter.rangeIds(anchor, index))
        }
        if (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) return selectionModel.toggle(id)
        return selectionModel.select(id)
    }
}
