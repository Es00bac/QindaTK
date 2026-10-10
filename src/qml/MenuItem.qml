// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// One 22px menu entry: optional icon (or a check mark when checkable and
// checked, a dot when `radio`), small-font label, a muted caption
// `shortcut` on the right, a chevron for a submenu. `danger` paints the
// label in the danger role.
T.MenuItem {
    id: item

    property string shortcut: ""
    property string iconName: ""
    property string tooltip: ""
    property Tk.CommandRegistry commandRegistry: null
    property Tk.ShortcutMap shortcutMap: null
    property string commandId: ""
    readonly property bool commandBound: commandRegistry !== null && commandId.length > 0
    property Tk.CommandBinding commandBinding: Tk.CommandBinding {
        registry: item.commandRegistry
        shortcutMap: item.shortcutMap
        commandId: item.commandId
    }
    property bool danger: false
    // A member of an exclusive group: a dot instead of a check when checked.
    // Exclusivity itself is the owner's (an action model, a ButtonGroup).
    property bool radio: false
    readonly property string accessibleText: text.replace(/&(?!&)/g, "").replace(/&&/g, "&")

    Accessible.role: Accessible.MenuItem
    Accessible.name: accessibleText
    Accessible.description: item.commandBound ? item.commandBinding.reason : item.tooltip
    Accessible.focusable: item.enabled
    Accessible.checked: item.checked
    Accessible.onPressAction: if (item.enabled) item.click()

    Tk.ToolTip {
        text: item.commandBound && item.commandBinding.reason.length > 0
              ? item.commandBinding.reason : item.tooltip
        visible: text.length > 0 && item.hovered
        delay: Tk.Theme.motion.tooltipDelay
    }

    function syncCommand() {
        if (!item.commandBound)
            return
        item.text = item.commandBinding.label
        item.iconName = item.commandBinding.iconName
        item.shortcut = item.commandBinding.shortcut
        item.enabled = item.commandBinding.enabled
        item.checkable = item.commandBinding.checkable
        item.checked = item.commandBinding.checked
    }
    onCommandBindingChanged: syncCommand()
    Component.onCompleted: syncCommand()
    Connections {
        target: item.commandBinding
        function onChanged() { item.syncCommand() }
    }
    onTriggered: if (item.commandBound) item.commandBinding.trigger()

    // AGENT-NOTE: Flex gives the label basis zero so its measured width does
    // not contribute to the content row's implicit width. Measure the menu
    // entry from the children's natural widths for Menu's popup sizing.
    readonly property real _naturalContentWidth: menuItemText.implicitWidth
        + (iconSlot.visible ? iconSlot.implicitWidth + item.spacing : 0)
        + (shortcutText.visible ? shortcutText.implicitWidth + item.spacing : 0)
        + (submenuArrow.visible ? submenuArrow.implicitWidth + item.spacing : 0)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            _naturalContentWidth + leftPadding + rightPadding)
    implicitHeight: Tk.Theme.size.menuItem
    leftPadding: Tk.Theme.space.sm
    rightPadding: Tk.Theme.space.sm
    topPadding: 0
    bottomPadding: 0
    spacing: Tk.Theme.space.sm
    hoverEnabled: true

    // Templates place `indicator` and `arrow` themselves; both live in the
    // content row here so their widths take part in the layout.
    indicator: Item { }
    arrow: Item { }

    contentItem: Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: item.spacing

        Item {
            id: iconSlot
            // AGENT-NOTE (D-323): a checkable item with an icon keeps the
            // icon and shows its checked state as a frame around it (as
            // office suites do); without an icon the slot is the check mark
            // (a dot for `radio`).
            readonly property bool framed: item.checkable && item.iconName.length > 0
            implicitWidth: Tk.Theme.size.icon
            implicitHeight: Tk.Theme.size.icon
            visible: item.iconName.length > 0 || item.checkable
            Tk.Flex.shrink: 0
            Rectangle {
                objectName: "menuItemCheckFrame"
                visible: iconSlot.framed && item.checked
                anchors.centerIn: parent
                width: Tk.Theme.size.icon + Tk.Theme.space.xs * 2
                height: width
                radius: Tk.Theme.radius.xs
                color: Tk.Theme.color.accentSubtle
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.accent
            }
            Tk.Icon {
                objectName: "menuItemIcon"
                anchors.centerIn: parent
                name: item.checkable && !iconSlot.framed ? (item.radio ? "circle-dot" : "check") : item.iconName
                size: Tk.Theme.size.icon
                visible: !item.checkable || iconSlot.framed || item.checked
                color: item.danger ? Tk.Theme.color.danger
                     : item.checkable && (item.checked || !iconSlot.framed) ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
            }
        }
        Tk.Label {
            id: menuItemText
            objectName: "menuItemText"
            // '&' marks the mnemonic for the keyboard; '&&' is a literal.
            text: item.accessibleText
            font.pixelSize: Tk.Theme.font.small
            color: !item.enabled ? Tk.Theme.color.textDisabled
                 : item.danger ? Tk.Theme.color.danger : Tk.Theme.color.text
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
        Tk.Caption {
            id: shortcutText
            visible: item.shortcut.length > 0
            text: item.shortcut
            Tk.Flex.shrink: 0
            // The gap the original keeps between label and shortcut.
            leftPadding: Tk.Theme.space.xl - item.spacing
        }
        Tk.Icon {
            id: submenuArrow
            visible: item.subMenu !== null
            name: "chevron-right"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
            Tk.Flex.shrink: 0
        }
    }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.menuItemMinWidth
        implicitHeight: Tk.Theme.size.menuItem
        radius: Tk.Theme.radius.xs
        color: item.highlighted || item.down ? Tk.Theme.color.hover
              : Tk.Theme.alpha(Tk.Theme.color.hover, Tk.Theme.opacity.hidden)
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
    }
}
