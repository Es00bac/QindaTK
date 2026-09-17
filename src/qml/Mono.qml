// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Monospace readout text: timecodes, coordinates, hashes, values that
// must line up in columns.
Tk.Label {
    mono: true
    font.pixelSize: Tk.Theme.font.small
}
