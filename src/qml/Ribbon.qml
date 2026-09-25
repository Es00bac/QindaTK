// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Model-driven contextual command ribbon (D-297). Tabs declare groups and
// items by stable ID; contextual tabs appear only for matching context IDs.
//
//   tabs: [{ id, label, keyTip, contextual, contextId, visible,
//            groups: [{ id, label, items: [item…] | rows: [[item…], …] }] }]
//   item: { id, label, iconName, tooltip, shortcut, keyTip, visible, enabled,
//           size: "large"|"small"|"icon", checkable, checked,
//           action (Action-shaped QObject, see RibbonSlot.qml) | commandId
//           (with commandRegistry) | component (a host Component),
//           menu: [item… | "|"], split }
//
// Layout: a row of pill tabs with the host's `trailing` item (a command
// search, say) and the collapse toggle at the right; below it the current
// tab's groups as soft cards. `mode: "compact"` draws one row of small
// buttons per group without captions.
//
// AGENT-CONTRACT (squeeze, QindaOffice AGENTS.md "Squeeze"): the ribbon
// never hides a command. As the width shrinks, `squeezeLevel` steps 0 → 1
// (large items become small) → 2 (items with an icon become icon-only) →
// 3 (the groups wrap onto further lines). Levels are planned from
// RibbonGroup.widthAt(), never from drawn geometry.
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
    property string mode: "expanded"
    property int stackRows: 2
    property bool autoSqueeze: true
    property Component itemDelegate: null
    property Item trailing: null
    readonly property var visibleTabs: filteredTabs()
    readonly property var currentTab: tabForId(currentTabId)
    readonly property bool compact: mode === "compact"
    readonly property real bandHeight: stackRows * Tk.Theme.size.control + (stackRows - 1) * Tk.Theme.space.xs
    readonly property int squeezeLevel: planLevel()
    signal tabChanged(string tabId)
    signal itemRequested(string tabId, string groupId, string itemId)
    signal collapseToggled(bool collapsed)

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
    function groupItems(group) {
        if (group && group.rows && group.rows.length) {
            let out = []
            for (let r = 0; r < group.rows.length; ++r) out = out.concat(group.rows[r])
            return out
        }
        return group && group.items ? group.items : []
    }
    function findItem(groupId, itemId) {
        const groups = currentTab && currentTab.groups ? currentTab.groups : []
        for (let g = 0; g < groups.length; ++g) {
            if (String(groups[g].id) !== String(groupId)) continue
            const items = groupItems(groups[g])
            for (let i = 0; i < items.length; ++i)
                if (items[i] && String(items[i].id || items[i].commandId) === String(itemId)) return items[i]
        }
        return null
    }
    // Pointer, keyboard and key-tip activation all land here.
    function activateItem(groupId, itemId) {
        const item = findItem(groupId, itemId)
        if (!item || item.enabled === false) return false
        // AGENT-GUARD: key tips must use the same contextual command
        // authority as the pointer-activated CommandButton.
        if (item.commandId && commandRegistry && !commandRegistry.trigger(String(item.commandId))) return false
        if (item.action) {
            if (item.action.enabled === false) return false
            item.action.trigger()
        }
        itemRequested(currentTabId, String(groupId), String(itemId))
        return true
    }
    function keyTipFor(item) {
        return String(item.keyTip || item.label || item.id || "").charAt(0).toUpperCase()
    }
    function openItemMenu(item, anchor) {
        itemMenu.entries = item.menu || []
        itemMenu.parent = anchor
        itemMenu.y = anchor.height
        itemMenu.x = 0
        itemMenu.open()
    }
    function planLevel() {
        if (!autoSqueeze || compact) return 0
        const available = groupScroll.width - Tk.Theme.space.sm * 2
        const count = groupRepeater.count
        if (count === 0 || available <= 0) return 0
        for (let level = 0; level <= 2; ++level) {
            let total = (count - 1) * Tk.Theme.space.sm
            for (let g = 0; g < count; ++g) {
                const group = groupRepeater.itemAt(g) as Tk.RibbonGroup
                if (group !== null) total += group.widthAt(level)
            }
            if (total <= available) return level
        }
        return 3
    }
    function onKey(event) {
        if (event.key === Qt.Key_Alt) { keyTipsVisible = !keyTipsVisible; return true }
        if (event.key === Qt.Key_Escape && keyTipsVisible) { keyTipsVisible = false; return true }
        if (keyTipsVisible && event.text && event.text.length) {
            const key = event.text.toUpperCase()
            for (let i = 0; i < visibleTabs.length; ++i) {
                const tab = visibleTabs[i]
                if (keyTipFor(tab) === key) { activateTab(String(tab.id)); keyTipsVisible = false; return true }
            }
            const groups = currentTab && currentTab.groups ? currentTab.groups : []
            for (let g = 0; g < groups.length; ++g) {
                const items = groupItems(groups[g])
                for (let i = 0; i < items.length; ++i) {
                    const item = items[i]
                    if (item && !item.component && keyTipFor(item) === key) {
                        activateItem(groups[g].id, item.id || item.commandId)
                        keyTipsVisible = false
                        return true
                    }
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
    onTrailingChanged: if (trailing !== null) trailing.parent = trailingHost
    Keys.onPressed: function(event) { event.accepted = onKey(event) }

    Rectangle {
        objectName: "ribbonBackground"
        anchors.fill: parent
        color: Tk.Theme.color.panel
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: Tk.Theme.size.border; color: Tk.Theme.color.divider }
    }

    Tk.Flex {
        id: content
        width: ribbon.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        paddingTop: Tk.Theme.space.xs
        paddingBottom: ribbon.collapsed ? Tk.Theme.space.xs : Tk.Theme.space.sm
        Tk.Flex {
            id: tabRow
            objectName: "ribbonTabRow"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            paddingLeft: Tk.Theme.space.sm
            paddingRight: Tk.Theme.space.sm
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
                    implicitHeight: Tk.Theme.size.control
                    checkable: false
                    checked: current
                    focusPolicy: Qt.TabFocus
                    hoverEnabled: true
                    Tk.Flex.shrink: 0
                    Accessible.role: Accessible.PageTab
                    Accessible.name: text
                    Accessible.selected: current
                    Accessible.description: modelData.contextual ? qsTr("Contextual tab") : ""
                    onClicked: {
                        if (tab.current && !ribbon.collapsed) return
                        if (ribbon.collapsed) { ribbon.collapsed = false; ribbon.collapseToggled(false) }
                        ribbon.activateTab(String(modelData.id))
                    }
                    onDoubleClicked: { ribbon.collapsed = !ribbon.collapsed; ribbon.collapseToggled(ribbon.collapsed) }
                    Keys.onPressed: function(event) { event.accepted = ribbon.onKey(event) }
                    background: Rectangle {
                        radius: Tk.Theme.radius.md
                        color: tab.current && !ribbon.collapsed ? Tk.Theme.color.controlActiveBg
                             : tab.hovered ? Tk.Theme.color.hover : Tk.Theme.alpha(Tk.Theme.color.hover, Tk.Theme.opacity.hidden)
                        border.width: tab.visualFocus ? Tk.Theme.size.focusRing : 0
                        border.color: Tk.Theme.color.focus
                        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
                    }
                    contentItem: Tk.Label {
                        id: tabLabel
                        text: tab.text
                        horizontalAlignment: Text.AlignHCenter
                        font.weight: tab.current ? Font.DemiBold : Font.Normal
                        color: tab.modelData.contextual ? Tk.Theme.color.accentText
                             : tab.current ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                    }
                    Item {
                        visible: ribbon.keyTipsVisible
                        anchors.right: parent.right
                        anchors.top: parent.top
                        z: 2
                        width: keyTipLabel.implicitWidth + Tk.Theme.space.xs * 2
                        height: Tk.Theme.size.controlSm
                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.controlActiveBg; border.color: Tk.Theme.color.accent; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.xs }
                        Tk.Caption { id: keyTipLabel; anchors.centerIn: parent; text: ribbon.keyTipFor(tab.modelData); color: Tk.Theme.color.accentText }
                    }
                    Tk.ToolTip { text: tab.text; visible: tab.hovered && ribbon.collapsed }
                }
            }
            Tk.Spacer { }
            Item {
                id: trailingHost
                objectName: "ribbonTrailing"
                Tk.Flex.shrink: 1
                implicitWidth: ribbon.trailing ? ribbon.trailing.implicitWidth : 0
                implicitHeight: ribbon.trailing ? ribbon.trailing.implicitHeight : 0
                visible: ribbon.trailing !== null
            }
            Tk.IconButton {
                objectName: "ribbonCollapseButton"
                iconName: ribbon.collapsed ? "chevron-down" : "chevron-up"
                tooltip: ribbon.collapsed ? qsTr("Show the ribbon") : qsTr("Hide the ribbon")
                onClicked: { ribbon.collapsed = !ribbon.collapsed; ribbon.collapseToggled(ribbon.collapsed) }
            }
        }
        Item {
            id: groupScroll
            objectName: "ribbonGroupViewport"
            width: ribbon.width
            implicitHeight: groups.implicitHeight
            height: ribbon.collapsed ? 0 : implicitHeight
            visible: !ribbon.collapsed && ribbon.currentTab !== null
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Ribbon command groups")
            Tk.Flex {
                id: groups
                objectName: "ribbonGroups"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.sm
                paddingLeft: Tk.Theme.space.sm
                paddingRight: Tk.Theme.space.sm
                Repeater {
                    id: groupRepeater
                    model: ribbon.currentTab && ribbon.currentTab.groups ? ribbon.currentTab.groups : []
                    delegate: Tk.RibbonGroup {
                        required property var modelData
                        groupData: modelData
                        ribbon: ribbon
                        level: Math.min(2, ribbon.squeezeLevel)
                        compact: ribbon.compact
                    }
                }
            }
        }
    }

    Tk.Menu {
        id: itemMenu
        objectName: "ribbonItemMenu"
        property var entries: []
        Instantiator {
            model: itemMenu.entries.filter(function(entry) { return entry !== "|" })
            delegate: Tk.MenuItem {
                required property var modelData
                readonly property var boundAction: modelData && modelData.action ? modelData.action : null
                objectName: "ribbonMenuItem_" + String(modelData.id || "")
                text: String(modelData.label || (boundAction ? boundAction.text : "") || modelData.id || "")
                iconName: String(modelData.iconName || (boundAction && boundAction.iconName ? boundAction.iconName : ""))
                shortcut: boundAction && boundAction.shortcutText ? boundAction.shortcutText : ""
                enabled: modelData.enabled !== false && (boundAction === null || boundAction.enabled)
                checkable: boundAction !== null && boundAction.checkable === true
                checked: boundAction !== null && boundAction.checked === true
                onTriggered: {
                    if (boundAction !== null) boundAction.trigger()
                    ribbon.itemRequested(ribbon.currentTabId, "", String(modelData.id || ""))
                }
            }
            onObjectAdded: function(index, object) { itemMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { itemMenu.removeItem(object) }
        }
    }
}
