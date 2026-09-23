// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The compact 22px strip along the bottom of a window: caption-sized fields
// in a row on the surface colour under a 1px top rule. Touch keeps the same
// content rule and reserves its themed control target below it.
Tk.Box {
    id: bar

    default property alias fields: row.data
    property real gap: Tk.Theme.space.md

    color: Tk.Theme.color.surface
    borderTop: 1
    borderColor: Tk.Theme.color.divider
    // The top rule sits outside the Touch content target. Compact keeps its
    // measured 22px band; Touch includes the themed top rule above the target.
    implicitHeight: Tk.Density.modeName === "touch"
                    ? Tk.Theme.size.control + edgeTop : Tk.Theme.size.statusBar
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
