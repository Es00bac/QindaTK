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
    readonly property bool canToggle: collapsible && enabled

    implicitWidth: row.implicitWidth
    implicitHeight: Tk.Theme.size.row
    activeFocusOnTab: canToggle

    Accessible.role: collapsible ? Accessible.Button : Accessible.StaticText
    Accessible.name: header.title.length > 0 ? header.title : qsTr("Section")
    Accessible.description: collapsible
                              ? enabled
                                  ? collapsed
                                      ? qsTr("Collapsed. Activate to expand.")
                                      : qsTr("Expanded. Activate to collapse.")
                                  : collapsed ? qsTr("Disabled. Collapsed")
                                              : qsTr("Disabled. Expanded")
                              : ""
    Accessible.focusable: canToggle
    Accessible.focused: activeFocus

    function toggle() {
        if (canToggle) {
            collapsed = !collapsed
        }
    }

    Keys.onSpacePressed: function(event) {
        if (canToggle) {
            toggle()
            event.accepted = true
        }
    }
    Keys.onReturnPressed: function(event) {
        if (canToggle) {
            toggle()
            event.accepted = true
        }
    }
    Keys.onEnterPressed: function(event) {
        if (canToggle) {
            toggle()
            event.accepted = true
        }
    }

    Accessible.onPressAction: header.toggle()

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

    Item {
        anchors.fill: parent
        visible: header.activeFocus && header.canToggle

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: Tk.Theme.size.focusRing
            color: Tk.Theme.color.focus
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Tk.Theme.size.focusRing
            color: Tk.Theme.color.focus
        }
        Rectangle {
            anchors.left: parent.left
            width: Tk.Theme.size.focusRing
            height: parent.height
            color: Tk.Theme.color.focus
        }
        Rectangle {
            anchors.right: parent.right
            width: Tk.Theme.size.focusRing
            height: parent.height
            color: Tk.Theme.color.focus
        }
    }

    TapHandler {
        enabled: header.canToggle
        onTapped: header.toggle()
    }
}
