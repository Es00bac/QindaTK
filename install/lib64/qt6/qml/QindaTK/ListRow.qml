// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: a dense list row. 22px (28 with `secondaryText`), text
// in body size, optional icon, `leading`/`trailing` slots, hover tint,
// `selected` paints the selection role, `active` lights the text in the
// accent. Right-clicks arrive as `rightClicked(point)` for a context
// menu; left click and double click come from AbstractButton.
T.ItemDelegate {
    id: row

    property string secondaryText: ""
    property string iconName: ""
    property bool selected: false
    property bool active: false
    property bool small: false
    property real indent: 0
    property alias leading: leadingHost.data
    property alias trailing: trailingHost.data
    readonly property Item leadingItem: leadingHost
    readonly property Item trailingItem: trailingHost
    readonly property bool twoLine: row.secondaryText.length > 0

    signal rightClicked(var point)

    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: row.twoLine ? Tk.Theme.size.rowLg
                  : row.small ? Tk.Theme.size.controlSm : Tk.Theme.size.row
    leftPadding: Tk.Theme.space.sm + row.indent
    rightPadding: Tk.Theme.space.sm
    topPadding: 0
    bottomPadding: 0
    spacing: Tk.Theme.space.sm
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    highlighted: row.selected

    Accessible.role: Accessible.ListItem
    Accessible.name: row.text
    Accessible.selected: row.selected

    background: Rectangle {
        color: row.down ? Tk.Theme.color.pressed
             : row.selected ? Tk.Theme.color.selection
             : row.hovered ? Tk.Theme.color.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
    }

    contentItem: Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: row.spacing

        Tk.Flex {
            id: leadingHost
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            visible: children.length > 0
            Tk.Flex.shrink: 0
        }
        Tk.Icon {
            visible: row.iconName.length > 0
            name: row.iconName.length > 0 ? row.iconName : "circle"
            size: row.small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
            color: row.active ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
            Tk.Flex.shrink: 0
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            justify: Tk.Flex.Center
            gap: 0
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minWidth: 0

            Tk.Label {
                objectName: "rowText"
                text: row.text
                accent: row.active
                font.pixelSize: row.small ? Tk.Theme.font.small : Tk.Theme.font.body
                font.weight: row.active ? Font.DemiBold : Font.Normal
            }
            Tk.Caption {
                objectName: "rowSecondary"
                visible: row.twoLine
                text: row.secondaryText
            }
        }
        Tk.Flex {
            id: trailingHost
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            visible: children.length > 0
            Tk.Flex.shrink: 0
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(eventPoint) { row.rightClicked(eventPoint.position) }
    }
}
