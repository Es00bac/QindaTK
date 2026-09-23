// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): one 24px row of joined segments (small
// 20). `model` is a list of strings or {text, iconName, value, tooltip};
// the selected segment paints controlActiveBg, the rest are ghost. Each
// segment is objectName "segment_<index>". `activated(index)` fires on a
// user click (not on programmatic currentIndex changes).
Item {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property var model: []
    property int currentIndex: 0
    readonly property var currentValue: {
        const entry = control.entry(control.currentIndex)
        return entry === null ? undefined : (entry.value !== undefined ? entry.value : entry.text)
    }
    property bool small: false
    property bool stretch: false
    property string tooltip: ""
    // Compatibility alias: callers that used the old spelling still disable
    // the real Item.enabled state inherited by every segment.
    property alias enabled_: control.enabled

    signal activated(int index)

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property int count: control.model === undefined || control.model === null ? 0 : control.model.length

    function entry(index) {
        if (index < 0 || index >= control.count) {
            return null
        }
        const item = control.model[index]
        return typeof item === "string" ? { "text": item } : item
    }

    function isEntryEnabled(index) {
        const item = control.entry(index)
        return control.enabled && item !== null && item.enabled !== false
    }

    function activate(index) {
        if (!control.isEntryEnabled(index)) {
            return
        }
        control.currentIndex = index
        control.activated(index)
    }

    function move(from, direction) {
        if (control.count < 1) return
        for (let step = 1; step <= control.count; ++step) {
            const next = (from + direction * step + control.count) % control.count
            if (control.isEntryEnabled(next)) {
                control.activate(next)
                const item = segments.itemAt(next)
                if (item !== null) item.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
    }

    implicitWidth: row.implicitWidth + Tk.Theme.size.border * 2
    implicitHeight: control.controlHeight
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.PageTabList
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Selection")

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && hover.hovered
        delay: 600
    }

    HoverHandler { id: hover }

    Rectangle {
        anchors.fill: parent
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.controlBg
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.controlBorder
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        anchors.margins: Tk.Theme.size.border
        direction: Tk.Flex.Row
        align: Tk.Flex.Stretch
        gap: 0

        Repeater {
            id: segments
            model: control.count
            delegate: T.AbstractButton {
                id: segment
                required property int index
                readonly property var entry: control.entry(index)
                readonly property bool selected: index === control.currentIndex
                enabled: control.isEntryEnabled(index)
                opacity: control.enabled && entry !== null && entry.enabled === false
                         ? Tk.Theme.opacity.disabled : 1.0
                objectName: "segment_" + index
                text: entry !== null && entry.text !== undefined ? entry.text : ""
                // AGENT-GUARD: never `Tk.Flex.flex: 0` here: the shorthand sets
                // basis 0, which collapses every segment to zero width.
                Tk.Flex.grow: control.stretch ? 1 : 0
                Tk.Flex.basis: control.stretch ? 0 : -1
                Tk.Flex.shrink: control.stretch ? 1 : 0
                implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding,
                                        Tk.Theme.size.controlSm)
                Tk.Flex.minWidth: Tk.Theme.size.controlSm
                implicitHeight: control.controlHeight - Tk.Theme.size.border * 2
                leftPadding: Tk.Theme.space.md
                rightPadding: Tk.Theme.space.md
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: text
                Accessible.description: entry !== null && entry.tooltip !== undefined ? entry.tooltip : ""
                Accessible.selected: selected
                Accessible.focusable: enabled
                Accessible.focused: activeFocus
                Accessible.onPressAction: control.activate(index)
                Tk.ToolTip {
                    text: entry !== null && entry.tooltip !== undefined ? entry.tooltip : ""
                    visible: text.length > 0 && hovered
                    delay: 600
                }
                onClicked: {
                    control.activate(index)
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Left) control.move(index, -1)
                    else if (event.key === Qt.Key_Right) control.move(index, 1)
                    else return
                    event.accepted = true
                }

                background: Rectangle {
                    radius: Tk.Theme.radius.sm - 1
                    color: segment.selected ? Tk.Theme.color.controlActiveBg
                         : segment.down ? Tk.Theme.color.pressed
                         : segment.hovered ? Tk.Theme.color.hover : "transparent"
                    Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
                    Rectangle {
                        // Seam between unselected neighbours.
                        visible: segment.index > 0 && !segment.selected
                                 && segment.index - 1 !== control.currentIndex
                        width: Tk.Theme.size.border
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                        anchors.topMargin: Tk.Theme.space.xs + 1
                        anchors.bottomMargin: Tk.Theme.space.xs + 1
                        color: Tk.Theme.color.divider
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -1
                        radius: parent.radius + 1
                        color: "transparent"
                        border.width: Tk.Theme.size.focusRing
                        border.color: Tk.Theme.color.focus
                        visible: segment.visualFocus
                    }
                }

                contentItem: Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    justify: Tk.Flex.Center
                    gap: Tk.Theme.space.xs + 2
                    Tk.Icon {
                        visible: segment.entry !== null && segment.entry.iconName !== undefined
                                 && segment.entry.iconName.length > 0
                        name: visible ? segment.entry.iconName : ""
                        size: control.small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
                        color: segment.selected ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                        Tk.Flex.shrink: 0
                    }
                    Tk.Label {
                        visible: segment.text.length > 0
                        text: segment.text
                        color: segment.selected ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                        font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
                        font.weight: segment.selected ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
