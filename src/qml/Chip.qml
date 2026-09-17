// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the Sloom pill. Measured from the
// running original: 32px tall (small 28), radius 6, 1px border, 8px
// horizontal padding, icon and label 6px apart, label and glyph in the
// accent colour. `rounded` turns it into the 36px fully round island
// action with 10px padding. `emphasis` follows the original's three
// weights: "plain" for most actions, "tinted" for one that is on, "solid"
// for the primary. `active` (alias of checked) lights the border.
T.AbstractButton {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property string iconName: ""
    property bool chevron: false
    property alias active: control.checked
    property string emphasis: "plain"
    property bool rounded: false
    property bool small: false
    property string tooltip: ""

    readonly property real controlHeight: rounded ? Tk.Theme.size.action
                                        : small ? Tk.Theme.size.controlLg : Tk.Theme.size.chip
    readonly property real iconSize: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg
    readonly property bool lit: control.checked || control.emphasis !== "plain"
    readonly property color fillColor: {
        if (control.emphasis === "solid") {
            return control.hovered || control.down ? Tk.Theme.color.chipHoverBg : Tk.Theme.color.controlActiveBg
        }
        if (control.emphasis === "tinted" || control.checked) {
            return control.hovered || control.down ? Tk.Theme.color.chipHoverBg : Tk.Theme.color.chipBg
        }
        if (control.rounded) {
            // Island actions rest on the glass; only hover paints them.
            return control.down ? Tk.Theme.color.pressed
                 : control.hovered ? Tk.Theme.color.panel : "transparent"
        }
        return control.hovered || control.down ? Tk.Theme.color.chipHoverBg : Tk.Theme.color.chipBg
    }
    readonly property color outlineColor: {
        if (control.emphasis === "solid") return Tk.Theme.color.controlActiveBorder
        if (control.checked) return Tk.Theme.color.accent
        if (control.emphasis === "tinted") return Tk.Theme.color.controlActiveBorder
        if (control.rounded) return "transparent"
        return control.hovered ? Tk.Theme.color.chipHoverBorder : Tk.Theme.color.chipBorder
    }
    readonly property color labelColor: !control.enabled ? Tk.Theme.color.textDisabled
                                      : control.rounded && !control.lit ? Tk.Theme.color.text
                                      : Tk.Theme.color.accentText

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: rounded ? Tk.Theme.space.md + 2 : Tk.Theme.space.md
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    spacing: control.rounded ? Tk.Theme.space.md : Tk.Theme.space.sm + 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: control.checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.name: control.text.length > 0 ? control.text : control.tooltip
    Accessible.description: control.tooltip

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    background: Rectangle {
        implicitWidth: control.controlHeight
        implicitHeight: control.controlHeight
        radius: control.rounded ? height / 2 : Tk.Theme.radius.md
        color: control.fillColor
        border.width: Tk.Theme.size.border
        border.color: control.outlineColor
        antialiasing: true
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }

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

        Tk.Icon {
            visible: control.iconName.length > 0
            name: control.iconName
            size: control.iconSize
            color: control.labelColor
            Tk.Flex.shrink: 0
        }
        // AGENT-GUARD: the label is a Text item, never a Control's `text`
        // rendered by a style: Qt eats `&` in styled button labels as a
        // mnemonic, which turned "Inputs & Data" into "Inputs_Data" once.
        Tk.Label {
            visible: control.text.length > 0
            text: control.text
            color: control.labelColor
            font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
            font.weight: control.lit ? Font.DemiBold : Font.Normal
            Tk.Flex.shrink: 1
        }
        Tk.Icon {
            visible: control.chevron
            name: "chevron-down"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
            Tk.Flex.shrink: 0
        }
    }
}
