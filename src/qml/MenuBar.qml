// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// A 22px menu bar on the surface colour: small-font titles, the open one
// filled with the active control tint. Children are Tk.Menu { title }.
T.MenuBar {
    id: bar

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    padding: 0
    leftPadding: Tk.Theme.space.xs
    rightPadding: Tk.Theme.space.xs
    spacing: 1

    delegate: T.MenuBarItem {
        id: entry

        implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Tk.Theme.size.statusBar
        leftPadding: Tk.Theme.space.sm
        rightPadding: Tk.Theme.space.sm
        topPadding: 0
        bottomPadding: 0
        hoverEnabled: true

        contentItem: Tk.Label {
            // The '&' mnemonic marker is for the keyboard (Alt+letter,
            // handled by the template), not for the eye.
            text: entry.text.replace(/&(?!&)/g, "").replace(/&&/g, "&")
            font.pixelSize: Tk.Theme.font.small
            color: !entry.enabled ? Tk.Theme.color.textDisabled
                 : entry.highlighted || entry.hovered ? Tk.Theme.color.text : Tk.Theme.color.textMuted
            horizontalAlignment: Text.AlignHCenter
        }

        background: Rectangle {
            implicitWidth: 40
            implicitHeight: Tk.Theme.size.statusBar
            radius: Tk.Theme.radius.xs
            color: entry.highlighted ? Tk.Theme.color.controlActiveBg
                 : entry.hovered ? Tk.Theme.color.hover : "transparent"
        }
    }

    contentItem: Row {
        spacing: bar.spacing
        Repeater {
            model: bar.contentModel
        }
    }

    background: Rectangle {
        implicitHeight: Tk.Theme.size.statusBar
        color: Tk.Theme.color.surface
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Tk.Theme.color.divider
        }
    }
}
