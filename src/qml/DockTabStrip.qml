// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The tab row of a slot that holds several panels (a tab group). 22px,
// caption font, accent underline on the active tab. Click activates
// (DockModel.activateInGroup), a horizontal drag reorders on release
// (moveInGroup), a drag that leaves the strip vertically tears the panel
// out (host.beginDrag), right-click opens the group menu.
Item {
    id: strip

    required property var host
    property var panelIds: []
    property string activeId: ""
    property string zone: ""

    implicitHeight: Tk.Theme.size.tab
    implicitWidth: row.implicitWidth

    function indexForX(x, excludedId) {
        let index = 0
        for (let i = 0; i < tabs.count; ++i) {
            const tab = tabs.itemAt(i)
            if (tab === null || tab.panelId === excludedId) {
                continue
            }
            if (tab.x + tab.width / 2 < x) {
                ++index
            }
        }
        return index
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.panelAlt
    }
    Tk.Divider {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        align: Tk.Flex.Stretch
        gap: 1
        paddingLeft: Tk.Theme.space.xs

        Repeater {
            id: tabs
            // A lone panel has no strip and no tab items (keeps dumps clean).
            model: strip.panelIds.length > 1 ? strip.panelIds : []

            delegate: Item {
                id: tab
                required property string modelData
                required property int index
                readonly property string panelId: modelData
                readonly property bool active: panelId === strip.activeId
                objectName: "dockTab_" + panelId
                implicitWidth: label.implicitWidth + Tk.Theme.space.sm * 2
                Tk.Flex.shrink: 1
                Tk.Flex.minWidth: Tk.Theme.space.xl

                Rectangle {
                    anchors.fill: parent
                    color: tab.active ? Tk.Theme.color.panel
                         : tabHover.hovered ? Tk.Theme.color.hover : "transparent"
                }
                Tk.Caption {
                    id: label
                    anchors.fill: parent
                    anchors.leftMargin: Tk.Theme.space.sm
                    anchors.rightMargin: Tk.Theme.space.sm
                    text: strip.host.panelTitle(tab.panelId)
                    muted: !tab.active
                    color: tab.active ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                    font.weight: tab.active ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    color: Tk.Theme.color.accent
                    visible: tab.active
                }

                HoverHandler { id: tabHover }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: strip.host.model.activateInGroup(tab.panelId)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(eventPoint) {
                        groupMenu.panelId = tab.panelId
                        groupMenu.popup(tab, eventPoint.position.x, eventPoint.position.y)
                    }
                }
                DragHandler {
                    id: tabDrag
                    target: null
                    dragThreshold: 8
                    property bool torn: false
                    property point lastScene: Qt.point(0, 0)
                    property real lastX: 0
                    onActiveChanged: {
                        if (active) {
                            torn = false
                            return
                        }
                        if (torn) {
                            torn = false
                            strip.host.endDrag()
                            return
                        }
                        const local = row.mapFromItem(tab, lastX, 0)
                        strip.host.model.moveInGroup(tab.panelId, strip.indexForX(local.x, tab.panelId))
                    }
                    onTranslationChanged: {
                        if (!active) {
                            return
                        }
                        lastScene = centroid.scenePosition
                        lastX = centroid.position.x
                        if (!torn && Math.abs(translation.y) > 12) {
                            torn = true
                            strip.host.beginDrag(tab.panelId, centroid.scenePosition, false)
                        }
                        if (torn) {
                            strip.host.updateDrag(centroid.scenePosition)
                        }
                    }
                }
            }
        }
    }

    Tk.Menu {
        id: groupMenu
        property string panelId: ""
        Tk.MenuItem {
            text: qsTr("Take out of group")
            iconName: "ungroup"
            onTriggered: strip.host.model.ungroup(groupMenu.panelId)
        }
        Tk.MenuItem {
            text: qsTr("Float")
            iconName: "maximize-2"
            onTriggered: strip.host.model.floatPanel(groupMenu.panelId)
        }
        Tk.MenuSeparator { }
        Tk.MenuItem {
            text: qsTr("Close")
            iconName: "x"
            onTriggered: strip.host.model.hide(groupMenu.panelId)
        }
    }
}
