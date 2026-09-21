// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Two handles on one track, for a band rather than a point.
//
// AGENT-CONTRACT (docs/controls-media.md): matches Slider's metrics — a 2px
// track, a 10px handle (8px small), in a 16px (14px) row — so a range and a
// value sit on the same inspector grid. `rangeModified(first, second)` fires
// on user moves from either handle.
//
// AGENT-NOTE: the handles are deliberately allowed to meet and cross-clamp
// rather than being blocked at each other. A user dragging the upper handle
// down past the lower one means "I want this band to start here", and a slider
// that simply refuses feels broken; T.RangeSlider's own clamping keeps first
// <= second, so the band collapses rather than inverting.
T.RangeSlider {
    id: control

    property bool showValues: false
    property int decimals: 0
    property string suffix: ""
    property bool small: false
    property string tooltip: ""

    signal rangeModified(real first, real second)

    readonly property real rowHeight: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg
    readonly property real handleSize: small ? Tk.Theme.space.md : Tk.Theme.size.grip
    readonly property real trackThickness: Tk.Theme.space.xs
    readonly property string readout:
        Number(control.first.value).toFixed(Math.max(0, control.decimals))
        + control.suffix + "–"
        + Number(control.second.value).toFixed(Math.max(0, control.decimals))
        + control.suffix

    objectName: "rangeSlider"
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            first.implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             first.implicitHandleHeight + topPadding + bottomPadding)
    padding: 0
    rightPadding: showValues ? valueText.implicitWidth + Tk.Theme.space.sm : 0
    hoverEnabled: true
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    first.onMoved: control.rangeModified(control.first.value, control.second.value)
    second.onMoved: control.rangeModified(control.first.value, control.second.value)

    Accessible.role: Accessible.Slider
    Accessible.name: control.tooltip

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    background: Item {
        implicitWidth: Tk.Theme.size.fieldWidth
        implicitHeight: control.rowHeight

        Rectangle {
            objectName: "rangeSliderTrack"
            x: control.leftPadding
            y: control.topPadding + (control.availableHeight - height) / 2
            width: control.availableWidth
            height: control.trackThickness
            radius: height / 2
            color: Tk.Theme.color.borderStrong

            // Only the band between the handles is accented: the point of a
            // range control is to show what is included, not where it starts.
            Rectangle {
                objectName: "rangeSliderBand"
                x: control.first.position * parent.width
                width: (control.second.position - control.first.position) * parent.width
                height: parent.height
                radius: parent.radius
                color: Tk.Theme.color.accent
            }
        }
    }

    first.handle: Rectangle {
        objectName: "rangeSliderFirstHandle"
        x: control.leftPadding + control.first.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: control.handleSize
        implicitHeight: control.handleSize
        radius: width / 2
        color: control.first.pressed ? Tk.Theme.color.pressed : Tk.Theme.color.controlBg
        border.width: Tk.Theme.size.border
        border.color: control.first.pressed || control.first.hovered
                      ? Tk.Theme.color.accent : Tk.Theme.color.controlBorder
    }

    second.handle: Rectangle {
        objectName: "rangeSliderSecondHandle"
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: control.handleSize
        implicitHeight: control.handleSize
        radius: width / 2
        color: control.second.pressed ? Tk.Theme.color.pressed : Tk.Theme.color.controlBg
        border.width: Tk.Theme.size.border
        border.color: control.second.pressed || control.second.hovered
                      ? Tk.Theme.color.accent : Tk.Theme.color.controlBorder
    }

    Tk.Mono {
        id: valueText
        objectName: "rangeSliderValue"
        visible: control.showValues
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: control.readout
        color: Tk.Theme.color.textMuted
    }
}
