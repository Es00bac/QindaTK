// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: a dense tab row. `model` is a list of strings or of
// {text, iconName, closable, tooltip}; `currentIndex` is the open tab.
// The strip never owns pages: bind a page's `visible` to the index.
// 22px tall (Theme.size.tab), caption font; the active tab shows the text
// colour with a 2px accent underline ("underline") or a filled pill.
Item {
    id: strip

    property var model: []
    property int currentIndex: 0
    property string style: "underline"
    property bool closable: false
    property bool small: false
    property bool stretch: false
    readonly property int count: Array.isArray(strip.model) ? strip.model.length : 0

    signal tabActivated(int index)
    signal tabClosed(int index)

    implicitWidth: row.implicitWidth
    implicitHeight: Tk.Theme.size.tab
    activeFocusOnTab: true

    Accessible.role: Accessible.PageTabList

    function entry(index) {
        const raw = strip.model[index]
        if (typeof raw === "string") {
            return { "text": raw, "iconName": "", "closable": strip.closable, "tooltip": "" }
        }
        return {
            "text": raw.text !== undefined ? raw.text : "",
            "iconName": raw.iconName !== undefined ? raw.iconName : "",
            "closable": raw.closable !== undefined ? raw.closable : strip.closable,
            "tooltip": raw.tooltip !== undefined ? raw.tooltip : ""
        }
    }

    // The delegate item of one tab (hit-testing a right-click or a
    // double-click on a tab that is not the current one).
    function tabItem(index) {
        return tabs.itemAt(index)
    }
    function activate(index) {
        if (index < 0 || index >= strip.count) {
            return
        }
        strip.currentIndex = index
        strip.tabActivated(index)
    }

    Keys.onLeftPressed: strip.activate(Math.max(0, strip.currentIndex - 1))
    Keys.onRightPressed: strip.activate(Math.min(strip.count - 1, strip.currentIndex + 1))

    Rectangle {
        // The baseline under an underline strip; the active underline sits on it.
        visible: strip.style === "underline"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Tk.Theme.color.divider
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        align: Tk.Flex.Stretch
        gap: strip.style === "pill" ? Tk.Theme.space.xs : 0

        Repeater {
            id: tabs
            model: strip.count
            delegate: T.TabButton {
                id: tab
                required property int index
                readonly property var entry: strip.entry(index)
                readonly property bool current: strip.currentIndex === index

                objectName: "tab_" + index
                Tk.Flex.grow: strip.stretch ? 1 : 0
                Tk.Flex.basis: strip.stretch ? 0 : -1
                implicitWidth: label.implicitWidth + leftPadding + rightPadding
                implicitHeight: Tk.Theme.size.tab
                leftPadding: Tk.Theme.space.md
                rightPadding: tab.entry.closable ? Tk.Theme.space.xs : Tk.Theme.space.md
                checked: current
                checkable: false
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                text: tab.entry.text

                Tk.ToolTip {
                    text: tab.entry.tooltip
                    visible: tab.entry.tooltip.length > 0 && tab.hovered
                    delay: 600
                }
                Accessible.name: tab.entry.text

                onClicked: strip.activate(index)

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        anchors.bottomMargin: strip.style === "underline" ? 1 : 0
                        radius: strip.style === "pill" ? Tk.Theme.radius.sm : 0
                        color: strip.style === "pill" && tab.current ? Tk.Theme.color.controlActiveBg
                             : tab.hovered ? Tk.Theme.color.hover : "transparent"
                        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
                    }
                    Rectangle {
                        visible: strip.style === "underline" && tab.current
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Tk.Theme.color.accent
                    }
                }

                contentItem: Tk.Flex {
                    id: label
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs

                    Tk.Icon {
                        visible: tab.entry.iconName.length > 0
                        name: tab.entry.iconName.length > 0 ? tab.entry.iconName : "circle"
                        size: Tk.Theme.size.iconSm
                        color: tab.current ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                        Tk.Flex.shrink: 0
                    }
                    Tk.Label {
                        text: tab.entry.text
                        font.pixelSize: strip.small ? Tk.Theme.font.micro : Tk.Theme.font.caption
                        font.weight: tab.current ? Font.DemiBold : Font.Normal
                        color: tab.current ? Tk.Theme.color.text
                             : tab.hovered ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                        horizontalAlignment: Text.AlignHCenter
                        Tk.Flex.grow: strip.stretch ? 1 : 0
                        Tk.Flex.basis: strip.stretch ? 0 : -1
                    }
                    Tk.IconButton {
                        objectName: "tabClose_" + tab.index
                        visible: tab.entry.closable
                        small: true
                        iconName: "x"
                        tooltip: qsTr("Close tab")
                        Tk.Flex.shrink: 0
                        onClicked: strip.tabClosed(tab.index)
                    }
                }
            }
        }
    }
}
