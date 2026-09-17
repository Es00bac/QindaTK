// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the 24px (small 20) single-line field
// on inputBg with a 1px inputBorder; focus paints inputFocusBorder plus
// the focus ring; `error` paints the danger colour. Optional leading
// `iconName`, a `clearable` × button (objectName "fieldClear") and a
// `trailing` slot for buttons or units. `cleared()` fires when the user
// clears the field through the button or Escape.
T.TextField {
    id: control

    property string iconName: ""
    property bool clearable: false
    property bool small: false
    property bool error: false
    property bool mono: false
    property string tooltip: ""
    property alias trailing: trailingHost.data

    signal cleared()

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real iconSize: small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
    readonly property real edgePadding: Tk.Theme.space.sm + 2
    readonly property bool showClear: clearable && control.length > 0 && control.enabled

    function clearText() {
        if (control.length === 0) {
            return
        }
        control.clear()
        control.cleared()
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    leftPadding: edgePadding + (iconName.length > 0 ? iconSize + Tk.Theme.space.xs + 2 : 0)
    rightPadding: edgePadding + trailingHost.implicitWidth
                  + (showClear ? clearButton.width + Tk.Theme.space.xs : 0)
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
    selectionColor: Tk.Theme.color.selection
    selectedTextColor: Tk.Theme.color.text
    placeholderTextColor: Tk.Theme.color.textMuted
    font.family: control.mono ? Tk.Theme.font.monoFamily : Tk.Theme.font.family
    font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
    renderType: Text.NativeRendering
    hoverEnabled: true
    selectByMouse: true
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.EditableText
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : control.placeholderText
    Accessible.description: control.error ? qsTr("Error") : ""

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    Keys.onEscapePressed: function(event) {
        if (control.clearable && control.length > 0) {
            control.clearText()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    // AGENT-GUARD: interactive parts live as direct children, not inside
    // `background`: T.TextField stacks its background at z -1, so the text
    // input takes every press before a button parented there could.
    Tk.Icon {
        visible: control.iconName.length > 0
        name: control.iconName
        size: control.iconSize
        color: control.activeFocus ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
        x: control.edgePadding
        y: (control.height - height) / 2
        z: 1
    }
    Tk.IconButton {
        id: clearButton
        objectName: "fieldClear"
        visible: control.showClear
        small: true
        iconName: "x"
        tooltip: qsTr("Clear")
        focusPolicy: Qt.NoFocus
        anchors.right: trailingHost.left
        anchors.rightMargin: Tk.Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        z: 1
        onClicked: control.clearText()
    }
    Tk.Flex {
        id: trailingHost
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        anchors.right: parent.right
        anchors.rightMargin: control.edgePadding - Tk.Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        width: implicitWidth
        height: implicitHeight
        z: 1
    }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.fieldWidth
        implicitHeight: control.controlHeight
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: control.error ? Tk.Theme.color.danger
                    : control.activeFocus ? Tk.Theme.color.inputFocusBorder
                    : control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.inputBorder
        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: control.error ? Tk.Theme.alpha(Tk.Theme.color.danger, 0.4) : Tk.Theme.color.focus
            visible: control.activeFocus
        }
        // AGENT-NOTE: placeholder at the small size (11px), one step under
        // body, so it reads as a hint inside a 24px field and still lines up
        // with the typed text.
        Tk.Label {
            visible: !control.length && !control.preeditText
            x: control.leftPadding
            width: Math.max(0, parent.width - control.leftPadding - control.rightPadding)
            height: parent.height
            text: control.placeholderText
            color: control.placeholderTextColor
            font.family: control.font.family
            font.pixelSize: control.small ? Tk.Theme.font.caption : Tk.Theme.font.small
            verticalAlignment: Text.AlignVCenter
        }
    }
}
