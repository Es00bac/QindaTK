// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 2px track with an accent fill and a
// 10px round handle (8px small) in a 16px (14px) row, so it sits inside a
// 24px inspector row. `showValue` adds a mono readout on the right.
// `orientation: Qt.Vertical` swaps the axes: the track extent follows
// `availableHeight`, the handle rides `visualPosition` (value grows upward,
// the direction Qt's template keys Up/Down to), and the readout moves below
// the track (`bottomPadding`). `valueModified(value)` fires on user moves
// (pointer and keyboard).
T.Slider {
    id: control

    property bool showValue: false
    property int decimals: 0
    property string suffix: ""
    property bool small: false
    property string tooltip: ""

    signal valueModified(real value)

    readonly property real rowHeight: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg
    readonly property real handleSize: small ? Tk.Theme.space.md : Tk.Theme.size.grip
    readonly property real trackThickness: Tk.Theme.space.xs
    readonly property string readout: Number(control.value).toFixed(Math.max(0, control.decimals)) + control.suffix

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)
    padding: 0
    rightPadding: showValue && horizontal ? valueText.implicitWidth + Tk.Theme.space.sm : 0
    bottomPadding: showValue && vertical ? valueText.implicitHeight + Tk.Theme.space.sm : 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    onMoved: control.valueModified(control.value)

    Accessible.role: Accessible.Slider
    Accessible.name: control.tooltip

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !control.pressed
        delay: 600
    }

    background: Item {
        implicitWidth: control.horizontal ? Tk.Theme.size.fieldWidth : control.rowHeight
        implicitHeight: control.horizontal ? control.rowHeight : Tk.Theme.size.fieldWidth

        Rectangle {
            id: track
            objectName: "sliderTrack"
            x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
            y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
            width: control.horizontal ? control.availableWidth : control.trackThickness
            height: control.horizontal ? control.trackThickness : control.availableHeight
            radius: control.trackThickness / 2
            color: Tk.Theme.color.borderStrong
        }
        Rectangle {
            objectName: "sliderFill"
            x: track.x
            y: track.y + (control.horizontal ? 0 : control.visualPosition * track.height)
            width: control.horizontal ? control.visualPosition * track.width : track.width
            height: control.horizontal ? track.height : control.position * track.height
            radius: control.trackThickness / 2
            color: control.enabled ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
        }
        Tk.Mono {
            id: valueText
            visible: control.showValue
            // Horizontal reserves a readout column on the right
            // (rightPadding); vertical reserves a row below (bottomPadding).
            x: control.horizontal ? parent.width - width : (parent.width - width) / 2
            y: control.horizontal ? (parent.height - height) / 2 : parent.height - height
            text: control.readout
            color: Tk.Theme.color.textMuted
            font.pixelSize: control.small ? Tk.Theme.font.caption : Tk.Theme.font.small
            horizontalAlignment: Text.AlignRight
        }
    }

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal
                                  ? control.visualPosition * (control.availableWidth - width)
                                  : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal
                                 ? (control.availableHeight - height) / 2
                                 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: control.handleSize
        implicitHeight: control.handleSize
        radius: width / 2
        color: control.pressed ? Tk.Theme.lighten(Tk.Theme.color.accent, 0.2)
             : control.hovered ? Tk.Theme.color.text : Tk.Theme.color.accent
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.bg
        antialiasing: true
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -Tk.Theme.size.focusRing
            radius: width / 2
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.visualFocus
        }
    }
}
