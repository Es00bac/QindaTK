// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk
pragma ComponentBehavior: Bound

// AGENT-NOTE: NodeGraph's toolbar part (docs/controls.md). Buttons only ask
// `graph` to emit requests; search is the one view-local state it edits.
Tk.Flex {
    id: bar
    required property var graph
    readonly property alias searchField: search
    objectName: "nodeGraphToolbar"
    padding: Tk.Theme.space.xs
    gap: Tk.Theme.space.xs
    direction: Tk.Flex.Row

    Rectangle {
        radius: Tk.Theme.radius.md
        color: Tk.Theme.color.islandBg
        border.color: Tk.Theme.color.islandBorder
        border.width: Tk.Theme.size.border
        implicitWidth: zoomReadout.implicitWidth + Tk.Theme.space.md
        implicitHeight: Tk.Theme.size.control
        Tk.Label {
            id: zoomReadout
            anchors.centerIn: parent
            text: qsTr("%1%").arg(Math.round(bar.graph.zoom * 100))
            color: Tk.Theme.color.textMuted
            font.pixelSize: Tk.Theme.font.caption
        }
    }

    Tk.SearchField {
        id: search
        objectName: "nodeGraphSearch"
        width: Tk.Theme.size.fieldWidth
        placeholderText: qsTr("Find node")
        tooltip: qsTr("Find nodes by title or type; Enter or F3 shows the next match")
        text: bar.graph.searchText
        onSearched: function(text) { bar.graph.searchText = text }
        onAccepted: {
            bar.graph.searchText = search.text
            bar.graph.findNext(false)
        }
    }

    Tk.IconButton {
        id: addButton
        objectName: "nodeGraphAddButton"
        iconName: "plus"
        tooltip: qsTr("Add node")
        enabled: (bar.graph.nodeTypes || []).length > 0
        onClicked: addMenu.popup(addButton, 0, addButton.height)
    }

    Tk.IconButton {
        objectName: "nodeGraphGroupButton"
        iconName: "group"
        tooltip: qsTr("Group selected nodes (Ctrl+G)")
        enabled: bar.graph.selectionIds().length > 0
        onClicked: bar.graph.requestGroup()
    }

    Tk.IconButton {
        objectName: "nodeGraphUngroupButton"
        iconName: "ungroup"
        tooltip: qsTr("Ungroup the selected group (Ctrl+Shift+G)")
        enabled: Boolean(bar.graph.selectedGroupId)
        onClicked: bar.graph.requestUngroup()
    }

    Tk.Button {
        text: qsTr("Disconnect")
        small: true
        enabled: Boolean(bar.graph.selectedEdgeId)
        tooltip: qsTr("Request disconnection of the selected link")
        onClicked: bar.graph.requestDisconnect(bar.graph.selectedEdgeId)
    }

    Tk.Menu {
        id: addMenu
        objectName: "nodeGraphAddMenu"
        Instantiator {
            model: bar.graph.nodeTypes || []
            delegate: Tk.MenuItem {
                id: typeItem
                required property var modelData
                objectName: "nodeGraphAdd_" + modelData.type
                text: String(modelData.title || modelData.type)
                onTriggered: bar.graph.requestAddNode(String(typeItem.modelData.type))
            }
            onObjectAdded: function(index, object) { addMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { addMenu.removeItem(object) }
        }
    }
}
