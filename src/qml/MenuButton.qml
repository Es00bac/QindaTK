// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Button whose activation only opens a command menu. The selected row may
// update the button's label and icon while registry state remains authoritative.
T.Button {
    id: control
    property Tk.CommandRegistry registry: null
    property Tk.ShortcutMap shortcutMap: null
    property var menuModel: []
    property string selectedCommandId: ""
    property string tooltip: ""
    property string placement: "bottom"
    property bool showSelectedCommand: true
    readonly property string displayedIconName: showSelectedCommand && selectedBinding.iconName.length > 0
                                               ? selectedBinding.iconName : ""
    readonly property alias menu: choices
    readonly property string selectedLabel: selectedBinding.label
    signal choiceTriggered(string commandId)

    text: showSelectedCommand && selectedBinding.label.length > 0
          ? selectedBinding.label : qsTr("Options")
    implicitWidth: Math.max(Tk.Theme.size.control,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Tk.Theme.size.control
    leftPadding: Tk.Theme.space.md
    rightPadding: Tk.Theme.space.md
    spacing: Tk.Theme.space.xs + 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: tooltip.length ? tooltip : text
    Accessible.description: selectedBinding.reason
    Accessible.focusable: enabled
    Accessible.onPressAction: if (enabled) openMenu()
    onClicked: openMenu()
    Keys.onDownPressed: openMenu()
    background: Rectangle {
        radius: Tk.Theme.radius.sm
        color: control.down ? Tk.Theme.color.pressed
             : control.hovered ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.controlBg
        border.width: Tk.Theme.size.border
        border.color: control.visualFocus ? Tk.Theme.color.focus
                      : control.hovered ? Tk.Theme.color.controlHoverBorder
                                        : Tk.Theme.color.controlBorder
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: Tk.Theme.alpha(Tk.Theme.color.bg, Tk.Theme.opacity.hidden)
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.visualFocus
        }
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        justify: Tk.Flex.Center
        gap: control.spacing
        Tk.Icon {
            visible: control.displayedIconName.length > 0
            name: control.displayedIconName
            size: Tk.Theme.size.icon
            color: control.enabled ? Tk.Theme.color.textMuted : Tk.Theme.color.textDisabled
        }
        Tk.Label {
            text: control.text
            color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
            font.pixelSize: Tk.Theme.font.body
            Tk.Flex.grow: 1
        }
        Tk.Icon {
            name: "chevron-down"
            size: Tk.Theme.size.iconSm
            color: control.enabled ? Tk.Theme.color.textMuted : Tk.Theme.color.textDisabled
        }
    }
    Tk.CommandBinding {
        id: selectedBinding
        registry: control.registry
        shortcutMap: control.shortcutMap
        commandId: control.selectedCommandId
    }
    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: Tk.Theme.motion.tooltipDelay
    }
    Tk.Menu {
        id: choices
        title: control.tooltip.length ? control.tooltip : control.text
        Repeater {
            model: Array.isArray(control.menuModel) ? control.menuModel : []
            delegate: Tk.MenuItem {
                id: menuEntry
                required property var modelData
                text: modelData.label || ""
                iconName: modelData.iconName || ""
                shortcut: modelData.shortcut || ""
                commandRegistry: control.registry
                shortcutMap: control.shortcutMap
                commandId: modelData.commandId || modelData.id || ""
                tooltip: modelData.tooltip || modelData.label || ""
                visible: modelData.visible !== false
                enabled: modelData.enabled !== false
                Connections {
                    target: menuEntry
                    function onTriggered() {
                        if (menuEntry.commandId.length > 0)
                            control.selectedCommandId = menuEntry.commandId
                        control.choiceTriggered(menuEntry.commandId)
                    }
                }
            }
        }
        onClosed: control.forceActiveFocus(Qt.PopupFocusReason)
    }
    function openMenu() {
        if (!enabled || !Array.isArray(menuModel) || menuModel.length === 0) return
        let point = Qt.point(0, control.height)
        if (placement === "top") point = Qt.point(0, -choices.height)
        else if (placement === "left") point = Qt.point(-choices.width, 0)
        else if (placement === "right") point = Qt.point(control.width, 0)
        choices.popup(control, point)
    }
}
