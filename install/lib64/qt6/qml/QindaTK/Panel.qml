// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: panel chrome. A 24px header (grip, uppercase title,
// trailing actions, collapse / float / close buttons) over a padded body.
// The panel only reports intent (collapseToggled, floatRequested,
// closeRequested); a host such as DockFrame decides what happens. Measured
// from Sloom Studio: header 24px, title 10px semibold uppercase, 1px
// divider, floating panels get a 6px radius and an accent border.
Item {
    id: panel

    default property alias content: bodyBox.content
    property alias actions: actionsHost.data
    readonly property Item headerItem: headerBox
    readonly property Item bodyItem: bodyBox

    property string title: ""
    property string iconName: ""
    property bool grip: true
    property bool headerVisible: true
    property bool collapsible: true
    property bool collapsed: false
    property bool closable: false
    property bool floatable: false
    property bool floating: false
    property bool active: false
    property real padding: Tk.Theme.space.sm
    property color color: Tk.Theme.color.panel

    signal collapseToggled(bool collapsed)
    signal closeRequested()
    signal floatRequested()

    implicitWidth: Math.max(headerBox.implicitWidth, bodyBox.implicitWidth)
    implicitHeight: (headerVisible ? headerBox.implicitHeight : 0) + (collapsed ? 0 : bodyBox.implicitHeight)

    Rectangle {
        anchors.fill: parent
        color: panel.color
        radius: panel.floating ? Tk.Theme.radius.md : 0
        border.width: panel.floating ? 1 : 0
        border.color: panel.active ? Tk.Theme.color.accent : Tk.Theme.color.borderStrong
        antialiasing: panel.floating
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: panel.floating ? 1 : 0
        direction: Tk.Flex.Column

        Tk.Box {
            id: headerBox
            objectName: "panelHeader"
            visible: panel.headerVisible
            color: Tk.Theme.color.headerBg
            borderBottom: panel.collapsed ? 0 : 1
            borderColor: Tk.Theme.color.divider
            radius: 0
            implicitHeight: Tk.Theme.size.header
            Tk.Flex.shrink: 0

            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                paddingLeft: Tk.Theme.space.sm
                paddingRight: Tk.Theme.space.xs

                Tk.Icon {
                    visible: panel.grip
                    name: "grip-vertical"
                    size: Tk.Theme.size.iconSm
                    color: Tk.Theme.color.textMuted
                    Tk.Flex.shrink: 0
                }
                Tk.Icon {
                    visible: panel.iconName.length > 0
                    name: panel.iconName
                    size: Tk.Theme.size.iconSm
                    color: panel.active ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                    Tk.Flex.shrink: 0
                }
                Tk.Overline {
                    objectName: "panelTitle"
                    title: panel.title
                    color: panel.active ? Tk.Theme.color.text : Tk.Theme.color.headerText
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Flex {
                    id: actionsHost
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: 1
                    Tk.Flex.shrink: 0
                }
                Tk.IconButton {
                    objectName: "panelCollapse"
                    visible: panel.collapsible
                    small: true
                    iconName: panel.collapsed ? "chevron-down" : "chevron-up"
                    tooltip: panel.collapsed ? qsTr("Expand") : qsTr("Collapse")
                    onClicked: {
                        panel.collapsed = !panel.collapsed
                        panel.collapseToggled(panel.collapsed)
                    }
                }
                Tk.IconButton {
                    objectName: "panelFloat"
                    visible: panel.floatable
                    small: true
                    iconName: panel.floating ? "minimize-2" : "maximize-2"
                    tooltip: panel.floating ? qsTr("Dock") : qsTr("Float")
                    onClicked: panel.floatRequested()
                }
                Tk.IconButton {
                    objectName: "panelClose"
                    visible: panel.closable
                    small: true
                    iconName: "x"
                    tooltip: qsTr("Close")
                    onClicked: panel.closeRequested()
                }
            }
        }

        Tk.Box {
            id: bodyBox
            objectName: "panelBody"
            visible: !panel.collapsed
            padding: panel.padding
            clipContent: true
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
        }
    }
}
