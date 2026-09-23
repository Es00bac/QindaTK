// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: root
    objectName: "virtualTree"
    property var model: null
    property var identityModel: null
    property Tk.SelectionModel selectionModel: null
    property string idRole: "id"
    property string labelRole: "label"
    property string depthRole: "depth"
    property string hasChildrenRole: "hasChildren"
    property string expandedRole: "expanded"
    property string loadingRole: "loading"
    property string parentRole: "parentId"
    property string tooltip: ""
    // Hosts with a visible-row projection may opt out of the adapter projection.
    property bool flatProjection: false
    readonly property alias treeView: rows
    readonly property alias collectionAdapter: adapter
    signal activated(string id)
    signal expansionRequested(string id, bool expanded)
    signal renameRequested(string id, string text)
    signal moveRequested(string id, string parentId, int position)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: Tk.Theme.space.xs
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Tree
    Accessible.name: tooltip.length > 0 ? tooltip : qsTr("Tree")
    Accessible.focusable: enabled

    Tk.VirtualCollectionAdapter {
        id: adapter
        model: root.model
        identityModel: root.identityModel ? root.identityModel : root.model
        selectionModel: root.selectionModel
        idRole: root.idRole
    }
    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: ListView {
        id: rows
        objectName: "virtualTreeView"
        clip: true
        model: root.flatProjection ? root.model : adapter
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        keyNavigationWraps: false
        spacing: 0
        Accessible.role: Accessible.Tree
        Accessible.name: root.Accessible.name
        delegate: Item {
            id: node
            required property int index
            objectName: "virtualTreeDelegate"
            width: rows.width
            height: Tk.Theme.size.row
            property string stableId: ""
            property string labelText: ""
            property int itemDepth: 0
            property bool hasChildren: false
            property bool expanded: false
            property bool loading: false
            function roleValue(name) {
                return root.flatProjection ? adapter.dataAt(index, name) : adapter.treeDataAt(index, name)
            }
            function refreshRoles() {
                stableId = root.flatProjection ? adapter.idAt(index) : adapter.treeIdAt(index)
                labelText = String(roleValue(root.labelRole) || "")
                itemDepth = Number(root.flatProjection ? roleValue(root.depthRole) || 0
                                                        : roleValue("treeDepth") || 0)
                hasChildren = Boolean(root.flatProjection ? roleValue(root.hasChildrenRole)
                                                           : roleValue("treeHasChildren"))
                expanded = Boolean(root.flatProjection ? roleValue(root.expandedRole)
                                                        : roleValue("treeExpanded"))
                loading = Boolean(root.flatProjection ? roleValue(root.loadingRole)
                                                       : roleValue("treeLoading"))
            }
            Component.onCompleted: refreshRoles()
            onIndexChanged: refreshRoles()
        Connections { target: adapter; function onChanged() { node.refreshRoles() } }
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Rectangle {
                    width: node.itemDepth * Tk.Theme.space.lg
                    height: Tk.Theme.size.border
                    color: Tk.Theme.alpha(Tk.Theme.color.surface, Tk.Theme.opacity.hidden)
                }
                Tk.IconButton {
                    objectName: "treeDisclosure_" + node.stableId
                    visible: node.hasChildren
                    enabled: root.enabled && !node.loading
                    small: true
                    ghost: true
                    iconName: node.loading ? "loader-circle" : node.expanded ? "chevron-down" : "chevron-right"
                    tooltip: node.expanded ? qsTr("Collapse %1").arg(node.labelText)
                                           : qsTr("Expand %1").arg(node.labelText)
                    Accessible.name: tooltip
                    onClicked: root.requestExpansion(node.stableId, node.index, !node.expanded)
                }
                Tk.ListRow {
                    objectName: "treeRow_" + node.stableId
                    text: node.labelText
                    selected: root.selectionModel && root.selectionModel.selectedIds.indexOf(node.stableId) >= 0
                    active: root.selectionModel && root.selectionModel.currentId === node.stableId
                    tooltip: node.labelText
                    Tk.Flex.grow: 1
                    onClicked: {
                        rows.currentIndex = node.index
                        root.selectNode(node.stableId)
                        root.forceActiveFocus()
                    }
                    onDoubleClicked: root.activated(node.stableId)
                }
            }
            Accessible.role: Accessible.TreeItem
            Accessible.name: node.labelText
            Accessible.description: node.hasChildren ? node.expanded ? qsTr("Expanded") : qsTr("Collapsed") : ""
            Accessible.selected: root.selectionModel && root.selectionModel.selectedIds.indexOf(node.stableId) >= 0
            Accessible.focusable: root.enabled
            Accessible.focused: root.activeFocus && rows.currentIndex === node.index
            Accessible.onPressAction: {
                rows.currentIndex = node.index
                root.selectNode(node.stableId)
                root.forceActiveFocus()
                root.activated(node.stableId)
            }
        }
    }
    Keys.onPressed: function(event) {
        if (!root.enabled || rows.count === 0) return
        if (event.key === Qt.Key_F2) root.requestRename(rows.currentIndex)
        else if ((event.modifiers & Qt.ControlModifier)
                 && (event.key === Qt.Key_Up || event.key === Qt.Key_Down))
            root.requestMove(rows.currentIndex, event.key === Qt.Key_Up ? -1 : 1)
        else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            const nextRow = Math.max(0, Math.min(rows.count - 1,
                rows.currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
            rows.currentIndex = nextRow
            root.syncCurrent(nextRow)
            rows.positionViewAtIndex(nextRow, ListView.Contain)
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
            root.requestExpansionForCurrent(rows.currentIndex, event.key === Qt.Key_Right)
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            const id = root.flatProjection ? adapter.idAt(rows.currentIndex)
                                           : adapter.treeIdAt(rows.currentIndex)
            if (id.length > 0) root.activated(id)
        } else return
        event.accepted = true
    }
    onActiveFocusChanged: if (activeFocus && rows.currentIndex < 0 && rows.count > 0) rows.currentIndex = 0
    Connections {
        target: root.selectionModel
        function onCurrentChanged() { root.restoreCurrent() }
    }
    Connections {
        target: adapter
        function onChanged() { root.restoreCurrent() }
    }
    Tk.EmptyState { anchors.centerIn: parent; visible: adapter.count === 0; title: qsTr("No items") }

    function requestExpansion(id, row, expanded) {
        if (!id) return false
        if (flatProjection) {
            if (expanded && adapter.canFetchMoreForId(id)) adapter.fetchMoreForId(id)
        } else if (!adapter.setExpanded(id, expanded)) return false
        expansionRequested(id, expanded)
        return true
    }
    function selectNode(id) { return selectionModel ? selectionModel.select(id) : false }
    function syncCurrent(row) {
        if (!selectionModel || row < 0) return
        const id = flatProjection ? adapter.idAt(row) : adapter.treeIdAt(row)
        if (id.length > 0 && selectionModel.currentId !== id) selectionModel.select(id)
    }
    function restoreCurrent() {
        if (!selectionModel) return
        const row = flatProjection ? adapter.indexOfId(selectionModel.currentId)
                                   : adapter.treeIndexOfId(selectionModel.currentId)
        if (row >= 0 && rows.currentIndex !== row) rows.currentIndex = row
    }
    function requestExpansionForCurrent(row, expanded) {
        if (row < 0) return false
        const id = flatProjection ? adapter.idAt(row) : adapter.treeIdAt(row)
        return requestExpansion(id, row, expanded)
    }
    function requestRename(row) {
        if (row < 0) return false
        const id = flatProjection ? adapter.idAt(row) : adapter.treeIdAt(row)
        if (!id) return false
        renameRequested(id, String(adapter.dataForId(id, labelRole) || ""))
        return true
    }
    function requestMove(row, delta) {
        if (row < 0) return false
        const id = flatProjection ? adapter.idAt(row) : adapter.treeIdAt(row)
        if (!id) return false
        const parentId = String(adapter.dataForId(id, parentRole) || "")
        moveRequested(id, parentId, row + delta)
        return true
    }
}
