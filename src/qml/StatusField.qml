// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One entry of a StatusBar: optional icon and caption text. Clickable
// (hover tint, `clicked`) so a field can open what it reports on.
Item {
    id: field
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property string text: ""
    property string iconName: ""
    property bool muted: true
    property string tooltip: ""
    readonly property bool hovered: hover.hovered

    signal clicked()

    implicitWidth: row.implicitWidth + Tk.Theme.space.xs * 2
    implicitHeight: Tk.Theme.size.statusBar

    Accessible.role: Accessible.StaticText
    Accessible.name: field.text

    Tk.ToolTip {
        text: field.tooltip
        visible: field.tooltip.length > 0 && field.hovered
        delay: 600
    }

    Rectangle {
        anchors.fill: parent
        radius: Tk.Theme.radius.xs
        color: field.hovered ? Tk.Theme.color.hover : "transparent"
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        paddingLeft: Tk.Theme.space.xs
        paddingRight: Tk.Theme.space.xs

        Tk.Icon {
            visible: field.iconName.length > 0
            name: field.iconName.length > 0 ? field.iconName : "circle"
            size: Tk.Theme.size.iconSm
            color: field.muted && !field.hovered ? Tk.Theme.color.textMuted : Tk.Theme.color.text
            Tk.Flex.shrink: 0
        }
        Tk.Caption {
            text: field.text
            muted: field.muted && !field.hovered
        }
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: field.clicked() }
}
