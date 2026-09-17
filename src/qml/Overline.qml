// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The uppercase, letter-spaced, semibold caption that heads every grouped
// block of a Sloom panel: 10px, 0.18em tracking, muted. The typographic
// signature of the dense style; use it for section titles and panel
// headers, never for body copy.
Tk.Label {
    id: overline

    property string title
    property bool wide: false

    text: overline.title.length > 0 ? overline.title.toUpperCase() : ""
    muted: true
    font.pixelSize: Tk.Theme.font.caption
    font.weight: Font.DemiBold
    font.letterSpacing: overline.wide ? Tk.Theme.font.trackingWider : Tk.Theme.font.trackingWide
    font.capitalization: Font.AllUppercase
}
