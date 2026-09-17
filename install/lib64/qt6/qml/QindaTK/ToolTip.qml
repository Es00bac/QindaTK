// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Dense tooltip: caption text on the popover surface, 600ms delay. Use the
// attached form on any control: `Tk.ToolTip.text: qsTr("Undo")`,
// `Tk.ToolTip.visible: hovered`.
T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - Tk.Theme.space.xs
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    margins: Tk.Theme.space.xs
    padding: Tk.Theme.space.xs
    leftPadding: Tk.Theme.space.sm + 1
    rightPadding: Tk.Theme.space.sm + 1
    delay: 600
    timeout: 6000
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font.family: Tk.Theme.font.family
        font.pixelSize: Tk.Theme.font.caption
        color: Tk.Theme.color.tooltipText
        wrapMode: Text.Wrap
        renderType: Text.NativeRendering
    }

    background: Rectangle {
        color: Tk.Theme.color.tooltipBg
        border.width: 1
        border.color: Tk.Theme.color.popoverBorder
        radius: Tk.Theme.radius.sm
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Tk.Theme.motion.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Tk.Theme.motion.fast } }
}
