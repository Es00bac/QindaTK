// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// A square icon-only button at control height (24px; `small` is 20px).
// Ghost by default (no fill at rest), tinted on hover, accent when
// `checked`. `tooltip` sets the accessible name and the hover tooltip.
T.AbstractButton {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property string icon_: ""
    property alias iconName: control.icon_
    property real iconSize: control.small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
    property bool small: false
    property bool danger: false
    property bool ghost: true
    property string tooltip: ""
    property color iconColor: !control.enabled ? Tk.Theme.color.textDisabled
                            : control.danger ? Tk.Theme.color.danger
                            : control.checked ? Tk.Theme.color.accent
                            : control.hovered || control.down ? Tk.Theme.color.text
                            : Tk.Theme.color.textMuted

    implicitWidth: control.small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    implicitHeight: implicitWidth
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: control.enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.Button
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : control.text

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    background: Rectangle {
        radius: Tk.Theme.radius.sm
        color: control.down ? Tk.Theme.color.pressed
             : control.checked ? Tk.Theme.color.accentSubtle
             : control.hovered ? Tk.Theme.color.hover
             : control.ghost ? "transparent" : Tk.Theme.color.controlBg
        border.width: control.checked || (!control.ghost) ? 1 : 0
        border.color: control.checked ? Tk.Theme.color.controlActiveBorder : Tk.Theme.color.controlBorder
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.visualFocus
        }
    }

    contentItem: Item {
        implicitWidth: control.iconSize
        implicitHeight: control.iconSize
        Tk.Icon {
            anchors.centerIn: parent
            name: control.icon_
            size: control.iconSize
            color: control.iconColor
        }
    }
}
