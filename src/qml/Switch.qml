// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 26x14 track (small 22x12), radius
// full, accent when on, with a 10px (8px) knob that slides. Row height 20.
// The track is derived from the icon sizes so it scales with density.
T.Switch {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property bool small: false
    property string tooltip: ""

    readonly property real trackHeight: small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
    readonly property real trackWidth: trackHeight * 2 - Tk.Theme.space.xs
    readonly property real knobSize: trackHeight - Tk.Theme.space.sm
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
        implicitWidth: control.trackWidth
        implicitHeight: control.trackHeight
        radius: height / 2
        color: control.checked ? Tk.Theme.color.accent
             : control.hovered ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.inputBg
        border.width: control.checked ? 0 : Tk.Theme.size.border
        border.color: control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.borderStrong
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            readonly property real inset: (parent.height - height) / 2
            x: Math.max(inset, Math.min(parent.width - width - inset,
                        control.visualPosition * (parent.width - width - inset * 2) + inset))
            y: inset
            width: control.knobSize
            height: control.knobSize
            radius: height / 2
            color: control.checked ? Tk.Theme.color.accentContrast : Tk.Theme.color.textMuted
            Behavior on x {
                enabled: !control.down
                NumberAnimation { duration: Tk.Theme.motion.fast; easing.type: Easing.OutCubic }
            }
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
