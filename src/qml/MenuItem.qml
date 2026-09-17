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
    property bool danger: false
    // A member of an exclusive group: a dot instead of a check when checked.
    // Exclusivity itself is the owner's (an action model, a ButtonGroup).
    property bool radio: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
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
            implicitWidth: Tk.Theme.size.icon
            implicitHeight: Tk.Theme.size.icon
            visible: item.iconName.length > 0 || item.checkable
            Tk.Flex.shrink: 0
            Tk.Icon {
                anchors.centerIn: parent
                name: item.checkable ? (item.radio ? "circle-dot" : "check") : item.iconName
                size: Tk.Theme.size.icon
                visible: !item.checkable || item.checked
                color: item.danger ? Tk.Theme.color.danger
                     : item.checkable ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
            }
        }
        Tk.Label {
            objectName: "menuItemText"
            // '&' marks the mnemonic for the keyboard; '&&' is a literal.
            text: item.text.replace(/&(?!&)/g, "").replace(/&&/g, "&")
            font.pixelSize: Tk.Theme.font.small
            color: !item.enabled ? Tk.Theme.color.textDisabled
                 : item.danger ? Tk.Theme.color.danger : Tk.Theme.color.text
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
        Tk.Caption {
            visible: item.shortcut.length > 0
            text: item.shortcut
            Tk.Flex.shrink: 0
            // The gap the original keeps between label and shortcut.
            leftPadding: Tk.Theme.space.xl - item.spacing
        }
        Tk.Icon {
            visible: item.subMenu !== null
            name: "chevron-right"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
            Tk.Flex.shrink: 0
        }
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Tk.Theme.size.menuItem
        radius: Tk.Theme.radius.xs
        color: item.highlighted || item.down ? Tk.Theme.color.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
    }
}
