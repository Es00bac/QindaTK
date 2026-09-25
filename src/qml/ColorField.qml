// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 24px (small 20) colour readout: a
// 16px swatch over a checkerboard (so alpha shows) beside an editable hex
// value ("#rrggbb" or "#aarrggbb"; "#rgb" and bare hex are accepted on
// entry). A changed, valid user commit emits colorEdited(color); applyHex()
// and owner color assignment are silent. Swatch tap, key or accessible press
// emits pickRequested() for the host to open its picker. objectNames
// "colorHex" and "colorSwatch" identify the interactive parts.
T.Control {
    id: control

    property color color: Tk.Theme.color.accent
    property bool showHex: true
    property bool small: false
    property bool error: false
    property bool readOnly: false
    property string tooltip: ""
    readonly property bool inputInvalid: hexInput.visible && invalidDraft

    signal colorEdited(color color)
    signal pickRequested()

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real swatchSize: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg
    readonly property real edgePadding: Tk.Theme.space.sm + 1
    readonly property string hexText: control.color.toString()
    property bool invalidDraft: false

    function applyHex(text) {
        return setHex(text, false)
    }

    function setHex(text, notifyUser) {
        const match = /^\s*#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})\s*$/.exec(text)
        if (match === null) {
            if (notifyUser)
                invalidDraft = true
            return false
        }
        let hex = match[1]
        if (hex.length === 3) {
            hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2]
        }
        const next = Qt.color("#" + hex)
        if (Qt.colorEqual(next, control.color)) {
            hexInput.text = control.hexText
            invalidDraft = false
            return false
        }
        control.color = next
        hexInput.text = control.hexText
        invalidDraft = false
        if (notifyUser)
            control.colorEdited(next)
        return true
    }

    function commitUserDraft() {
        if (!control.enabled || control.readOnly)
            return false
        return setHex(hexInput.text, true)
    }

    function revertDraft() {
        invalidDraft = false
        hexInput.text = control.hexText
    }

    onColorChanged: {
        if (!hexInput.activeFocus) {
            // AGENT-GUARD: read the colour itself, not hexText: this handler can
            // run before the hexText binding re-evaluates, and assigning that
            // stale text also cut the text's binding (the hex froze while the
            // swatch followed an owner's colour).
            hexInput.text = control.color.toString()
            invalidDraft = false
        }
    }

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

    Accessible.role: Accessible.Grouping
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Colour field")
    Accessible.description: qsTr("Current colour %1").arg(control.hexText)

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
        border.color: control.error || control.inputInvalid ? Tk.Theme.color.danger
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
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Tk.Theme.size.focusRing
                color: "transparent"
                radius: Tk.Theme.radius.sm
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
                visible: swatch.activeFocus
            }
            HoverHandler {
                id: swatchHover
                cursorShape: Qt.PointingHandCursor
                enabled: control.enabled && !control.readOnly
            }
            TapHandler {
                enabled: control.enabled && !control.readOnly
                onTapped: control.pickRequested()
            }
            activeFocusOnTab: control.enabled && !control.readOnly
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Choose colour")
            Accessible.description: qsTr("Current colour %1").arg(control.hexText)
            Accessible.focusable: control.enabled && !control.readOnly
            Accessible.focused: activeFocus
            Accessible.onPressAction: if (control.enabled && !control.readOnly) control.pickRequested()
            Keys.onSpacePressed: function(event) {
                if (control.enabled && !control.readOnly) control.pickRequested()
                event.accepted = true
            }
            Keys.onReturnPressed: function(event) {
                if (control.enabled && !control.readOnly) control.pickRequested()
                event.accepted = true
            }
            Keys.onEnterPressed: function(event) {
                if (control.enabled && !control.readOnly) control.pickRequested()
                event.accepted = true
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
            readOnly: !control.enabled || control.readOnly
            selectByMouse: true
            maximumLength: 9
            clip: true
            Accessible.role: Accessible.EditableText
            Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Colour value")
            Accessible.description: control.inputInvalid ? qsTr("Invalid colour. Press Escape to restore %1").arg(control.hexText)
                                      : qsTr("Current colour %1").arg(control.hexText)
            onTextChanged: if (activeFocus && !readOnly) control.invalidDraft = false
            onAccepted: {
                control.commitUserDraft()
                selectAll()
            }
            onActiveFocusChanged: {
                if (activeFocus) {
                    selectAll()
                } else {
                    control.commitUserDraft()
                }
            }
            Keys.onEscapePressed: function(event) {
                control.revertDraft()
                selectAll()
                event.accepted = true
            }
            onReadOnlyChanged: if (readOnly) control.revertDraft()
        }
    }
}
