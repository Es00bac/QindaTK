// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Thin overlay scrollbar: an 8px rounded thumb in the accent tint over a
// track that only appears on hover, as the original's WebKit scrollbar
// styling does. Attach with `T.ScrollBar.vertical: Tk.ScrollBar {}`.
T.ScrollBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 1
    minimumSize: 0.06
    visible: control.size < 1.0 && control.policy !== T.ScrollBar.AlwaysOff
    hoverEnabled: true

    contentItem: Rectangle {
        implicitWidth: Tk.Theme.size.scrollbar
        implicitHeight: Tk.Theme.size.scrollbar
        radius: Math.min(width, height) / 2
        color: control.pressed ? Tk.Theme.color.accent
             : control.hovered ? Tk.Theme.alpha(Tk.Theme.color.accent, 0.6)
             : Tk.Theme.color.scrollThumb
        opacity: control.active || control.hovered ? 1.0 : 0.55
        Behavior on opacity { NumberAnimation { duration: Tk.Theme.motion.fast } }
    }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.scrollbar + 2
        implicitHeight: Tk.Theme.size.scrollbar + 2
        color: Tk.Theme.color.scrollTrack
        opacity: control.hovered || control.pressed ? 0.9 : 0.0
        Behavior on opacity { NumberAnimation { duration: Tk.Theme.motion.fast } }
    }
}
