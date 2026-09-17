// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// What an empty list or panel says out loud: a muted icon, an overline
// title, a wrapping caption and optional actions, centred in the space
// it is given. An empty panel with nothing in it reads as a broken one.
Item {
    id: empty

    property string text: ""
    property string title: ""
    property string iconName: ""
    property real maxTextWidth: 320
    default property alias actions: actionsHost.data

    implicitWidth: Math.max(column.implicitWidth, 120)
    implicitHeight: column.implicitHeight + Tk.Theme.space.lg * 2

    Accessible.role: Accessible.StaticText
    Accessible.name: empty.title.length > 0 ? empty.title + ". " + empty.text : empty.text

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Center
        justify: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.lg

        Tk.Icon {
            visible: empty.iconName.length > 0
            name: empty.iconName.length > 0 ? empty.iconName : "circle"
            size: Tk.Theme.size.iconXl
            color: Tk.Theme.color.textMuted
            Tk.Flex.shrink: 0
        }
        Tk.Overline {
            objectName: "emptyTitle"
            visible: empty.title.length > 0
            title: empty.title
            horizontalAlignment: Text.AlignHCenter
        }
        Tk.Caption {
            objectName: "emptyText"
            visible: empty.text.length > 0
            text: empty.text
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            horizontalAlignment: Text.AlignHCenter
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            Tk.Flex.maxWidth: empty.maxTextWidth
        }
        Tk.Flex {
            id: actionsHost
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            visible: children.length > 0
        }
    }
}
