// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A title line. `level` 1 is display size, 2 is title, 3 is large.
Tk.Label {
    id: heading

    property int level: 2

    font.pixelSize: level <= 1 ? Tk.Theme.font.display
                  : level === 2 ? Tk.Theme.font.title
                  : Tk.Theme.font.large
    font.weight: Font.DemiBold
}
