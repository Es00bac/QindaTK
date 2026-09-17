// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A 1px rule. Horizontal by default; `vertical: true` for toolbars.
// Inside a Flex it takes no main-axis space beyond its thickness and
// stretches on the cross axis.
Rectangle {
    id: divider

    property bool vertical: false
    property real thickness: 1
    property real inset: 0

    implicitWidth: vertical ? thickness : inset * 2 + 1
    implicitHeight: vertical ? inset * 2 + 1 : thickness
    color: Tk.Theme.color.divider
}
