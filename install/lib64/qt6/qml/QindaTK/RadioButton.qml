// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 14px circle (small 12) with an
// accent dot when checked; exclusive within its parent like any
// T.RadioButton. Row height 20.
T.RadioButton {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property bool small: false
    property string tooltip: ""

    readonly property real circleSize: small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
    readonly property real rowHeight: small ? Tk.Theme.size.controlSm - Tk.Theme.space.sm
                                            : Tk.Theme.size.controlSm

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 0
    spacing: Tk.Theme.space.sm + 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.RadioButton
    Accessible.name: control.text.length > 0 ? control.text : control.tooltip
    Accessible.description: control.tooltip

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    background: Item {
        implicitHeight: control.rowHeight
    }

    indicator: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: control.circleSize
        implicitHeight: control.circleSize
        radius: width / 2
        color: control.down ? Tk.Theme.color.pressed
             : control.hovered ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: control.checked ? Tk.Theme.color.accent
                    : control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.inputBorder
        antialiasing: true

        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.5
            height: width
            radius: width / 2
            color: Tk.Theme.color.accent
            visible: control.checked
            antialiasing: true
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: width / 2
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.visualFocus
        }
    }

    contentItem: Tk.Label {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
        font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
        verticalAlignment: Text.AlignVCenter
    }
}
