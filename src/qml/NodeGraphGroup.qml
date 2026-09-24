// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk
pragma ComponentBehavior: Bound

// AGENT-NOTE: NodeGraph's frame for one host-owned group (docs/controls.md).
// Its bounds follow the member cards; header gestures only ask `graph` to
// emit groupSelected/groupMoveRequested, and the host moves the members.
Rectangle {
    id: groupFrame
    required property var graph
    required property var modelData
    readonly property string groupId: String(modelData.groupId)
    readonly property string title: String(modelData.title || modelData.groupId)
    readonly property var bounds: graph.groupBounds(modelData)
    readonly property bool selected: groupId === graph.selectedGroupId
    readonly property point offset: graph.draggingGroupId === groupId && !graph.dragCanceled
        ? graph.groupDragDelta : Qt.point(0, 0)
    objectName: "nodeGroup_" + groupId
    visible: bounds !== null
    x: graph.panX + (bounds ? bounds.x : 0) * graph.zoom + offset.x
    y: graph.panY + (bounds ? bounds.y : 0) * graph.zoom + offset.y
    width: bounds ? bounds.width : 0
    height: bounds ? bounds.height : 0
    scale: graph.zoom
    transformOrigin: Item.TopLeft
    radius: Tk.Theme.radius.md
    color: selected ? Tk.Theme.color.accentSubtle : Tk.Theme.color.surface
    border.color: selected ? Tk.Theme.color.accent : Tk.Theme.color.borderStrong
    border.width: Tk.Theme.size.border
    Accessible.role: Accessible.Grouping
    Accessible.name: title
    Accessible.description: qsTr("%1 nodes").arg((modelData.nodeIds || []).length)

    Item {
        id: groupHeader
        objectName: "nodeGroupHeader_" + groupFrame.groupId
        width: parent.width
        height: Tk.Theme.size.header
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Group %1").arg(groupFrame.title)
        Accessible.description: qsTr("Select, then Shift+arrows move it and Ctrl+Shift+G ungroups")
        Accessible.onPressAction: groupFrame.graph.groupSelected(groupFrame.groupId)
        Keys.onSpacePressed: groupFrame.graph.groupSelected(groupFrame.groupId)
        Keys.onReturnPressed: groupFrame.graph.groupSelected(groupFrame.groupId)
        Tk.Label {
            anchors.fill: parent
            anchors.leftMargin: Tk.Theme.space.sm
            anchors.rightMargin: Tk.Theme.space.sm
            text: groupFrame.title
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Tk.Theme.font.caption
            color: groupFrame.selected ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: Tk.Theme.radius.md
            border.color: Tk.Theme.color.focus
            border.width: Tk.Theme.size.focusRing
            visible: groupHeader.activeFocus
        }
        HoverHandler { id: groupHover }
        Tk.ToolTip {
            text: qsTr("Group %1: click to select, drag to move its nodes").arg(groupFrame.title)
            visible: groupHover.hovered
        }
        TapHandler { onTapped: groupFrame.graph.groupSelected(groupFrame.groupId) }
        DragHandler {
            target: null
            onActiveChanged: {
                if (active) {
                    groupFrame.graph.dragCanceled = false
                    groupFrame.graph.draggingGroupId = groupFrame.groupId
                    return
                }
                Qt.callLater(groupFrame.graph.finishDrag, "group", groupFrame.groupId,
                             groupFrame.graph.groupDragDelta.x, groupFrame.graph.groupDragDelta.y)
            }
            // Scene-space pixels, like NodeGraphNode's drag delta.
            onTranslationChanged: if (active)
                groupFrame.graph.groupDragDelta = Qt.point(translation.x, translation.y)
            onCanceled: groupFrame.graph.dragCanceled = true
        }
    }
}
