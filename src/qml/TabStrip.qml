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
    // AGENT-NOTE: count reads .length, never Array.isArray — a C++
    // QStringList/QVariantList property arrives as a sequence object that
    // HAS length and indexing but is NOT a true JS array, so an isArray()
    // test reports 0 and every tab vanishes (QindaCalc's sheet strip bound
    // a QStringList and rendered an empty bar).
    readonly property int count: strip.model != null && strip.model.length !== undefined
                                 ? strip.model.length : 0

    signal tabActivated(int index)
    signal tabClosed(int index)

    implicitWidth: row.implicitWidth
    implicitHeight: Tk.Theme.size.tab
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Tabs")

    function entry(index) {
        if (index < 0 || index >= strip.count) return null
        const raw = strip.model[index]
        if (typeof raw === "string") {
            return { "text": raw, "iconName": "", "closable": strip.closable, "tooltip": "" }
        }
        return {
            "text": raw.text !== undefined ? raw.text : "",
            "iconName": raw.iconName !== undefined ? raw.iconName : "",
            "closable": raw.closable !== undefined ? raw.closable : strip.closable,
            "tooltip": raw.tooltip !== undefined ? raw.tooltip : "",
            "enabled": raw.enabled !== false
        }
    }

    // The delegate item of one tab (hit-testing a right-click or a
    // double-click on a tab that is not the current one).
    function tabItem(index) {
        return tabs.itemAt(index)
    }
    function activate(index) {
        const item = strip.entry(index)
        if (!strip.enabled || item === null || item.enabled === false) {
            return
        }
        strip.currentIndex = index
        strip.tabActivated(index)
    }

    function move(from, direction, boundary) {
        const start = boundary === "first" ? 0
                    : boundary === "last" ? strip.count - 1 : from + direction
        const step = boundary === "first" ? 1 : boundary === "last" ? -1 : direction
        for (let index = start; index >= 0 && index < strip.count; index += step) {
            const item = strip.entry(index)
            if (strip.enabled && item !== null && item.enabled !== false) {
                strip.activate(index)
                const tab = tabs.itemAt(index)
                if (tab !== null) tab.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
    }

    Accessible.focusable: false

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
                focusPolicy: Qt.TabFocus
                enabled: strip.enabled && tab.entry !== null && tab.entry.enabled !== false
                opacity: strip.enabled && tab.entry.enabled === false
                         ? Tk.Theme.opacity.disabled : 1.0
                text: tab.entry.text

                Tk.ToolTip {
                    text: tab.entry.tooltip
                    visible: tab.entry.tooltip.length > 0 && tab.hovered
                    delay: 600
                }
                Accessible.role: Accessible.PageTab
                Accessible.name: tab.entry.text
                Accessible.description: tab.entry.tooltip
                Accessible.selected: current
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                Accessible.onPressAction: strip.activate(index)

                onClicked: strip.activate(index)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Left) strip.move(index, -1, "")
                    else if (event.key === Qt.Key_Right) strip.move(index, 1, "")
                    else if (event.key === Qt.Key_Home) strip.move(index, 1, "first")
                    else if (event.key === Qt.Key_End) strip.move(index, -1, "last")
                    else return
                    event.accepted = true
                }

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
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -Tk.Theme.size.focusRing
                        color: "transparent"
                        border.width: Tk.Theme.size.focusRing
                        border.color: Tk.Theme.color.focus
                        visible: tab.visualFocus && tab.enabled
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
