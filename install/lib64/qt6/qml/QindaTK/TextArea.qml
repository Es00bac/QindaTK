// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a wrapping multi-line field on inputBg
// that is at least `rows` lines tall and grows with its text. Place it in
// a Tk.Scroll when the text can outgrow its panel.
T.TextArea {
    id: control

    property int rows: 3
    property bool mono: false
    property bool error: false
    property string tooltip: ""

    readonly property real minimumHeight: metrics.height * Math.max(1, rows) + topPadding + bottomPadding

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding, minimumHeight)
    padding: Tk.Theme.space.sm + 1
    leftPadding: Tk.Theme.space.sm + 2
    rightPadding: Tk.Theme.space.sm + 2
    color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
    selectionColor: Tk.Theme.color.selection
    selectedTextColor: Tk.Theme.color.text
    placeholderTextColor: Tk.Theme.color.textMuted
    font.family: control.mono ? Tk.Theme.font.monoFamily : Tk.Theme.font.family
    font.pixelSize: Tk.Theme.font.body
    wrapMode: TextEdit.WordWrap
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

    FontMetrics {
        id: metrics
        font: control.font
    }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.fieldWidth * 2
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
        Tk.Label {
            visible: !control.length && !control.preeditText
            x: control.leftPadding
            y: control.topPadding
            width: Math.max(0, parent.width - control.leftPadding - control.rightPadding)
            text: control.placeholderText
            color: control.placeholderTextColor
            font.family: control.font.family
            font.pixelSize: Tk.Theme.font.small
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }
}
