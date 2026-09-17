// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 14px box (small 12), radius xs,
// accent-filled with a `check` glyph when checked, a `minus` glyph when
// partially checked (tristate). Row height 20 (small 16), body text.
T.CheckBox {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property bool small: false
    property string tooltip: ""

    readonly property real boxSize: small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
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

    Accessible.role: Accessible.CheckBox
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
        implicitWidth: control.boxSize
        implicitHeight: control.boxSize
        radius: Tk.Theme.radius.xs
        color: control.checkState !== Qt.Unchecked ? Tk.Theme.color.accent
             : control.down ? Tk.Theme.color.pressed
             : control.hovered ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.inputBg
        border.width: control.checkState !== Qt.Unchecked ? 0 : Tk.Theme.size.border
        border.color: control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.inputBorder
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Tk.Icon {
            anchors.centerIn: parent
            name: control.checkState === Qt.PartiallyChecked ? "minus" : "check"
            size: control.boxSize * 0.85
            strokeWidth: 3
            color: Tk.Theme.color.accentContrast
            visible: control.checkState !== Qt.Unchecked
        }
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

    contentItem: Tk.Label {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
        font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
        verticalAlignment: Text.AlignVCenter
    }
}
