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
            keyNavigationEnabled: false
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
                    // AGENT-GUARD: GridView can retain offscreen delegates without
                    // pooling them (notably the former current item on Qt 6.8).
                    // Keep requests inside the viewport plus one prefetch cell.
                    active: root.enabled && tile.displayActive && tile.resourceId.length > 0
                            && tile.x + tile.width > tiles.contentX - tiles.cellWidth
                            && tile.x < tiles.contentX + tiles.width + tiles.cellWidth
                            && tile.y + tile.height > tiles.contentY - tiles.cellHeight
                            && tile.y < tiles.contentY + tiles.height + tiles.cellHeight
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
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.contextRequested(tile.stableId, Qt.point(mouse.x, mouse.y))
                            return
                        }
                        root.selectTile(tile.index, mouse.modifiers)
                        root.forceActiveFocus()
                    }
                    onDoubleClicked: function(mouse) {
                        if (mouse.button !== Qt.LeftButton) return
                        root.selectTile(tile.index, Qt.NoModifier)
                        root.forceActiveFocus()
                        root.activated(tile.stableId)
                    }
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: tile.labelText
                Accessible.description: tile.captionText
                Accessible.focusable: root.enabled
                Accessible.focused: root.activeFocus && tiles.currentIndex === tile.index
                Accessible.selected: root.selectionModel && root.selectionModel.selectedIds.indexOf(tile.stableId) >= 0
                Accessible.onPressAction: {
                    root.selectTile(tile.index, Qt.NoModifier)
                    root.forceActiveFocus()
                    root.activated(tile.stableId)
                }
            }
        }
        Tk.EmptyState { objectName: "assetGridEmpty"; anchors.centerIn: parent; visible: adapter.count === 0; title: qsTr("No assets") }
    }
    onEnabledChanged: if (!enabled) tiles.currentIndex = -1
    Keys.onPressed: function(event) {
        if (!root.enabled || adapter.count === 0) return
        let index = tiles.currentIndex < 0 ? 0 : tiles.currentIndex
        const columns = Math.max(1, Math.floor(tiles.width / tiles.cellWidth))
        if (event.key === Qt.Key_Left) index -= 1
        else if (event.key === Qt.Key_Right) index += 1
        else if (event.key === Qt.Key_Up) index -= columns
        else if (event.key === Qt.Key_Down) index += columns
        else if (event.key === Qt.Key_Home) index = 0
        else if (event.key === Qt.Key_End) index = adapter.count - 1
        else if (event.key === Qt.Key_Space) {
            root.selectTile(index, event.modifiers)
            event.accepted = true
            return
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            const id = adapter.idAt(index)
            if (id.length > 0) root.activated(id)
            event.accepted = true
            return
        } else return
        index = Math.max(0, Math.min(adapter.count - 1, index))
        root.selectTile(index, event.modifiers)
        tiles.positionViewAtIndex(index, GridView.Contain)
        event.accepted = true
    }
    onActiveFocusChanged: if (activeFocus && tiles.currentIndex < 0 && adapter.count > 0) tiles.currentIndex = 0
    onSelectionModelChanged: restoreCurrent()
    Connections {
        target: root.selectionModel
        function onCurrentChanged() { root.restoreCurrent() }
    }
    Connections {
        target: adapter
        function onChanged() { root.restoreCurrent() }
    }
    function restoreCurrent() {
        if (!selectionModel) return
        const row = adapter.indexOfId(selectionModel.currentId)
        if (row < 0) { tiles.currentIndex = -1; return }
        if (tiles.currentIndex !== row) tiles.currentIndex = row
        tiles.positionViewAtIndex(row, GridView.Contain)
    }
    function selectTile(index, modifiers) {
        if (!selectionModel || index < 0 || index >= adapter.count) return false
        const id = adapter.idAt(index)
        let accepted = false
        if (modifiers & Qt.ShiftModifier) {
            const anchor = adapter.indexOfId(selectionModel.anchorId)
            if (anchor >= 0) accepted = selectionModel.extendTo(id, adapter.rangeIds(anchor, index))
            else accepted = selectionModel.select(id)
        } else if (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
            accepted = selectionModel.toggle(id)
        } else {
            accepted = selectionModel.select(id)
        }
        if (accepted) root.restoreCurrent()
        return accepted
    }
}
