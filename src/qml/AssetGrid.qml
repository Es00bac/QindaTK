// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: root
    objectName: "assetGrid"
    property var model: null
    property var identityModel: null
    property Tk.SelectionModel selectionModel: null
    property var provider: null
    property var document: null
    property string viewId: ""
    property string idRole: "id"
    property string resourceRole: "resourceId"
    property string labelRole: "label"
    property string captionRole: "caption"
    property string tooltip: ""
    property real tileWidth: Tk.Theme.size.thumbnail * 2
    property real tileHeight: Tk.Theme.size.thumbnailHeight + Tk.Theme.size.row
    property real tileSpacing: Tk.Theme.space.sm
    property alias gridView: tiles
    readonly property alias collectionAdapter: adapter
    signal activated(string id)
    signal contextRequested(string id, point position)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: Tk.Theme.space.xs
    activeFocusOnTab: enabled
    Accessible.role: Accessible.List
    Accessible.name: tooltip.length > 0 ? tooltip : qsTr("Assets")
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
        GridView {
            id: tiles
            objectName: "assetGridView"
            anchors.fill: parent
            clip: true
            model: root.model
            cellWidth: root.tileWidth + root.tileSpacing
            cellHeight: root.tileHeight + root.tileSpacing
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            keyNavigationWraps: false
            Accessible.role: Accessible.List
            Accessible.name: root.Accessible.name
            delegate: Item {
                id: tile
                required property int index
                objectName: "assetGridDelegate"
                width: root.tileWidth
                height: root.tileHeight
                property string stableId: ""
                property string resourceId: ""
                property string labelText: ""
                property string captionText: ""
                property bool displayActive: true
                function refreshRoles() {
                    stableId = adapter.idAt(index)
                    resourceId = String(adapter.dataAt(index, root.resourceRole) || "")
                    labelText = String(adapter.dataAt(index, root.labelRole) || "")
                    captionText = String(adapter.dataAt(index, root.captionRole) || labelText)
                }
                Component.onCompleted: refreshRoles()
                onIndexChanged: refreshRoles()
                Connections { target: adapter; function onChanged() { tile.refreshRoles() } }
                GridView.onPooled: displayActive = false
                GridView.onReused: { displayActive = true; refreshRoles() }
                Tk.AssetThumbnail {
                    objectName: "assetGridThumbnail_" + tile.stableId
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: root.tileWidth
                    height: Math.max(0, root.tileHeight - Tk.Theme.size.row)
                    provider: root.provider
                    resourceId: tile.resourceId
                    requestKind: "thumbnail"
                    document: root.document
                    viewId: root.viewId
                    active: root.enabled && tile.displayActive && tile.resourceId.length > 0
                    selected: root.selectionModel && root.selectionModel.selectedIds.indexOf(tile.stableId) >= 0
                    caption: ""
                    tooltip: tile.labelText
                }
                Tk.Label {
                    objectName: "assetGridCaption"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Tk.Theme.size.row
                    text: tile.captionText
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Accessible.ignored: true
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: root.selectTile(tile.index, 0)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: function(eventPoint) { root.contextRequested(tile.stableId, eventPoint.position) }
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: tile.labelText
                Accessible.description: tile.captionText
                Accessible.selected: root.selectionModel && root.selectionModel.selectedIds.indexOf(tile.stableId) >= 0
                Accessible.onPressAction: root.selectTile(tile.index, Qt.NoModifier)
            }
        }
        Tk.EmptyState { objectName: "assetGridEmpty"; anchors.centerIn: parent; visible: adapter.count === 0; title: qsTr("No assets") }
    }
    onEnabledChanged: if (!enabled) tiles.currentIndex = -1
    Keys.onPressed: function(event) {
        if (!root.enabled || adapter.count === 0) return
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            const id = adapter.idAt(tiles.currentIndex)
            if (id.length > 0) root.activated(id)
            event.accepted = true
        }
    }
    onActiveFocusChanged: if (activeFocus && tiles.currentIndex < 0 && adapter.count > 0) tiles.currentIndex = 0
    onSelectionModelChanged: if (selectionModel && selectionModel.currentId.length > 0) tiles.currentIndex = adapter.indexOfId(selectionModel.currentId)
    function selectTile(index, modifiers) {
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
