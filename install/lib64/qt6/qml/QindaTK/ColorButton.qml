// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a toolbar button showing the current
// `color` as a swatch with a chevron; clicking opens a Tk.Popover with a
// Tk.ColorSwatches over `colors`. Picking a swatch sets `color` and emits
// colorSelected(color); "Custom…" emits customRequested() (the host opens
// its picker and sets `color` itself). Control height (24; `small` 20).
// objectNames "colorButtonSwatch", "colorButtonPopover".
T.AbstractButton {
    id: control
    Tk.Flex.shrink: 0

    property color color: Tk.Theme.color.accent
    property var colors: []
    property int columns: 8
    property bool small: false
    property bool ghost: true
    property string tooltip: ""

    signal colorSelected(color color)
    signal customRequested()

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real swatchSize: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg

    implicitWidth: swatchSize + Tk.Theme.size.iconSm + Tk.Theme.space.sm * 2 + Tk.Theme.space.xs
    implicitHeight: controlHeight
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: control.enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.Button
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Colour")

    onClicked: popover.open()

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !popover.visible
        delay: 600
    }

    background: Rectangle {
        radius: Tk.Theme.radius.sm
        color: control.down ? Tk.Theme.color.pressed
             : popover.visible ? Tk.Theme.color.accentSubtle
             : control.hovered ? Tk.Theme.color.hover
             : control.ghost ? "transparent" : Tk.Theme.color.controlBg
        border.width: popover.visible || !control.ghost ? 1 : 0
        border.color: popover.visible ? Tk.Theme.color.controlActiveBorder : Tk.Theme.color.controlBorder
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
        implicitWidth: swatch.width + Tk.Theme.space.xs + chevron.width
        implicitHeight: control.swatchSize

        Rectangle {
            id: swatch
            objectName: "colorButtonSwatch"
            width: control.swatchSize
            height: control.swatchSize
            anchors.verticalCenter: parent.verticalCenter
            radius: Tk.Theme.radius.xs
            color: control.color
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.borderStrong
        }
        Tk.Icon {
            id: chevron
            anchors.left: swatch.right
            anchors.leftMargin: Tk.Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-down"
            size: Tk.Theme.size.iconSm
            color: control.hovered || popover.visible ? Tk.Theme.color.text : Tk.Theme.color.textMuted
        }
    }

    Tk.Popover {
        id: popover
        objectName: "colorButtonPopover"
        anchorItem: control
        placement: "bottom"

        Tk.ColorSwatches {
            colors: control.colors
            columns: control.columns
            current: control.color
            onSelected: function(color) {
                control.color = color
                control.colorSelected(color)
                popover.close()
            }
            onCustomRequested: {
                popover.close()
                control.customRequested()
            }
        }
    }
}
