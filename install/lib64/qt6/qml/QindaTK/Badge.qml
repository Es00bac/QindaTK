// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 16px count/status pill in micro
// semibold text, radius full. `variant` picks the status tint; `dot`
// collapses it to an 8px status dot.
Rectangle {
    id: badge
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property string text: ""
    property string variant: "default"
    property bool dot: false
    property string tooltip: ""

    readonly property color tint: variant === "accent" ? Tk.Theme.color.accent
                                : variant === "danger" ? Tk.Theme.color.danger
                                : variant === "success" ? Tk.Theme.color.success
                                : variant === "warning" ? Tk.Theme.color.warning
                                : variant === "info" ? Tk.Theme.color.info
                                : Tk.Theme.color.textMuted

    // 16 = the small control height minus one spacing step; scales with density.
    readonly property real pillHeight: Tk.Theme.size.controlSm - Tk.Theme.space.sm

    implicitHeight: dot ? Tk.Theme.space.md : pillHeight
    implicitWidth: dot ? Tk.Theme.space.md : Math.max(pillHeight, label.implicitWidth + (Tk.Theme.space.sm + 1) * 2)
    radius: height / 2
    color: dot ? badge.tint
         : variant === "default" ? Tk.Theme.color.controlBg : Tk.Theme.alpha(badge.tint, 0.18)
    border.width: variant === "default" && !dot ? Tk.Theme.size.border : 0
    border.color: Tk.Theme.color.controlBorder
    antialiasing: true

    Accessible.role: Accessible.StaticText
    Accessible.name: badge.text.length > 0 ? badge.text : badge.tooltip

    Tk.ToolTip {
        text: badge.tooltip
        visible: badge.tooltip.length > 0 && hover.hovered
        delay: 600
    }
    HoverHandler { id: hover }

    Tk.Label {
        id: label
        visible: !badge.dot
        anchors.centerIn: parent
        text: badge.text
        color: badge.variant === "default" ? Tk.Theme.color.textMuted
             : badge.variant === "accent" ? Tk.Theme.color.accentText : badge.tint
        font.pixelSize: Tk.Theme.font.micro
        font.weight: Font.DemiBold
        font.letterSpacing: Tk.Theme.font.micro * 0.04
    }
}
