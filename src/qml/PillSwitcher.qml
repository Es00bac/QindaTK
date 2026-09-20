// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a compact, fully rounded page switcher
// for top bars. Entries are strings or
// {text, iconName, value, tooltip, accent, objectName}; programmatic currentIndex changes
// are silent and activated(index, value) reports user changes only.
Item {
    id: control

    property var model: []
    property int currentIndex: 0
    property bool small: false
    property bool showLabels: true
    property bool showActiveMarker: true
    property string tooltip: ""
    readonly property int count: control.model === undefined || control.model === null
                                 ? 0 : control.model.length
    readonly property var currentValue: {
        const current = control.entry(control.currentIndex)
        return current === null ? undefined
                                : (current.value !== undefined ? current.value : current.text)
    }
    readonly property real itemHeight: control.small ? Tk.Theme.size.controlSm
                                                     : Tk.Theme.size.control

    signal activated(int index, var value)

    function entry(index) {
        if (index < 0 || index >= control.count) {
            return null
        }
        const item = control.model[index]
        return typeof item === "string" ? { "text": item } : item
    }

    function activate(index) {
        if (index < 0 || index >= control.count) {
            return
        }
        control.currentIndex = index
        control.activated(index, control.currentValue)
    }

    function move(from, amount) {
        if (control.count < 1) {
            return
        }
        const next = (from + amount + control.count) % control.count
        control.activate(next)
        const item = segments.itemAt(next)
        if (item !== null) {
            item.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    function itemAt(index) {
        return segments.itemAt(index)
    }

    implicitWidth: row.implicitWidth + Tk.Theme.space.xs * 2
    implicitHeight: control.itemHeight + Tk.Theme.space.xs * 2

    Accessible.role: Accessible.PageTabList
    Accessible.name: control.tooltip

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && hover.hovered
        delay: Tk.Theme.motion.slow
    }
    HoverHandler { id: hover }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Tk.Theme.color.islandBg
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.islandBorder
        antialiasing: true
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.size.border

        Repeater {
            id: segments
            model: control.count

            delegate: T.AbstractButton {
                id: segment
                required property int index
                readonly property var entry: control.entry(index)
                readonly property bool selected: index === control.currentIndex
                readonly property color accentColor: entry !== null && entry.accent !== undefined
                                                     ? entry.accent : Tk.Theme.color.accent
                objectName: entry !== null && entry.objectName !== undefined
                            && entry.objectName.length > 0
                            ? entry.objectName : "pillSwitcherItem_" + index
                text: entry !== null && entry.text !== undefined ? entry.text : ""
                checkable: true
                checked: selected
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                implicitWidth: contentRow.implicitWidth + Tk.Theme.space.md * 2
                implicitHeight: control.itemHeight
                leftPadding: Tk.Theme.space.md
                rightPadding: Tk.Theme.space.md
                topPadding: 0
                bottomPadding: 0

                Accessible.role: Accessible.PageTab
                Accessible.name: entry !== null && entry.tooltip !== undefined
                                 ? entry.tooltip : text
                Accessible.selected: selected

                Tk.ToolTip {
                    text: segment.Accessible.name
                    visible: segment.hovered && text.length > 0
                    delay: Tk.Theme.motion.slow
                }

                onClicked: control.activate(index)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Left) {
                        control.move(index, -1)
                    } else if (event.key === Qt.Key_Right) {
                        control.move(index, 1)
                    } else if (event.key === Qt.Key_Home) {
                        control.activate(0)
                    } else if (event.key === Qt.Key_End) {
                        control.activate(control.count - 1)
                    } else {
                        return
                    }
                    event.accepted = true
                }

                background: Rectangle {
                    radius: height / 2
                    color: segment.selected
                           ? Tk.Theme.mix(Tk.Theme.color.controlActiveBg,
                                          segment.accentColor, 1.0 - Tk.Theme.opacity.island)
                           : segment.down ? Tk.Theme.color.pressed
                           : segment.hovered ? Tk.Theme.color.hover : "transparent"
                    border.width: segment.selected ? Tk.Theme.size.border : 0
                    border.color: segment.selected
                                  ? Tk.Theme.mix(Tk.Theme.color.controlActiveBorder,
                                                 segment.accentColor, 1.0 - Tk.Theme.opacity.scrim)
                                  : "transparent"

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -Tk.Theme.size.border
                        radius: parent.radius + Tk.Theme.size.border
                        color: "transparent"
                        border.width: Tk.Theme.size.focusRing
                        border.color: Tk.Theme.color.focus
                        visible: segment.visualFocus
                    }
                }

                contentItem: Tk.Flex {
                    id: contentRow
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    justify: Tk.Flex.Center
                    gap: Tk.Theme.space.xs

                    Tk.Icon {
                        visible: segment.entry !== null
                                 && segment.entry.iconName !== undefined
                                 && segment.entry.iconName.length > 0
                        name: visible ? segment.entry.iconName : ""
                        size: control.small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
                        color: segment.selected ? segment.accentColor
                                                : Tk.Theme.color.textMuted
                        Tk.Flex.shrink: 0
                    }
                    Tk.Label {
                        visible: control.showLabels && segment.text.length > 0
                        text: segment.text
                        color: segment.selected ? Tk.Theme.color.text
                                                : Tk.Theme.color.textMuted
                        font.pixelSize: control.small ? Tk.Theme.font.small
                                                      : Tk.Theme.font.body
                        font.weight: segment.selected ? Font.DemiBold : Font.Normal
                        Tk.Flex.shrink: 0
                    }
                    Rectangle {
                        visible: control.showActiveMarker && segment.selected
                        implicitWidth: Tk.Theme.space.xs + Tk.Theme.size.border * 2
                        implicitHeight: implicitWidth
                        radius: height / 2
                        color: segment.accentColor
                        Tk.Flex.shrink: 0
                    }
                }
            }
        }
    }
}
