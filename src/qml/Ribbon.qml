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
// RibbonGroup.widthAt(), never from drawn geometry, and PER GROUP: groups
// step up one at a time from the right, just enough to fit, so a tab a few
// pixels too wide loses the labels of its last group, not of every group
// (D-305). `squeezeLevel` is the deepest level any group uses.
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
    readonly property var squeezePlan: planSqueeze()
    readonly property int squeezeLevel: squeezePlan.level
    // D-317: the tab to return to when the current contextual tab goes away —
    // the one that was current before a contextual tab became current.
    property string _returnTabId: ""
    property string _lastTabId: ""
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
    // AGENT-GUARD (D-317): tabs are matched by ID on a freshly filtered
    // list. From onTabsChanged `visibleTabs` can still hold the previous
    // array's objects, so an object lookup sent a host that rebuilt `tabs`
    // (new objects, same IDs) back to the first tab.
    function isVisibleTab(id) {
        const visible = filteredTabs()
        for (let i = 0; i < visible.length; ++i) if (String(visible[i].id) === String(id)) return true
        return false
    }
    function ensureCurrent() {
        const visible = filteredTabs()
        if (visible.length === 0) { currentTabId = ""; return }
        if (isVisibleTab(currentTabId)) return
        currentTabId = isVisibleTab(_returnTabId) ? _returnTabId : String(visible[0].id)
    }
    function activateTab(id) {
        if (!isVisibleTab(id)) return false
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
    function planSqueeze() {
        const count = groupRepeater.count
        const levels = []
        for (let g = 0; g < count; ++g) levels.push(0)
        if (!autoSqueeze || compact) return { level: 0, levels: levels }
        const available = groupScroll.width - Tk.Theme.space.sm * 2
        if (count === 0 || available <= 0) return { level: 0, levels: levels }
        const widths = []
        for (let g = 0; g < count; ++g) {
            const group = groupRepeater.itemAt(g) as Tk.RibbonGroup
            widths.push(group !== null ? [group.widthAt(0), group.widthAt(1), group.widthAt(2)] : [0, 0, 0])
        }
        let total = (count - 1) * Tk.Theme.space.sm
        for (let g = 0; g < count; ++g) total += widths[g][0]
        // AGENT-GUARD: step groups up one level at a time from the RIGHT
        // (the first groups hold the most-used commands), finishing level 1
        // everywhere before any group goes icon-only. Stepping every group
        // together turned "12 px too wide" into an icon-only tab with a
        // third of the band empty.
        for (let target = 1; target <= 2; ++target) {
            for (let g = count - 1; g >= 0; --g) {
                if (total <= available) return { level: Math.max.apply(null, levels), levels: levels }
                total += widths[g][target] - widths[g][levels[g]]
                levels[g] = target
            }
        }
        if (total <= available) return { level: Math.max.apply(null, levels), levels: levels }
        return { level: 3, levels: levels }
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
            let index = visibleTabs.findIndex(tab => String(tab.id) === currentTabId)
            index = (index + (event.key === Qt.Key_Right ? 1 : -1) + visibleTabs.length) % visibleTabs.length
            activateTab(String(visibleTabs[index].id))
            const button = tabRepeater.itemAt(index)
            if (button) button.forceActiveFocus(Qt.TabFocusReason)
            return true
        }
        return false
    }
    onCurrentTabIdChanged: {
        const now = tabForId(currentTabId)
        const before = tabForId(_lastTabId)
        if (now && now.contextual === true) {
            if (before && before.contextual !== true) _returnTabId = _lastTabId
        } else {
            _returnTabId = ""
        }
        _lastTabId = currentTabId
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
                        required property int index
                        groupData: modelData
                        ribbon: ribbon
                        level: ribbon.squeezePlan.levels[index] ?? Math.min(2, ribbon.squeezeLevel)
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
