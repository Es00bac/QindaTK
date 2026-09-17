// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 24px (small 20) colour readout: a
// 16px swatch over a checkerboard (so alpha shows) beside an editable hex
// value ("#rrggbb" or "#aarrggbb"; "#rgb" and bare hex are accepted on
// entry). Editing the hex emits colorEdited(color); tapping the swatch
// emits pickRequested() for the host to open its picker. objectName
// "colorHex" on the hex input.
T.Control {
    id: control

    property color color: Tk.Theme.color.accent
    property bool showHex: true
    property bool small: false
    property bool error: false
    property string tooltip: ""

    signal colorEdited(color color)
    signal pickRequested()

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real swatchSize: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg
    readonly property real edgePadding: Tk.Theme.space.sm + 1
    readonly property string hexText: control.color.toString()

    function applyHex(text) {
        const match = /^\s*#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})\s*$/.exec(text)
        if (match === null) {
            hexInput.text = control.hexText
            return false
        }
        let hex = match[1]
        if (hex.length === 3) {
            hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2]
        }
        const next = Qt.color("#" + hex)
        if (Qt.colorEqual(next, control.color)) {
            hexInput.text = control.hexText
            return false
        }
        control.color = next
        control.colorEdited(next)
        return true
    }

    onColorChanged: if (!hexInput.activeFocus) hexInput.text = control.hexText

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: edgePadding
    rightPadding: edgePadding
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.EditableText
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Colour %1").arg(control.hexText)

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    background: Rectangle {
        implicitWidth: control.showHex ? Tk.Theme.size.fieldWidth : control.controlHeight
        implicitHeight: control.controlHeight
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: control.error ? Tk.Theme.color.danger
                    : hexInput.activeFocus ? Tk.Theme.color.inputFocusBorder
                    : control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.inputBorder
        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: hexInput.activeFocus
        }
    }

    contentItem: Item {
        implicitWidth: swatch.width + (control.showHex ? Tk.Theme.space.sm + hexInput.implicitWidth : 0)
        implicitHeight: swatch.height

        Item {
            id: swatch
            objectName: "colorSwatch"
            width: control.swatchSize
            height: control.swatchSize
            y: (parent.height - height) / 2

            Canvas {
                id: checker
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    const cell = Math.max(2, Math.floor(width / 4))
                    ctx.fillStyle = Tk.Theme.color.panel
                    ctx.fillRect(0, 0, width, height)
                    ctx.fillStyle = Tk.Theme.color.borderStrong
                    for (let y = 0; y < height; y += cell) {
                        for (let x = 0; x < width; x += cell) {
                            if (((x / cell) + (y / cell)) % 2 === 0) {
                                ctx.fillRect(x, y, cell, cell)
                            }
                        }
                    }
                }
                Connections {
                    target: Tk.Theme
                    function onChanged() { checker.requestPaint() }
                }
            }
            Rectangle {
                anchors.fill: parent
                color: control.color
                radius: Tk.Theme.radius.xs
                border.width: Tk.Theme.size.border
                border.color: swatchHover.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.borderStrong
            }
            HoverHandler {
                id: swatchHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: control.pickRequested()
            }
        }

        TextInput {
            id: hexInput
            objectName: "colorHex"
            visible: control.showHex
            x: swatch.width + Tk.Theme.space.sm
            width: Math.max(0, parent.width - x)
            height: parent.height
            verticalAlignment: TextInput.AlignVCenter
            text: control.hexText
            color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
            selectionColor: Tk.Theme.color.selection
            selectedTextColor: Tk.Theme.color.text
            font.family: Tk.Theme.font.monoFamily
            font.pixelSize: control.small ? Tk.Theme.font.caption : Tk.Theme.font.small
            renderType: Text.NativeRendering
            readOnly: !control.enabled
            selectByMouse: true
            maximumLength: 9
            clip: true
            onAccepted: {
                control.applyHex(text)
                selectAll()
            }
            onActiveFocusChanged: {
                if (activeFocus) {
                    selectAll()
                } else {
                    control.applyHex(text)
                }
            }
            Keys.onEscapePressed: function(event) {
                text = control.hexText
                selectAll()
                event.accepted = true
            }
        }
    }
}
