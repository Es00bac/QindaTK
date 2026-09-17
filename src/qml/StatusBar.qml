// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The 22px strip along the bottom of a window: caption-sized fields in a
// row on the surface colour under a 1px top rule. Put a Tk.Spacer between
// the left group and the right group.
Tk.Box {
    id: bar

    default property alias fields: row.data
    property real gap: Tk.Theme.space.md

    color: Tk.Theme.color.surface
    borderTop: 1
    borderColor: Tk.Theme.color.divider
    implicitHeight: Tk.Theme.size.statusBar
    implicitWidth: row.implicitWidth + Tk.Theme.space.sm * 2

    Accessible.role: Accessible.StatusBar

    Tk.Flex {
        id: row
        direction: Tk.Flex.Row
        // Fields fill the 21px content box (22 minus the top rule) and
        // centre their own contents, so nothing sits a pixel high.
        align: Tk.Flex.Stretch
        gap: bar.gap
        paddingLeft: Tk.Theme.space.sm
        paddingRight: Tk.Theme.space.sm
    }
}
