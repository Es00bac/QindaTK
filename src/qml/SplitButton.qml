// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Primary command plus a distinct menu segment. The command and menu have
// separate activation surfaces so the arrow can never invoke the command.
Item {
    id: control
    property Tk.CommandRegistry registry: null
    property Tk.ShortcutMap shortcutMap: null
    property string commandId: ""
    property string tooltip: ""
    property var menuModel: []
    property bool small: false
    property string placement: "bottom"
    readonly property alias menu: choices
    readonly property alias primaryButton: primary.button
    signal triggered()

    implicitWidth: primary.implicitWidth + arrow.implicitWidth
    implicitHeight: Math.max(primary.implicitHeight, arrow.implicitHeight)
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip.length ? tooltip : primary.button.text

    Tk.CommandButton {
        id: primary
        objectName: "splitPrimary"
        width: implicitWidth
        height: implicitHeight
        registry: control.registry
        shortcutMap: control.shortcutMap
        commandId: control.commandId
        small: control.small
        tooltip: control.tooltip
        onTriggered: control.triggered()
    }
    T.Button {
        id: arrow
        objectName: "splitMenuButton"
        x: primary.width
        width: Tk.Theme.size.control
        height: primary.height
        text: ""
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("More options")
        Accessible.description: control.tooltip
        Accessible.focusable: enabled
        Accessible.onPressAction: if (enabled) control.openMenu()
        onClicked: control.openMenu()
        Keys.onDownPressed: control.openMenu()
        background: Rectangle {
            color: arrow.down || arrow.hovered ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.controlBg
            border.width: Tk.Theme.size.border
            border.color: arrow.visualFocus ? Tk.Theme.color.focus : Tk.Theme.color.controlBorder
            radius: Tk.Theme.radius.sm
        }
        contentItem: Tk.Icon {
            name: "chevron-down"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
        }
        Tk.ToolTip { text: control.tooltip.length ? control.tooltip : qsTr("More options"); visible: arrow.hovered }
    }
    Tk.Menu {
        id: choices
        title: control.tooltip
        Repeater {
            model: Array.isArray(control.menuModel) ? control.menuModel : []
            delegate: Tk.MenuItem {
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
            }
        }
        onClosed: arrow.forceActiveFocus(Qt.PopupFocusReason)
    }
    function openMenu() {
        if (!enabled || !Array.isArray(menuModel) || menuModel.length === 0) return
        let point = Qt.point(0, arrow.height)
        if (placement === "top") point = Qt.point(0, -choices.height)
        else if (placement === "left") point = Qt.point(-choices.width, 0)
        else if (placement === "right") point = Qt.point(arrow.width, 0)
        choices.popup(arrow, point)
    }
}
