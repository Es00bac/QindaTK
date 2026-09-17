// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A raised content card: the panelAlt surface with a 1px border and a 6px
// radius, md padding. `interactive` adds hover/pressed tinting and the
// `clicked` signal (from Box); `selected` paints the accent border.
Tk.Box {
    id: card

    property bool selected: false

    color: card.interactive && card.pressed ? Tk.Theme.mix(Tk.Theme.color.panelAlt, Tk.Theme.color.accent, 0.08)
         : card.interactive && card.hovered ? Tk.Theme.mix(Tk.Theme.color.panelAlt, Tk.Theme.color.accent, 0.04)
         : Tk.Theme.color.panelAlt
    borderWidth: 1
    borderColor: card.selected ? Tk.Theme.color.accent
               : card.interactive && card.hovered ? Tk.Theme.color.controlHoverBorder
               : Tk.Theme.color.border
    radius: Tk.Theme.radius.md
    padding: Tk.Theme.space.md

    Accessible.role: card.interactive ? Accessible.Button : Accessible.Grouping
}
