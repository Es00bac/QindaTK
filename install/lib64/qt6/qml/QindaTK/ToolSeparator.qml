// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A vertical rule between toolbar groups: 1px, inset 6px from the bar's
// top and bottom. AGENT-NOTE: an Item that stretches on the cross axis
// rather than a bare Divider, so the inset is measured from the bar and
// not from the row's centred content height.
Item {
    id: separator

    property real inset: Tk.Theme.space.sm + 2
    property real margin: Tk.Theme.space.xs

    Tk.Flex.alignSelf: Tk.Flex.Stretch
    Tk.Flex.shrink: 0
    implicitWidth: Tk.Theme.size.border + separator.margin * 2
    implicitHeight: separator.inset * 2 + Tk.Theme.size.border

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: separator.inset
        anchors.bottomMargin: separator.inset
        width: Tk.Theme.size.border
        color: Tk.Theme.color.divider
    }
}
