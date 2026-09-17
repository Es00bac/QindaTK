// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A floating, fully rounded control island that sits over the canvas
// (Sloom's `theme-control` glass pills): the canvas at 72% behind a 1px
// low-contrast border, 6px vertical and 8px horizontal padding, children
// in a centred row. Anchor it over the work area, not inside a toolbar.
Rectangle {
    id: island

    default property alias content: row.data
    property real spacing: 6
    property real paddingH: 8
    property real paddingV: 6

    implicitWidth: row.implicitWidth + paddingH * 2
    implicitHeight: row.implicitHeight + paddingV * 2
    radius: height / 2
    color: Tk.Theme.color.islandBg
    border.width: 1
    border.color: Tk.Theme.color.islandBorder
    antialiasing: true

    Tk.Flex {
        id: row
        anchors.fill: parent
        anchors.leftMargin: island.paddingH
        anchors.rightMargin: island.paddingH
        anchors.topMargin: island.paddingV
        anchors.bottomMargin: island.paddingV
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: island.spacing
    }
}
