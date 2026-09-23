// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Model-driven contextual command ribbon. Group commands keep stable model IDs.
Item {
    id: ribbon
    property var tabs: []
    property var contextIds: []
    property string currentTabId: ""
    property Tk.CommandRegistry commandRegistry: null
    property Tk.ShortcutMap shortcutMap: null
    property bool collapsed: false
    property bool keyTipsVisible: false
    property string tooltip: qsTr("Ribbon")
    readonly property var visibleTabs: filteredTabs()
    readonly property var currentTab: tabForId(currentTabId)
    signal tabChanged(string tabId)
    signal itemRequested(string tabId, string groupId, string itemId)

    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight
    Accessible.role: Accessible.ToolBar
    Accessible.name: tooltip

    function tabForId(id) {
        for (let i = 0; i < tabs.length; ++i)
            if (String(tabs[i].id) === String(id)) return tabs[i]
        return null
    }
    function filteredTabs() {
        const result = []
        for (let i = 0; i < tabs.length; ++i) {
            const tab = tabs[i]
            if (!tab || tab.visible === false) continue
            if (tab.contextual === true && contextIds.indexOf(String(tab.contextId || "")) < 0) continue
            result.push(tab)
        }
        return result
    }
    function ensureCurrent() {
        if (!visibleTabs || visibleTabs.length === 0) { currentTabId = ""; return }
        if (!tabForId(currentTabId) || visibleTabs.indexOf(tabForId(currentTabId)) < 0)
            currentTabId = String(visibleTabs[0].id)
    }
    function activateTab(id) {
        const tab = tabForId(id)
        if (!tab || visibleTabs.indexOf(tab) < 0) return false
        currentTabId = String(id)
        tabChanged(currentTabId)
        return true
    }
    function activateItem(groupId, itemId) {
        const groups = currentTab && currentTab.groups ? currentTab.groups : []
        for (let g = 0; g < groups.length; ++g) {
            if (String(groups[g].id) !== String(groupId)) continue
            const items = groups[g].items || []
            for (let i = 0; i < items.length; ++i) {
                const item = items[i]
                if (String(item.id || item.commandId) !== String(itemId)) continue
                // AGENT-GUARD: key tips must use the same contextual command
                // authority as the pointer-activated CommandButton.
                if (item.commandId && commandRegistry
                    && !commandRegistry.trigger(String(item.commandId))) return false
                itemRequested(currentTabId, String(groupId), String(itemId))
                return true
            }
        }
        return false
    }
    function onKey(event) {
        if (event.key === Qt.Key_Alt) { keyTipsVisible = !keyTipsVisible; return true }
        if (event.key === Qt.Key_Escape && keyTipsVisible) { keyTipsVisible = false; return true }
        if (keyTipsVisible && event.text && event.text.length) {
            const key = event.text.toUpperCase()
            for (let i = 0; i < visibleTabs.length; ++i) {
                const tab = visibleTabs[i]
                const tip = String(tab.keyTip || tab.label || tab.id).charAt(0).toUpperCase()
                if (tip === key) { activateTab(String(tab.id)); keyTipsVisible = false; return true }
            }
            const groups = currentTab && currentTab.groups ? currentTab.groups : []
            for (let g = 0; g < groups.length; ++g) {
                const items = groups[g].items || []
                for (let i = 0; i < items.length; ++i) {
                    const item = items[i]
                    const tip = String(item.keyTip || item.label || item.id).charAt(0).toUpperCase()
                    if (tip === key) { activateItem(groups[g].id, item.id || item.commandId); keyTipsVisible = false; return true }
                }
            }
        }
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            if (!visibleTabs.length) return false
            let index = visibleTabs.indexOf(tabForId(currentTabId))
            index = (index + (event.key === Qt.Key_Right ? 1 : -1) + visibleTabs.length) % visibleTabs.length
            activateTab(String(visibleTabs[index].id))
            const button = tabRepeater.itemAt(index)
            if (button) button.forceActiveFocus(Qt.TabFocusReason)
            return true
        }
        return false
    }
    onTabsChanged: ensureCurrent()
    onContextIdsChanged: ensureCurrent()
    onVisibleTabsChanged: ensureCurrent()
    Keys.onPressed: function(event) { event.accepted = onKey(event) }

    Tk.Flex {
        id: content
        width: ribbon.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            id: tabRow
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Repeater {
                id: tabRepeater
                model: ribbon.visibleTabs
                delegate: T.TabButton {
                    id: tab
                    required property int index
                    required property var modelData
                    readonly property bool current: String(modelData.id) === ribbon.currentTabId
                    objectName: "ribbonTab_" + modelData.id
                    text: modelData.label || modelData.id
                    leftPadding: Tk.Theme.space.md
                    rightPadding: Tk.Theme.space.md
                    implicitWidth: tabLabel.implicitWidth + leftPadding + rightPadding
                    checkable: false
                    checked: current
                    focusPolicy: Qt.TabFocus
                    implicitHeight: Tk.Theme.size.tab
                    Accessible.role: Accessible.PageTab
                    Accessible.name: text
                    Accessible.selected: current
                    Accessible.description: modelData.contextual ? qsTr("Contextual tab") : ""
                    onClicked: ribbon.activateTab(String(modelData.id))
                    Keys.onPressed: function(event) { event.accepted = ribbon.onKey(event) }
                    background: Rectangle {
                        color: tab.current ? Tk.Theme.color.panelAlt : tab.hovered ? Tk.Theme.color.hover : "transparent"
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: Tk.Theme.size.focusRing; color: Tk.Theme.color.accent; visible: tab.current }
                    }
                    contentItem: Tk.Label {
                        id: tabLabel
                        text: modelData.label || modelData.id
                        horizontalAlignment: Text.AlignHCenter
                        color: tab.current ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                    }
                    Item {
                        visible: ribbon.keyTipsVisible
                        anchors.right: parent.right
                        anchors.top: parent.top
                        z: 2
                        width: keyTipLabel.implicitWidth + Tk.Theme.space.xs * 2
                        height: Tk.Theme.size.controlSm
                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.controlActiveBg; border.color: Tk.Theme.color.accent; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.xs }
                        Tk.Caption { id: keyTipLabel; anchors.centerIn: parent; text: String(modelData.keyTip || modelData.label || modelData.id).charAt(0).toUpperCase(); color: Tk.Theme.color.accentText }
                    }
                    Tk.ToolTip { text: tab.text; visible: tab.hovered }
                }
            }
            Tk.Spacer { }
            Tk.Button {
                objectName: "ribbonCollapseButton"
                small: true
                variant: "ghost"
                text: ribbon.collapsed ? qsTr("Expand") : qsTr("Collapse")
                tooltip: text
                Accessible.name: text
                onClicked: ribbon.collapsed = !ribbon.collapsed
            }
        }
        Tk.Scroll {
            id: groupScroll
            objectName: "ribbonGroupViewport"
            width: ribbon.width
            height: ribbon.collapsed ? 0 : groups.implicitHeight
            visible: !ribbon.collapsed && ribbon.currentTab !== null
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Hidden
            accessibleName: qsTr("Ribbon command groups")
            Tk.Flex {
                id: groups
                anchors.left: parent.left
                anchors.top: parent.top
                width: Math.max(groupScroll.viewportWidth, implicitWidth)
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Repeater {
                    model: ribbon.currentTab && ribbon.currentTab.groups ? ribbon.currentTab.groups : []
                    delegate: Tk.Flex {
                        id: groupDelegate
                        required property int index
                        required property var modelData
                        Tk.Flex.shrink: 0
                        objectName: "ribbonGroup_" + modelData.id
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Flex {
                            direction: Tk.Flex.Row
                            gap: Tk.Theme.space.xs
                            Repeater {
                                model: modelData.items || []
                                delegate: Item {
                                    required property int index
                                    required property var modelData
                                    readonly property string itemId: String(modelData.id || modelData.commandId || index)
                                    objectName: "ribbonItem_" + itemId
                                    implicitWidth: itemLoader.item ? itemLoader.item.implicitWidth : 0
                                    implicitHeight: itemLoader.item ? itemLoader.item.implicitHeight : Tk.Theme.size.control
                                    Loader {
                                        id: itemLoader
                                        anchors.fill: parent
                                        sourceComponent: modelData.commandId && ribbon.commandRegistry ? commandComponent : actionComponent
                                    }
                                    Item {
                                        visible: ribbon.keyTipsVisible
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        z: 2
                                        width: itemTip.implicitWidth + Tk.Theme.space.xs * 2
                                        height: Tk.Theme.size.controlSm
                                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.controlActiveBg; border.color: Tk.Theme.color.accent; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.xs }
                                        Tk.Caption { id: itemTip; anchors.centerIn: parent; text: String(modelData.keyTip || modelData.label || modelData.id).charAt(0).toUpperCase(); color: Tk.Theme.color.accentText }
                                    }
                                    Component {
                                        id: commandComponent
                                        Tk.CommandButton {
                                            registry: ribbon.commandRegistry
                                            shortcutMap: ribbon.shortcutMap
                                            commandId: modelData.commandId
                                            iconOnly: modelData.iconOnly === true
                                            small: modelData.size !== "large"
                                            tooltip: modelData.tooltip || modelData.label || ""
                                            Accessible.name: modelData.label || modelData.commandId
                                            onTriggered: ribbon.itemRequested(ribbon.currentTabId, String(groupDelegate.modelData.id), String(modelData.id || modelData.commandId))
                                        }
                                    }
                                    Component {
                                        id: actionComponent
                                        Tk.Button {
                                            objectName: "ribbonAction_" + itemId
                                            text: modelData.label || itemId
                                            iconName: modelData.iconName || ""
                                            small: modelData.size !== "large"
                                            tooltip: modelData.tooltip || text
                                            Accessible.name: modelData.label || itemId
                                            onClicked: ribbon.activateItem(groupDelegate.modelData.id, itemId)
                                            Keys.onPressed: function(event) { event.accepted = ribbon.onKey(event) }
                                        }
                                    }
                                }
                            }
                        }
                        Tk.Caption {
                            text: modelData.label || modelData.id
                            horizontalAlignment: Text.AlignHCenter
                            color: Tk.Theme.color.textMuted
                        }
                        Rectangle { height: Tk.Theme.size.border; width: parent.implicitWidth; color: Tk.Theme.color.divider }
                    }
                }
            }
        }
    }
}
