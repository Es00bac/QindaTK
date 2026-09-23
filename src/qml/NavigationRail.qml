// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Persistent application navigation. Route state remains owned by the host.
Item {
    id: rail
    property var groups: []
    property string currentId: ""
    property bool expanded: true
    property real expandedWidth: Tk.Theme.size.panelMinWidth
    property real compactWidth: Tk.Theme.size.controlLg + Tk.Theme.space.md
    property string tooltip: qsTr("Application navigation")
    property bool _ready: false
    readonly property var entries: flattenedEntries()
    signal destinationRequested(string id, string destination)
    signal routeReconciled(string previousId, string currentId)

    implicitWidth: expanded ? expandedWidth : compactWidth
    implicitHeight: body.implicitHeight
    Accessible.role: Accessible.List
    Accessible.name: tooltip

    function flattenedEntries() {
        const result = []
        for (let i = 0; i < groups.length; ++i) {
            const group = groups[i]
            if (!group || group.visible === false) continue
            result.push({ kind: "group", id: String(group.id), label: String(group.label || ""),
                          iconName: "", badge: "", enabled: false })
            const items = group.entries || []
            for (let j = 0; j < items.length; ++j) {
                const entry = items[j]
                if (!entry || entry.visible === false || entry.allowed === false) continue
                result.push({ kind: "entry", groupId: String(group.id), id: String(entry.id),
                              label: String(entry.label || entry.id), destination: String(entry.destination || entry.id),
                              iconName: String(entry.iconName || ""), badge: String(entry.badge || ""), enabled: entry.enabled !== false })
            }
        }
        return result
    }
    function navigableIndexes() {
        const result = []
        for (let i = 0; i < entries.length; ++i)
            if (entries[i].kind === "entry" && entries[i].enabled) result.push(i)
        return result
    }
    function moveFocus(from, step, edge) {
        const indexes = navigableIndexes()
        if (!indexes.length) return false
        let position = indexes.indexOf(from)
        if (edge === "first") position = 0
        else if (edge === "last") position = indexes.length - 1
        else position = (Math.max(0, position) + step + indexes.length) % indexes.length
        const button = repeater.itemAt(indexes[position])
        if (!button) return false
        button.forceActiveFocus(Qt.TabFocusReason)
        return true
    }
    function activate(entry) {
        if (!entry || entry.kind !== "entry" || !entry.enabled) return false
        const current = entries.find(function(candidate) {
            return candidate.kind === "entry" && candidate.enabled
                && candidate.id === String(entry.id)
        })
        if (!current) return false
        // AGENT-CONTRACT: destinationRequested is an intent. The host confirms
        // its authoritative route with confirmRoute; a refused route must not
        // change the selected destination.
        destinationRequested(current.id, current.destination)
        return true
    }
    function validRoute(id) {
        return entries.some(function(entry) {
            return entry.kind === "entry" && entry.enabled && entry.id === String(id)
        })
    }
    function reconcileCurrent() {
        if (validRoute(currentId)) return true
        const previous = currentId
        const first = entries.find(function(entry) {
            return entry.kind === "entry" && entry.enabled
        })
        const fallback = first ? first.id : ""
        if (previous !== fallback) {
            currentId = fallback
            routeReconciled(previous, fallback)
        }
        return false
    }
    function confirmRoute(id) {
        if (!validRoute(id)) {
            reconcileCurrent()
            return false
        }
        currentId = String(id)
        return true
    }
    onEntriesChanged: if (_ready) reconcileCurrent()
    onCurrentIdChanged: if (_ready) reconcileCurrent()
    Component.onCompleted: { _ready = true; reconcileCurrent() }

    Tk.Scroll {
        id: viewport
        width: rail.width
        height: rail.height
        overflowY: Tk.Scroll.Auto
        overflowX: Tk.Scroll.Hidden
        accessibleName: rail.tooltip
        Tk.Flex {
            id: body
            width: rail.width
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            paddingTop: Tk.Theme.space.xs
            paddingBottom: Tk.Theme.space.xs
            Repeater {
                id: repeater
                model: rail.entries
                delegate: Item {
                    id: slot
                    required property int index
                    required property var modelData
                    objectName: modelData.kind === "group" ? "navigationGroup_" + modelData.id : "navigationItem_" + modelData.id
                    implicitWidth: rail.width
                    implicitHeight: modelData.kind === "group" ? (rail.expanded ? Tk.Theme.size.row : Tk.Theme.space.xs)
                                                                   : Tk.Theme.size.control
                    visible: modelData.kind !== "group" || rail.expanded
                    Tk.Caption {
                        anchors.fill: parent
                        visible: modelData.kind === "group" && rail.expanded
                        leftPadding: Tk.Theme.space.md
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.label
                        color: Tk.Theme.color.textMuted
                        Accessible.role: Accessible.Heading
                    }
                    T.Button {
                        id: itemButton
                        visible: modelData.kind === "entry"
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.xs
                        anchors.rightMargin: Tk.Theme.space.xs
                        text: rail.expanded ? slot.modelData.label : ""
                        enabled: slot.modelData.enabled
                        checked: slot.modelData.id === rail.currentId
                        checkable: false
                        focusPolicy: Qt.TabFocus
                        objectName: "navigationButton_" + slot.modelData.id
                        Accessible.role: Accessible.PageTab
                        Accessible.name: slot.modelData.label
                        Accessible.description: slot.modelData.badge.length ? qsTr("%1 notifications").arg(slot.modelData.badge) : ""
                        Accessible.selected: checked
                        Accessible.focusable: enabled
                        Accessible.onPressAction: rail.activate(slot.modelData)
                        onClicked: rail.activate(slot.modelData)
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) event.accepted = rail.moveFocus(index, 1, "")
                            else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) event.accepted = rail.moveFocus(index, -1, "")
                            else if (event.key === Qt.Key_Home) event.accepted = rail.moveFocus(index, 1, "first")
                            else if (event.key === Qt.Key_End) event.accepted = rail.moveFocus(index, -1, "last")
                        }
                        contentItem: Tk.Flex {
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            gap: Tk.Theme.space.sm
                            Tk.Icon { name: slot.modelData.iconName; size: Tk.Theme.size.icon; color: itemButton.checked ? Tk.Theme.color.accent : Tk.Theme.color.textMuted }
                            Tk.Label {
                                visible: rail.expanded
                                text: slot.modelData.label
                                color: itemButton.checked ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                                elide: Text.ElideRight
                                Tk.Flex.grow: 1
                            }
                            Tk.Caption {
                                visible: rail.expanded && slot.modelData.badge.length > 0
                                text: slot.modelData.badge
                                color: Tk.Theme.color.accentText
                            }
                        }
                        Tk.ToolTip { text: slot.modelData.label; visible: !rail.expanded && itemButton.hovered }
                        background: Rectangle {
                            color: itemButton.checked ? Tk.Theme.color.controlActiveBg : itemButton.hovered ? Tk.Theme.color.hover : Tk.Theme.alpha(Tk.Theme.color.surface, Tk.Theme.opacity.hidden)
                            radius: Tk.Theme.radius.sm
                            Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: Tk.Theme.size.focusRing; color: Tk.Theme.color.accent; visible: itemButton.checked }
                            Rectangle { anchors.fill: parent; anchors.margins: -Tk.Theme.size.focusRing; color: Tk.Theme.alpha(Tk.Theme.color.surface, Tk.Theme.opacity.hidden); border.width: Tk.Theme.size.focusRing; border.color: Tk.Theme.color.focus; visible: itemButton.activeFocus }
                        }
                    }
                }
            }
        }
    }
}
