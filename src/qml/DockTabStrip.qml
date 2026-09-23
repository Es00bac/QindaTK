// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
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

    signal tabActivated(string panelId)

    implicitHeight: Tk.Theme.size.tab
    implicitWidth: row.implicitWidth
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Dock panels")
    Accessible.focusable: false

    function activate(panelId) {
        if (!strip.enabled || panelId === strip.activeId
                || strip.panelIds.indexOf(panelId) < 0) {
            return
        }
        strip.host.model.activateInGroup(panelId)
        strip.tabActivated(panelId)
    }

    function moveFocus(fromIndex, direction) {
        const count = tabs.count
        if (!strip.enabled || count < 1) {
            return
        }
        for (let offset = 1; offset <= count; ++offset) {
            const index = (fromIndex + direction * offset + count) % count
            const tab = tabs.itemAt(index)
            if (tab !== null && tab.enabled) {
                tab.forceActiveFocus(Qt.TabFocusReason)
                strip.activate(tab.panelId)
                return
            }
        }
    }

    function focusIndex(index) {
        const tab = tabs.itemAt(index)
        if (strip.enabled && tab !== null && tab.enabled) {
            tab.forceActiveFocus(Qt.TabFocusReason)
            strip.activate(tab.panelId)
        }
    }

    function tabItem(index) {
        return tabs.itemAt(index)
    }

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

            delegate: T.TabButton {
                id: tab
                required property string modelData
                required property int index
                readonly property string panelId: modelData
                readonly property bool active: panelId === strip.activeId
                objectName: "dockTab_" + panelId
                implicitWidth: label.implicitWidth + Tk.Theme.space.sm * 2
                implicitHeight: Tk.Theme.size.tab
                leftPadding: Tk.Theme.space.sm
                rightPadding: Tk.Theme.space.sm
                Tk.Flex.shrink: 1
                Tk.Flex.minWidth: Tk.Theme.space.xl
                text: strip.host.panelTitle(panelId)
                checkable: false
                checked: active
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: text
                Accessible.description: active ? qsTr("Active panel") : qsTr("Panel tab")
                Accessible.selected: active
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                Accessible.onPressAction: strip.activate(panelId)

                Tk.ToolTip {
                    text: tab.text
                    visible: tab.hovered
                }

                HoverHandler { id: tabHover }
                onClicked: strip.activate(panelId)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Left) {
                        strip.moveFocus(index, -1)
                    } else if (event.key === Qt.Key_Right) {
                        strip.moveFocus(index, 1)
                    } else if (event.key === Qt.Key_Home) {
                        strip.focusIndex(0)
                    } else if (event.key === Qt.Key_End) {
                        strip.focusIndex(tabs.count - 1)
                    } else {
                        return
                    }
                    event.accepted = true
                }

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        color: tab.active ? Tk.Theme.color.panel
                             : tabHover.hovered ? Tk.Theme.color.hover : "transparent"
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Tk.Theme.color.accent
                        visible: tab.active
                    }
                    Rectangle {
                        objectName: "dockTabFocus_" + tab.panelId
                        anchors.fill: parent
                        anchors.margins: -Tk.Theme.size.focusRing
                        color: "transparent"
                        border.width: Tk.Theme.size.focusRing
                        border.color: Tk.Theme.color.focus
                        visible: tab.activeFocus && tab.enabled
                    }
                }

                contentItem: Tk.Caption {
                    id: label
                    text: tab.text
                    muted: !tab.active
                    color: tab.active ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                    font.weight: tab.active ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
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
