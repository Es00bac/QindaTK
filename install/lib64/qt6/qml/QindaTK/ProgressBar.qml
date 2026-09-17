// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 3px (thin: 2px) accent bar on a
// divider-coloured track, radius full. `indeterminate` slides a third-width
// segment; the animation stays off when reduced motion zeroes the motion
// ladder (a 0ms looping animation would spin the CPU).
T.ProgressBar {
    id: control

    property bool thin: false
    property string tooltip: ""

    readonly property real trackHeight: thin ? Tk.Theme.space.xs : Tk.Theme.space.xs + 1

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    Accessible.role: Accessible.ProgressBar
    Accessible.name: control.tooltip

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && hover.hovered
        delay: 600
    }
    HoverHandler { id: hover }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.fieldWidth
        implicitHeight: control.trackHeight
        radius: height / 2
        color: Tk.Theme.color.divider
    }

    contentItem: Item {
        implicitHeight: control.trackHeight

        Rectangle {
            id: bar
            readonly property real segment: parent.width / 3
            readonly property int slideDuration: Tk.Theme.motion.slow * 5
            y: 0
            height: parent.height
            radius: height / 2
            color: Tk.Theme.color.accent
            width: control.indeterminate ? segment : control.visualPosition * parent.width
            x: control.indeterminate && bar.slideDuration <= 0 ? segment : 0

            NumberAnimation on x {
                running: control.indeterminate && control.visible && bar.slideDuration > 0
                from: -bar.segment
                to: bar.parent.width
                duration: bar.slideDuration
                loops: Animation.Infinite
                easing.type: Easing.InOutSine
            }
        }
    }
}
