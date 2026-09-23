// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-042 visual fixture. A host would resolve requests against its scene.
Rectangle {
    width: 360
    height: 220
    color: Tk.Theme.color.bg

    Tk.AlignDistributePanel {
        objectName: "alignFixturePanel"
        x: Tk.Theme.space.sm
        y: Tk.Theme.space.sm
        width: parent.width - 2 * Tk.Theme.space.sm
        selectedIds: ["101", "204", "305"]
        referenceMode: "selection"
        spacing: 16
    }
}
