// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the text button. 24px (small 20,
// large 28), radius sm, body font, padding-x space.md. Variants: "default"
// (controlBg + controlBorder, text slightly muted at rest as Sloom's
// theme-button), "accent" (accent fill, accentContrast text), "ghost" (no
// fill or border at rest), "outline" (border only), "danger" (danger fill).
// `busy` swaps the icon for a Spinner and disables the button. Consumers
// express capability through `available`, not `enabled`, because the
// busy state owns the enabled binding.
T.Button {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property string iconName: ""
    property string trailingIconName: ""
    property string variant: "default"
    property bool small: false
    property bool large: false
    property bool busy: false
    property bool available: true
    property string tooltip: ""

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm
                                        : large ? Tk.Theme.size.controlLg
                                        : Tk.Theme.size.control
    readonly property real iconSize: small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
    readonly property bool filled: variant === "accent" || variant === "danger"
    readonly property color fillColor: {
        if (variant === "accent") {
            const accent = Tk.Theme.color.accent
            return control.down ? Tk.Theme.darken(accent, 0.12)
                 : control.hovered ? Tk.Theme.lighten(accent, 0.08) : accent
        }
        if (variant === "danger") {
            const danger = Tk.Theme.color.danger
            return control.down ? Tk.Theme.darken(danger, 0.12)
                 : control.hovered ? Tk.Theme.lighten(danger, 0.08) : danger
        }
        if (variant === "ghost" || variant === "outline") {
            return control.checked ? Tk.Theme.color.controlActiveBg
                 : control.down ? Tk.Theme.color.pressed
                 : control.hovered ? Tk.Theme.color.hover : "transparent"
        }
        return control.checked ? Tk.Theme.color.controlActiveBg
             : control.down || control.hovered ? Tk.Theme.color.controlHoverBg
             : Tk.Theme.color.controlBg
    }
    readonly property color outlineColor: {
        if (control.filled) return "transparent"
        if (variant === "ghost") return control.checked ? Tk.Theme.color.controlActiveBorder : "transparent"
        if (variant === "outline") return control.hovered || control.checked
                ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.borderStrong
        return control.checked ? Tk.Theme.color.controlActiveBorder
             : control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.controlBorder
    }
    readonly property color textColor: {
        if (!control.enabled) return Tk.Theme.color.textDisabled
        if (variant === "accent") return Tk.Theme.color.accentContrast
        if (variant === "danger") return Tk.Theme.color.dangerContrast
        if (control.checked) return Tk.Theme.color.accentText
        if (control.hovered || control.down) return Tk.Theme.color.text
        // Sloom's theme-button: label at 82% text / 18% muted at rest.
        return Tk.Theme.mix(Tk.Theme.color.text, Tk.Theme.color.textMuted, 0.18)
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: small ? Tk.Theme.space.sm + 2 : Tk.Theme.space.md
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    spacing: Tk.Theme.space.xs + 2
    enabled: available && !busy
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.Button
    Accessible.name: control.busy ? qsTr("%1, busy").arg(control.text) : control.text
    Accessible.description: control.tooltip

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    background: Rectangle {
        implicitWidth: control.controlHeight
        implicitHeight: control.controlHeight
        radius: Tk.Theme.radius.sm
        color: control.fillColor
        border.width: control.filled ? 0 : Tk.Theme.size.border
        border.color: control.outlineColor
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

    contentItem: Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        justify: Tk.Flex.Center
        gap: control.spacing

        Tk.Spinner {
            visible: control.busy
            size: control.iconSize
            running: control.busy
            color: control.textColor
            Tk.Flex.shrink: 0
        }
        Tk.Icon {
            visible: !control.busy && control.iconName.length > 0
            name: control.iconName
            size: control.iconSize
            color: control.textColor
            Tk.Flex.shrink: 0
        }
        Tk.Label {
            visible: control.text.length > 0
            text: control.text
            color: control.textColor
            font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
            font.weight: control.filled ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            Tk.Flex.shrink: 1
        }
        Tk.Icon {
            visible: control.trailingIconName.length > 0
            name: control.trailingIconName
            size: control.iconSize
            color: control.textColor
            Tk.Flex.shrink: 0
        }
    }
}
