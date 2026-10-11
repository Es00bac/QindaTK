// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/canvas.md#layer-tree-model, D-094): a virtual view of
// a CanvasDocument's layer hierarchy. Rows come from Tk.CanvasLayerTreeModel
// through Tk.VirtualCollectionAdapter, so only expanded rows are projected
// and the ListView builds delegates for visible rows only. Every edit is a
// model request that becomes one document undo step; object selection is
// the shared Tk.CanvasSelectionModel, so canvas and tree never hold two
// selections. The tree never writes selection in response to a selection
// change (reveal only moves its own current row and scroll position).
Tk.Box {
    id: control
    objectName: "layerTree"

    // A hierarchical QAbstractItemModel implementing the documented layer requests.
    // Null keeps the CanvasDocument adapter, preserving existing consumers.
    property var model: null
    readonly property var layerModel: model || canvasModel
    property var document: null          // Tk.CanvasDocument
    property var selectionModel: null    // Tk.CanvasSelectionModel shared with ToolControllers
    property bool showObjects: true
    property real listHeight: Tk.Theme.size.fieldWidth * 3
    property string currentKey: ""
    property string renamingKey: ""
    readonly property var treeModel: control.layerModel
    readonly property alias collection: adapter
    readonly property alias listView: layerList
    readonly property bool dragging: dragState.active

    // Box's own tooltip/accessibility serve interactive boxes; a tree is not one.
    tooltip: qsTr("Layers")
    Accessible.role: Accessible.Tree
    Accessible.name: tooltip
    Accessible.ignored: false
    Accessible.focusable: true
    Accessible.focused: activeFocus
    activeFocusOnTab: true

    // Kept for hosts of the flat slice: the same requests by layer id.
    function activateLayer(id) { return control.layerModel.activate("layer:" + id) }
    function setVisibility(id, visible) { return control.layerModel.setShown("layer:" + id, visible) }
    function setLock(id, locked) { return control.layerModel.setLocked("layer:" + id, locked) }

    function rowOf(key) { return adapter.treeIndexOfId(key) }
    // Remembers a collapsed group so a structure change does not reopen it.
    function setExpanded(key, expanded) {
        const next = Object.assign({}, collapsedGroups)
        if (expanded) delete next[key]
        else next[key] = true
        collapsedGroups = next
        return adapter.setExpanded(key, expanded)
    }
    function setCurrent(key) {
        control.currentKey = key
        const row = rowOf(key)
        if (row >= 0) {
            layerList.currentIndex = row
            layerList.positionViewAtIndex(row, ListView.Contain)
        }
    }
    function expandGroups() {
        const groups = control.layerModel.groupKeys()
        for (let i = 0; i < groups.length; ++i)
            if (!collapsedGroups[groups[i]]) adapter.setExpanded(groups[i], true)
    }
    property var collapsedGroups: ({})
    function toggleExpanded(key) {
        return setExpanded(key, !Boolean(adapter.treeDataAt(rowOf(key), "treeExpanded")))
    }
    function reveal(key) {
        const ancestors = control.layerModel.ancestorKeys(key)
        for (let i = 0; i < ancestors.length; ++i) adapter.setExpanded(ancestors[i], true)
        setCurrent(key)
    }
    function clickRow(key, toggle) {
        control.setCurrent(key)
        control.forceActiveFocus()
        return control.layerModel.select(key, toggle)
    }
    function stepCurrent(delta) {
        const row = Math.max(0, Math.min(adapter.treeCount - 1, rowOf(currentKey) + delta))
        if (adapter.treeCount > 0) setCurrent(adapter.treeIdAt(row))
    }
    function startRename(key) {
        const row = rowOf(key)
        const item = row >= 0 ? layerList.itemAtIndex(row) : null
        if (!item) return false
        // A double-click's second release must not click the row: that would
        // take focus from the field and close the rename at once.
        dragState.reset()
        renameField.y = item.y - layerList.contentY
        renameField.text = String(control.layerModel.nodeInfo(key).label || "")
        control.renamingKey = key
        renameField.forceActiveFocus()
        renameField.selectAll()
        return true
    }
    function finishRename(commit) {
        const key = control.renamingKey
        if (!key.length) return
        control.renamingKey = ""
        if (commit) control.layerModel.rename(key, renameField.text)
        control.forceActiveFocus()
    }
    function addLayer(group) {
        const key = control.layerModel.addLayer(group ? qsTr("Group") : qsTr("Layer"), currentKey, group)
        if (key.length) control.reveal(key)
        return key
    }
    function groupCurrent() {
        const key = control.layerModel.groupLayers([currentKey], qsTr("Group"))
        if (key.length) control.reveal(key)
        return key
    }

    Tk.CanvasLayerTreeModel {
        id: canvasModel
        document: control.document
        selectionModel: control.selectionModel
        showObjects: control.showObjects
    }
    Connections {
        target: control.layerModel
        function onStructureChanged() { control.expandGroups() }
        function onRevealRequested(key) { control.reveal(key) }
    }
    Tk.VirtualCollectionAdapter {
        id: adapter
        model: control.layerModel
        idRole: "key"
    }
    onModelChanged: Qt.callLater(control.expandGroups)
    Component.onCompleted: expandGroups()

    QtObject {
        id: dragState
        property string key: ""
        property bool active: false
        property real startY: 0
        property string targetKey: ""
        property string placement: ""
        property bool valid: false
        property real markerY: 0
        property real markerHeight: 0
        function reset() {
            key = ""; active = false; targetKey = ""; placement = ""; valid = false
        }
    }
    function pressRow(key, point) {
        dragState.reset()
        dragState.key = key
        dragState.startY = point.y
    }
    function dragRow(point) {
        if (!dragState.key.length) return
        if (!dragState.active && Math.abs(point.y - dragState.startY) < Tk.Theme.size.controlSm / 2) return
        dragState.active = true
        const row = layerList.indexAt(Tk.Theme.space.xs, point.y)
        const item = row >= 0 ? layerList.itemAtIndex(row) : null
        if (!item) {
            dragState.valid = false
            return
        }
        const key = adapter.treeIdAt(row)
        const fraction = (point.y - item.y) / Math.max(1, item.height)
        const group = String(control.layerModel.nodeInfo(key).kind) === "group"
        dragState.targetKey = key
        dragState.placement = group && fraction > 0.25 && fraction < 0.75 ? "inside"
                            : fraction < 0.5 ? "above" : "below"
        dragState.valid = control.layerModel.canMove(dragState.key, key, dragState.placement)
        dragState.markerY = item.y - layerList.contentY
                            + (dragState.placement === "below" ? item.height : 0)
        dragState.markerHeight = dragState.placement === "inside" ? item.height : 0
    }
    function releaseRow(key, modifiers) {
        if (dragState.active) {
            if (dragState.valid) control.layerModel.move(dragState.key, dragState.targetKey, dragState.placement)
        } else if (dragState.key === key) {
            control.clickRow(key, (modifiers & Qt.ControlModifier) !== 0)
        }
        dragState.reset()
    }

    Keys.onPressed: function(event) {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const key = control.currentKey
        if (event.key === Qt.Key_Escape && dragState.active) dragState.reset()
        else if (!key.length) {
            if (adapter.treeCount === 0 || (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down))
                return
            control.setCurrent(adapter.treeIdAt(0))
        }
        else if (ctrl && event.key === Qt.Key_Up) control.layerModel.step(key, -1)
        else if (ctrl && event.key === Qt.Key_Down) control.layerModel.step(key, 1)
        else if (ctrl && event.key === Qt.Key_Right) control.layerModel.indent(key)
        else if (ctrl && event.key === Qt.Key_Left) control.layerModel.outdent(key)
        else if (ctrl && event.key === Qt.Key_G && (event.modifiers & Qt.ShiftModifier))
            control.layerModel.ungroup(key)
        else if (ctrl && event.key === Qt.Key_G) control.groupCurrent()
        else if (event.key === Qt.Key_Up) control.stepCurrent(-1)
        else if (event.key === Qt.Key_Down) control.stepCurrent(1)
        else if (event.key === Qt.Key_Right) control.setExpanded(key, true)
        else if (event.key === Qt.Key_Left) control.setExpanded(key, false)
        else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            control.clickRow(key, ctrl)
        else if (event.key === Qt.Key_F2) control.startRename(key)
        else if (event.key === Qt.Key_Delete) control.layerModel.remove(key)
        else if (event.key === Qt.Key_V)
            control.layerModel.setShown(key, !control.layerModel.nodeInfo(key).shown)
        else if (event.key === Qt.Key_L)
            control.layerModel.setLocked(key, !control.layerModel.nodeInfo(key).locked)
        else return
        event.accepted = true
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        width: parent ? parent.width : Tk.Theme.size.fieldWidth

        Tk.Overline { objectName: "layerTreeTitle"; title: qsTr("Layers"); wide: true }

        Tk.Flex {
            objectName: "layerTreeActions"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            width: parent.width
            Tk.IconButton {
                objectName: "layerTreeAddLayer"; small: true; iconName: "plus"
                enabled: !!control.document || !!control.model; tooltip: qsTr("New layer")
                onClicked: control.addLayer(false)
            }
            Tk.IconButton {
                objectName: "layerTreeAddGroup"; small: true; iconName: "folder-plus"
                enabled: !!control.document || !!control.model; tooltip: qsTr("New layer group")
                onClicked: control.addLayer(true)
            }
            Tk.IconButton {
                objectName: "layerTreeGroup"; small: true; iconName: "group"
                enabled: control.currentKey.startsWith("layer:"); tooltip: qsTr("Group layer")
                onClicked: control.groupCurrent()
            }
            Tk.IconButton {
                objectName: "layerTreeUngroup"; small: true; iconName: "ungroup"
                enabled: control.currentKey.startsWith("layer:"); tooltip: qsTr("Ungroup")
                onClicked: control.layerModel.ungroup(control.currentKey)
            }
            Tk.IconButton {
                objectName: "layerTreeMoveUp"; small: true; iconName: "arrow-up"
                enabled: control.currentKey.length > 0; tooltip: qsTr("Move up")
                onClicked: control.layerModel.step(control.currentKey, -1)
            }
            Tk.IconButton {
                objectName: "layerTreeMoveDown"; small: true; iconName: "arrow-down"
                enabled: control.currentKey.length > 0; tooltip: qsTr("Move down")
                onClicked: control.layerModel.step(control.currentKey, 1)
            }
            Tk.IconButton {
                objectName: "layerTreeDelete"; small: true; iconName: "trash-2"
                enabled: control.currentKey.length > 0; tooltip: qsTr("Delete")
                onClicked: control.layerModel.remove(control.currentKey)
            }
        }

        Item {
            id: listFrame
            objectName: "layerTreeListFrame"
            width: parent.width
            implicitHeight: Math.min(Math.max(layerList.contentHeight, Tk.Theme.size.row), control.listHeight)
            height: implicitHeight
            visible: adapter.treeCount > 0
            Tk.Flex.shrink: 0

            ListView {
                id: layerList
                objectName: "layerTreeList"
                anchors.fill: parent
                clip: true
                model: adapter
                reuseItems: true
                keyNavigationEnabled: false
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.Tree
                Accessible.name: qsTr("Document layers")

                delegate: Item {
                    id: row
                    required property int index
                    required property string key
                    required property string kind
                    required property string itemId
                    required property string label
                    required property string detail
                    required property string iconName
                    required property bool shown
                    required property bool locked
                    required property bool effectiveVisible
                    required property bool effectiveLocked
                    required property bool selected
                    required property bool active
                    required property int childCount
                    required property int treeDepth
                    required property bool treeExpanded
                    readonly property string part: kind === "object" ? "object" : "layer"
                    readonly property bool current: control.currentKey === key

                    objectName: part + "Row_" + itemId
                    width: layerList.width
                    height: Tk.Theme.size.row
                    Accessible.role: Accessible.TreeItem
                    Accessible.name: label
                    Accessible.description: detail + ", " + (effectiveVisible ? qsTr("Visible") : qsTr("Hidden"))
                                            + ", " + (effectiveLocked ? qsTr("Locked") : qsTr("Unlocked"))
                    Accessible.selectable: true
                    Accessible.selected: selected || active
                    Accessible.focusable: true
                    Accessible.focused: control.activeFocus && current
                    Accessible.onPressAction: control.clickRow(key, false)

                    Rectangle {
                        anchors.fill: parent
                        radius: Tk.Theme.radius.sm
                        color: row.selected || row.active ? Tk.Theme.color.selection
                             : rowPointer.containsMouse ? Tk.Theme.color.hover
                             : Tk.Theme.alpha(Tk.Theme.color.panel, Tk.Theme.opacity.hidden)
                        border.width: control.activeFocus && row.current ? Tk.Theme.size.focusRing : 0
                        border.color: Tk.Theme.color.focus
                    }
                    MouseArea {
                        id: rowPointer
                        anchors.fill: parent
                        z: -1
                        hoverEnabled: true
                        preventStealing: true
                        acceptedButtons: Qt.LeftButton
                        onPressed: function(mouse) {
                            control.pressRow(row.key, row.mapToItem(layerList.contentItem, mouse.x, mouse.y))
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) control.dragRow(row.mapToItem(layerList.contentItem, mouse.x, mouse.y))
                        }
                        onReleased: function(mouse) { control.releaseRow(row.key, mouse.modifiers) }
                        onDoubleClicked: control.startRename(row.key)
                        onCanceled: dragState.reset()
                    }
                    Tk.Flex {
                        anchors.fill: parent
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        paddingLeft: Tk.Theme.space.xs + row.treeDepth * Tk.Theme.space.lg
                        paddingRight: Tk.Theme.space.xs

                        Tk.IconButton {
                            objectName: row.part + "Disclosure_" + row.itemId
                            small: true
                            opacity: row.childCount > 0 ? Tk.Theme.opacity.full : Tk.Theme.opacity.hidden
                            enabled: row.childCount > 0
                            focusPolicy: Qt.NoFocus
                            iconName: row.treeExpanded ? "chevron-down" : "chevron-right"
                            tooltip: row.treeExpanded ? qsTr("Collapse %1").arg(row.label)
                                                      : qsTr("Expand %1").arg(row.label)
                            onClicked: control.toggleExpanded(row.key)
                        }
                        Tk.Icon {
                            name: row.iconName
                            size: Tk.Theme.size.iconSm
                            color: row.active ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                        }
                        Tk.Label {
                            objectName: row.part + "Name_" + row.itemId
                            text: row.label
                            muted: !row.effectiveVisible
                            elide: Text.ElideRight
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                        }
                        Tk.IconButton {
                            objectName: row.part + "Visibility_" + row.itemId
                            small: true
                            focusPolicy: Qt.NoFocus
                            iconName: row.shown ? "eye" : "eye-off"
                            tooltip: row.shown ? qsTr("Hide %1").arg(row.label) : qsTr("Show %1").arg(row.label)
                            Accessible.role: Accessible.CheckBox
                            Accessible.checkable: true
                            Accessible.checked: row.shown
                            onClicked: control.layerModel.setShown(row.key, !row.shown)
                        }
                        Tk.IconButton {
                            objectName: row.part + "Lock_" + row.itemId
                            small: true
                            focusPolicy: Qt.NoFocus
                            iconName: row.locked ? "lock" : "unlock"
                            tooltip: row.locked ? qsTr("Unlock %1").arg(row.label) : qsTr("Lock %1").arg(row.label)
                            Accessible.role: Accessible.CheckBox
                            Accessible.checkable: true
                            Accessible.checked: row.locked
                            onClicked: control.layerModel.setLocked(row.key, !row.locked)
                        }
                    }
                }
            }

            Rectangle {
                objectName: "layerTreeDropMarker"
                visible: dragState.active && dragState.valid
                x: Tk.Theme.space.xs
                y: dragState.markerY - (dragState.markerHeight > 0 ? 0 : Tk.Theme.size.focusRing / 2)
                width: parent.width - Tk.Theme.space.xs * 2
                height: dragState.markerHeight > 0 ? dragState.markerHeight : Tk.Theme.size.focusRing
                color: dragState.markerHeight > 0 ? "transparent" : Tk.Theme.color.accent
                border.width: dragState.markerHeight > 0 ? Tk.Theme.size.focusRing : 0
                border.color: Tk.Theme.color.accent
                radius: Tk.Theme.radius.sm
            }

            Tk.TextField {
                id: renameField
                objectName: "layerTreeRename"
                visible: control.renamingKey.length > 0
                x: Tk.Theme.space.xl
                width: parent.width - Tk.Theme.space.xl * 2
                height: Tk.Theme.size.row
                small: true
                tooltip: qsTr("Name")
                onAccepted: control.finishRename(true)
                // TextField's own Escape handler declines unless clearable, so
                // the key reaches `pressed` here, which cancels the rename.
                Keys.onPressed: function(event) {
                    if (event.key !== Qt.Key_Escape) return
                    control.finishRename(false)
                    event.accepted = true
                }
                onActiveFocusChanged: if (!activeFocus && visible) control.finishRename(true)
            }
        }

        Tk.Label {
            objectName: "layerTreeEmpty"
            visible: adapter.treeCount === 0
            text: control.document || control.model ? qsTr("No layers") : qsTr("No document")
            muted: true
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }
        Tk.Caption {
            objectName: "layerTreeError"
            visible: text.length > 0
            text: control.layerModel.lastError
            width: parent.width
            wrapMode: Text.Wrap
        }
    }
}
