// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: root
    objectName: "sceneOutliner"

    property var scene: null
    property bool keepWorldOnDrop: true
    property real listHeight: Tk.Theme.size.panelMinHeight
    property var expandedIds: ({})
    property string tooltip: qsTr("3D scene hierarchy")

    signal reparentRequested(string entityId, string parentId, bool keepWorld)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Math.min(listHeight, Math.max(Tk.Theme.size.panelMinHeight,
                                                   rows.contentHeight))
    padding: Tk.Theme.space.xs
    Accessible.role: Accessible.Tree
    Accessible.name: tooltip
    Accessible.focusable: enabled
    activeFocusOnTab: enabled

    ListModel { id: rowsModel }

    function entityFor(id) {
        if (!root.scene) return null
        const entities = root.scene.entities
        for (let i = 0; i < entities.length; ++i)
            if (String(entities[i].entityId) === id) return entities[i]
        return null
    }

    function rebuildRows() {
        rowsModel.clear()
        if (!root.scene) return
        const entities = root.scene.entities
        const byParent = ({})
        const known = ({})
        for (let i = 0; i < entities.length; ++i) {
            const entity = entities[i]
            const id = String(entity.entityId)
            const parent = String(entity.parentId || "")
            known[id] = true
            if (expandedIds[id] === undefined) expandedIds[id] = true
            if (!byParent[parent]) byParent[parent] = []
            byParent[parent].push(entity)
        }
        const nextExpanded = ({})
        for (const id in expandedIds) if (known[id]) nextExpanded[id] = expandedIds[id]
        expandedIds = nextExpanded
        function appendChildren(parentId, depth) {
            const children = byParent[parentId] || []
            for (let i = 0; i < children.length; ++i) {
                const e = children[i]
                const id = String(e.entityId)
                const nested = !!byParent[id] && byParent[id].length > 0
                const expanded = !!expandedIds[id]
                rowsModel.append({ entityId: id, parentId: String(e.parentId || ""),
                    label: String(e.name), primitive: String(e.primitive), depth: depth,
                    hasChildren: nested, expanded: expanded, effectiveVisible: !!e.visible,
                    localVisible: e.localVisible !== false,
                    color: e.color })
                if (nested && expanded) appendChildren(id, depth + 1)
            }
        }
        appendChildren("", 0)
        root.restoreSelection()
    }

    function restoreSelection() {
        if (!root.scene || rows.count === 0) return
        for (let i = 0; i < rows.count; ++i) {
            if (rows.itemAtIndex(i) && rows.itemAtIndex(i).entityId === root.scene.selectedId) {
                rows.currentIndex = i
                rows.positionViewAtIndex(i, ListView.Contain)
                return
            }
        }
        const selectedIndex = rowsModelIndexForId(root.scene.selectedId)
        if (selectedIndex >= 0) {
            rows.currentIndex = selectedIndex
            rows.positionViewAtIndex(selectedIndex, ListView.Contain)
        }
    }

    function rowsModelIndexForId(id) {
        for (let i = 0; i < rowsModel.count; ++i)
            if (rowsModel.get(i).entityId === id) return i
        return -1
    }

    function selectEntity(id) {
        if (root.scene) root.scene.select(id)
        const index = rowsModelIndexForId(id)
        if (index >= 0) rows.currentIndex = index
    }

    function toggleExpanded(id) {
        expandedIds[id] = !expandedIds[id]
        expandedIds = Object.assign({}, expandedIds)
        rebuildRows()
    }

    function toggleVisibility(id) {
        const e = entityFor(id)
        if (e && root.scene) root.scene.setAppearance(id, e.color, !e.localVisible)
    }

    function reparentEntity(id, parentId) {
        if (!root.scene || id === parentId || entityIsAncestor(id, parentId)) return false
        reparentRequested(id, parentId, keepWorldOnDrop)
        if (parentId.length > 0 && !expandedIds[parentId]) {
            expandedIds[parentId] = true
            expandedIds = Object.assign({}, expandedIds)
        }
        return true
    }

    function entityIsAncestor(id, candidateParentId) {
        let current = entityFor(candidateParentId)
        while (current && String(current.parentId).length > 0) {
            if (String(current.parentId) === id) return true
            current = entityFor(String(current.parentId))
        }
        return false
    }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }

    contentItem: ListView {
        id: rows
        objectName: "sceneOutlinerRows"
        anchors.fill: parent
        clip: true
        model: rowsModel
        spacing: 0
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        Accessible.role: Accessible.Tree
        Accessible.name: root.tooltip

        delegate: Item {
            id: entry
            required property int index
            required property string entityId
            required property string parentId
            required property string label
            required property string primitive
            required property int depth
            required property bool hasChildren
            required property bool expanded
            required property bool effectiveVisible
            required property bool localVisible
            required property color color

            objectName: "sceneOutlinerRow_" + entityId
            width: rows.width
            height: Tk.Theme.size.row
            property bool dropTarget: false
            readonly property bool selected: root.scene && root.scene.selectedId === entityId

            T.AbstractButton {
                id: rowButton
                objectName: "sceneOutlinerSelect_" + entry.entityId
                anchors.fill: parent
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.TreeItem
                Accessible.name: entry.label
                Accessible.description: entry.primitive + (entry.effectiveVisible ? qsTr(", visible") : qsTr(", hidden"))
                Accessible.selected: entry.selected
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                onClicked: {
                    root.selectEntity(entry.entityId)
                    root.forceActiveFocus()
                }
                background: Rectangle {
                    color: entry.dropTarget ? Tk.Theme.color.controlHoverBg
                         : entry.selected ? Tk.Theme.color.selection
                         : rowButton.down ? Tk.Theme.color.pressed
                         : rowButton.hovered ? Tk.Theme.color.hover : "transparent"
                    radius: Tk.Theme.radius.xs
                }
                contentItem: Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    paddingLeft: Tk.Theme.space.xs + entry.depth * Tk.Theme.space.lg
                    paddingRight: Tk.Theme.space.xs

                    Tk.IconButton {
                        objectName: "sceneOutlinerExpand_" + entry.entityId
                        visible: entry.hasChildren
                        small: true
                        ghost: true
                        iconName: entry.expanded ? "chevron-down" : "chevron-right"
                        tooltip: entry.expanded ? qsTr("Collapse %1").arg(entry.label)
                                                 : qsTr("Expand %1").arg(entry.label)
                        Accessible.name: tooltip
                        onClicked: root.toggleExpanded(entry.entityId)
                    }
                    Tk.Icon {
                        name: entry.primitive === "sphere" ? "circle" : entry.primitive === "cylinder" ? "database" : "box"
                        color: entry.selected ? Tk.Theme.color.accent : entry.color
                        size: Tk.Theme.size.iconSm
                        Accessible.ignored: true
                    }
                    Tk.Label {
                        objectName: "sceneOutlinerName_" + entry.entityId
                        text: entry.label
                        elide: Text.ElideRight
                        color: entry.effectiveVisible ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                        Tk.Flex.grow: 1
                        Accessible.ignored: true
                    }
                    Tk.Caption {
                        text: entry.primitive
                        color: Tk.Theme.color.textMuted
                        Accessible.ignored: true
                    }
                    Tk.IconButton {
                        objectName: "sceneOutlinerVisibility_" + entry.entityId
                        small: true
                        ghost: true
                        iconName: entry.localVisible ? "eye" : "eye-off"
                        tooltip: entry.localVisible ? qsTr("Hide %1").arg(entry.label)
                                                     : qsTr("Show %1").arg(entry.label)
                        Accessible.name: tooltip
                        Accessible.role: Accessible.CheckBox
                        onClicked: root.toggleVisibility(entry.entityId)
                    }
                }
                Accessible.onPressAction: if (enabled) click()
            }

            Drag.keys: ["application/x-qindatk-scene-entity"]
            Drag.mimeData: ({"application/x-qindatk-scene-entity": entry.entityId})
            Drag.active: dragHandler.active
            Drag.supportedActions: Qt.MoveAction
            DragHandler { id: dragHandler; target: null; acceptedButtons: Qt.LeftButton }

            DropArea {
                objectName: "sceneOutlinerDrop_" + entry.entityId
                anchors.fill: parent
                keys: ["application/x-qindatk-scene-entity"]
                onEntered: function(drag) { entry.dropTarget = !!drag.source && drag.source.entityId !== entry.entityId }
                onExited: entry.dropTarget = false
                onDropped: function(drop) {
                    entry.dropTarget = false
                    if (drop.source && drop.source.entityId)
                        root.reparentEntity(drop.source.entityId, entry.entityId)
                }
            }

            Accessible.role: Accessible.TreeItem
            Accessible.name: entry.label
            Accessible.description: entry.primitive + (entry.effectiveVisible ? qsTr(", visible") : qsTr(", hidden"))
        }
    }

    Keys.onPressed: function(event) {
        if (!root.enabled || rows.count === 0) return
        const current = Math.max(0, rows.currentIndex)
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            rows.currentIndex = Math.max(0, Math.min(rows.count - 1,
                current + (event.key === Qt.Key_Up ? -1 : 1)))
            const row = rowsModel.get(rows.currentIndex)
            root.selectEntity(row.entityId)
            rows.positionViewAtIndex(rows.currentIndex, ListView.Contain)
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            const row = rowsModel.get(current)
            if (row.hasChildren) root.toggleExpanded(row.entityId)
            else if (event.key === Qt.Key_Left && row.parentId.length > 0)
                root.selectEntity(row.parentId)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.selectEntity(rowsModel.get(current).entityId)
        } else return
        event.accepted = true
    }

    Connections {
        target: root.scene
        function onChanged() { root.rebuildRows() }
        function onSelectionChanged() { root.restoreSelection() }
    }
    Component.onCompleted: rebuildRows()
    onSceneChanged: rebuildRows()
}
