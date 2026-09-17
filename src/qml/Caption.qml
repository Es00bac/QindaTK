// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// 10px secondary text (the original's text-[10px] / text-xs helpers):
// hints, readouts, list metadata. Muted by default.
Tk.Label {
    muted: true
    font.pixelSize: Tk.Theme.font.caption
}
