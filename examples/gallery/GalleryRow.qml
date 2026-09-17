// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-NOTE: named GalleryRow, not Row: an unqualified `Row` resolves to
// QtQuick's positioner and the collision is silent.
// A labelled, wrapping row of controls: the caption on the left names the
// variant being shown, the controls follow and wrap when the gallery is
// narrow.
Tk.Flex {
    id: row

    property string label: ""
    default property alias items: controls.data

    direction: Tk.Flex.Row
    align: Tk.Flex.Start
    gap: Tk.Theme.space.md

    Tk.Caption {
        text: row.label
        Tk.Flex.basis: 110
        Tk.Flex.shrink: 0
        Tk.Flex.alignSelf: Tk.Flex.Center
        visible: row.label.length > 0
    }
    Tk.Flex {
        id: controls
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
    }
}
