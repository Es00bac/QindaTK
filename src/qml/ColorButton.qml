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
// AGENT-NOTE (D-318): "Automatic" and "No Color" are modes, not colours —
// a document stores "use the automatic colour" or "no fill", which no
// `color` value can say. They are opt-in entries above the swatches;
// `colorMode` ("color", "automatic", "none") is what the swatch shows.
T.AbstractButton {
    id: control
    Tk.Flex.shrink: 0

    property color color: Tk.Theme.color.accent
    property var colors: []
    property int columns: 8
    property bool small: false
    property bool ghost: true
    property string tooltip: ""
    property bool automaticEntry: false
    property bool noColorEntry: false
    property string automaticText: qsTr("Automatic")
    property string noColorText: qsTr("No Color")
    // The colour "Automatic" stands for (e.g. the text colour); drawn in the swatch.
    property color automaticColor: Tk.Theme.color.text
    property string colorMode: "color"

    signal colorSelected(color color)
    signal customRequested()
    signal automaticSelected()
    signal noColorSelected()

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real swatchSize: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg

    implicitWidth: swatchSize + Tk.Theme.size.iconSm + Tk.Theme.space.sm * 2 + Tk.Theme.space.xs
    implicitHeight: controlHeight
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: control.enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.Button
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Colour")
    Accessible.description: control.colorMode === "automatic" ? control.automaticText
                          : control.colorMode === "none" ? control.noColorText : ""
    Accessible.focusable: control.enabled
    Accessible.focused: control.activeFocus
    Accessible.onPressAction: if (control.enabled) control.click()

    onClicked: popover.open()

    function chooseAutomatic() {
        colorMode = "automatic"
        automaticSelected()
        popover.close()
    }
    function chooseNoColor() {
        colorMode = "none"
        noColorSelected()
        popover.close()
    }

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
            color: control.colorMode === "automatic" ? control.automaticColor
                 : control.colorMode === "none" ? "transparent" : control.color
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.borderStrong
            clip: true
            // "No Color": the empty swatch struck through, as office suites draw it.
            Rectangle {
                objectName: "colorButtonNoColorMark"
                visible: control.colorMode === "none"
                anchors.centerIn: parent
                width: parent.width * Math.SQRT2
                height: Tk.Theme.size.border * 2
                rotation: -45
                color: Tk.Theme.color.danger
            }
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

        Tk.Flex {
            direction: Tk.Flex.Column
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "colorButtonAutomatic"
                visible: control.automaticEntry
                variant: control.colorMode === "automatic" ? "outline" : "ghost"
                small: true
                iconName: "square"
                text: control.automaticText
                tooltip: qsTr("Use the automatic colour")
                onClicked: control.chooseAutomatic()
            }
            Tk.Button {
                objectName: "colorButtonNoColor"
                visible: control.noColorEntry
                variant: control.colorMode === "none" ? "outline" : "ghost"
                small: true
                iconName: "circle-off"
                text: control.noColorText
                tooltip: qsTr("Use no colour")
                onClicked: control.chooseNoColor()
            }
            Tk.ColorSwatches {
                objectName: "colorButtonSwatches"
                colors: control.colors
                columns: control.columns
                current: control.colorMode === "color" ? control.color : "transparent"
                onSelected: function(color) {
                    control.color = color
                    control.colorMode = "color"
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
}
