// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Dense tooltip: caption text on the popover surface, 600ms delay. Declare
// a child `Tk.ToolTip { text: qsTr("Undo"); visible: hovered }` on its owner;
// the attached form creates the QtQuick.Controls style tooltip instead.
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
    leftPadding: Tk.Theme.space.sm + Tk.Theme.size.border
    rightPadding: Tk.Theme.space.sm + Tk.Theme.size.border
    delay: Tk.Theme.motion.tooltipDelay
    timeout: Tk.Theme.motion.tooltipTimeout
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
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.popoverBorder
        radius: Tk.Theme.radius.sm
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: Tk.Theme.opacity.hidden; to: Tk.Theme.opacity.full; duration: Tk.Theme.motion.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: Tk.Theme.opacity.full; to: Tk.Theme.opacity.hidden; duration: Tk.Theme.motion.fast } }
}
