// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Body text in the theme's face and colour. Elides when a layout narrows
// it; set `wrapMode` to wrap instead. `muted`/`disabled`/`accent` switch
// the colour role so call sites never name a colour.
Text {
    id: label

    property bool muted: false
    property bool disabled: false
    property bool accent: false
    property bool mono: false
    property bool selectable: false

    color: disabled ? Tk.Theme.color.textDisabled
         : accent ? Tk.Theme.color.accentText
         : muted ? Tk.Theme.color.textMuted
         : Tk.Theme.color.text
    font.family: mono ? Tk.Theme.font.monoFamily : Tk.Theme.font.family
    font.pixelSize: Tk.Theme.font.body
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
    renderType: Text.NativeRendering
    textFormat: Text.PlainText
}
