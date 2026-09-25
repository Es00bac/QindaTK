// SPDX-License-Identifier: LGPL-3.0-or-later
// Evidence scene: the gallery's Inputs and Structure sections on their own,
// so the NumberField echo handlers and the elided ComboBox label are in view.
import QtQuick
import QindaTK as Tk
import "../../../../examples/gallery"

Rectangle {
    width: 1280
    height: 800
    color: Tk.Theme.color.bg
    Tk.Scroll {
        anchors.fill: parent
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            padding: Tk.Theme.space.md
            InputsSection {}
            StructureSection {}
        }
    }
}
