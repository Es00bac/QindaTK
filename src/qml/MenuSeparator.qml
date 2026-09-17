// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// A 1px divider between groups of menu items with xs margins.
T.MenuSeparator {
    id: separator

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: Tk.Theme.space.xs
    leftPadding: Tk.Theme.space.sm
    rightPadding: Tk.Theme.space.sm

    contentItem: Rectangle {
        implicitWidth: 120
        implicitHeight: 1
        color: Tk.Theme.color.divider
    }
}
