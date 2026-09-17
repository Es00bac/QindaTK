// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The heading row of a grouped block inside a panel: an Overline title,
// an optional count, trailing actions, and an optional disclosure chevron
// that toggles `collapsed` (bind the block's `visible` to !collapsed).
// AGENT-NOTE: an Item wraps the Flex because Flex owns its implicit size;
// the row height comes from Theme.size.row, not from the text.
Item {
    id: header

    property string title: ""
    property string count: ""
    property bool collapsible: false
    property bool collapsed: false
    default property alias trailing: trailingHost.data

    implicitWidth: row.implicitWidth
    implicitHeight: Tk.Theme.size.row

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs

        Tk.Icon {
            visible: header.collapsible
            name: header.collapsed ? "chevron-right" : "chevron-down"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
            Tk.Flex.shrink: 0
        }
        Tk.Overline {
            objectName: "sectionTitle"
            title: header.title
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
        Tk.Caption {
            visible: header.count.length > 0
            text: header.count
            Tk.Flex.shrink: 0
        }
        Tk.Flex {
            id: trailingHost
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Flex.shrink: 0
        }
    }

    TapHandler {
        enabled: header.collapsible
        onTapped: header.collapsed = !header.collapsed
    }
}
