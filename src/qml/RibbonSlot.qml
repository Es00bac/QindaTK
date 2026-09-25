// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One item of a Tk.RibbonGroup: chooses the presentation for the item's
// data and exposes the weight-independent width the squeeze plans with.
//   { component: Component }      — a host widget (a combo, a colour button);
//   ribbon.itemDelegate set        — the host draws every non-component item;
//   { commandId } + registry       — a Tk.CommandButton bound to the registry;
//   otherwise                      — a Tk.RibbonButton, bound live to
//                                    `item.action` when one is given.
//
// AGENT-CONTRACT (`item.action`): any QObject with `text` (or `plainText`),
// `iconName`, `enabled`, `visible`, `checkable`, `checked`, optional
// `shortcutText`/`tooltip`, and a `trigger()` method — QtQuick's Action
// shape, and QindaOffice's ActionEntry. The ribbon never flips `checked`
// itself; the action is the truth and the button re-binds after a click.
Item {
    id: slot

    property Tk.Ribbon ribbon: null
    property string groupId: ""
    property var itemData: ({})
    property string weight: "small"
    readonly property var action: itemData && itemData.action ? itemData.action : null
    readonly property string itemId: String(itemData.id || itemData.commandId || "")
    readonly property string label: String(itemData.label || (action ? (action.plainText !== undefined ? action.plainText : action.text) : "") || itemId)
    // D-013 (QindaOffice) / D-297: an unknown icon never becomes a blank square.
    readonly property string iconName: {
        const name = String(itemData.iconName || (action && action.iconName ? action.iconName : ""))
        return name.length > 0 && Tk.Icons.has(name) ? name : ""
    }
    readonly property bool itemEnabled: itemData.enabled !== false && (action === null || action.enabled)
    readonly property Item content: loader.item as Item

    objectName: "ribbonItem_" + itemId
    visible: action === null || action.visible !== false
    implicitWidth: content ? content.implicitWidth : 0
    implicitHeight: content ? content.implicitHeight : Tk.Theme.size.control
    height: implicitHeight

    function widthFor(w) {
        if (content === null) return 0
        const button = content as Tk.RibbonButton
        if (button !== null) return w === "large" ? button.largeWidth : w === "icon" ? button.iconWidth : button.smallWidth
        return content.implicitWidth
    }

    Loader {
        id: loader
        width: slot.width
        sourceComponent: slot.itemData.component ? slot.itemData.component
                       : slot.ribbon && slot.ribbon.itemDelegate ? slot.ribbon.itemDelegate
                       : slot.itemData.commandId && slot.ribbon && slot.ribbon.commandRegistry ? commandComponent
                       : buttonComponent
        onLoaded: {
            // A host delegate learns what it draws through these properties.
            // (Indexed access: these belong to the host's delegate type.)
            const host = loader.item
            if (host["ribbonItem"] !== undefined) host["ribbonItem"] = Qt.binding(function() { return slot.itemData })
            if (host["ribbonWeight"] !== undefined) host["ribbonWeight"] = Qt.binding(function() { return slot.weight })
            if (host["ribbonGroupId"] !== undefined) host["ribbonGroupId"] = slot.groupId
        }
    }

    Component {
        id: buttonComponent
        Tk.RibbonButton {
            id: button
            objectName: "ribbonAction_" + slot.itemId
            size: slot.weight
            text: slot.label
            iconName: slot.iconName
            tooltip: String(slot.itemData.tooltip || (slot.action && slot.action.tooltip ? slot.action.tooltip : "") || slot.label)
            shortcutText: String(slot.itemData.shortcut || (slot.action && slot.action.shortcutText ? slot.action.shortcutText : ""))
            enabled: slot.itemEnabled
            checkable: slot.itemData.checkable === true || (slot.action !== null && slot.action.checkable === true)
            checked: slot.itemData.checked === true || (slot.action !== null && slot.action.checked === true)
            hasMenu: !!slot.itemData.menu
            split: slot.itemData.split === true
            bandHeight: slot.ribbon ? slot.ribbon.bandHeight : bandHeight
            onClicked: {
                if (hasMenu && !split) return
                slot.ribbon.activateItem(slot.groupId, slot.itemId)
                button.checked = Qt.binding(function() {
                    return slot.itemData.checked === true || (slot.action !== null && slot.action.checked === true)
                })
            }
            onMenuRequested: slot.ribbon.openItemMenu(slot.itemData, button)
            Keys.onPressed: function(event) { if (!event.accepted) event.accepted = slot.ribbon.onKey(event) }
        }
    }
    Component {
        id: commandComponent
        Tk.CommandButton {
            registry: slot.ribbon.commandRegistry
            shortcutMap: slot.ribbon.shortcutMap
            commandId: slot.itemData.commandId
            iconOnly: slot.weight === "icon"
            small: slot.weight !== "large"
            tooltip: slot.itemData.tooltip || slot.itemData.label || ""
            Accessible.name: slot.itemData.label || slot.itemData.commandId
            onTriggered: slot.ribbon.itemRequested(slot.ribbon.currentTabId, slot.groupId, slot.itemId)
        }
    }

    Item {
        visible: slot.ribbon !== null && slot.ribbon.keyTipsVisible && slot.ribbon.keyTipFor(slot.itemData).length > 0
        anchors.right: parent.right
        anchors.top: parent.top
        z: 2
        width: keyTip.implicitWidth + Tk.Theme.space.xs * 2
        height: Tk.Theme.size.controlSm
        Rectangle {
            anchors.fill: parent
            color: Tk.Theme.color.controlActiveBg
            border.color: Tk.Theme.color.accent
            border.width: Tk.Theme.size.border
            radius: Tk.Theme.radius.xs
        }
        Tk.Caption {
            id: keyTip
            anchors.centerIn: parent
            text: slot.ribbon ? slot.ribbon.keyTipFor(slot.itemData) : ""
            color: Tk.Theme.color.accentText
        }
    }
}
