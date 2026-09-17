// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 36px chrome bar (compact: 28) on the
// surface colour with a 1px edge, children in a centred row with
// space.sm gaps. Put Chips, Buttons, Segmented, ToolSeparator and Spacer
// in it. `edge` draws the border on "bottom" (default), "top" or "none".
Tk.Box {
    id: bar

    default property alias items: row.data
    property bool compact: false
    property real gap: Tk.Theme.space.sm
    property string edge: "bottom"

    color: Tk.Theme.color.surface
    borderColor: Tk.Theme.color.divider
    borderBottom: edge === "bottom" ? Tk.Theme.size.border : 0
    borderTop: edge === "top" ? Tk.Theme.size.border : 0
    borderLeft: 0
    borderRight: 0
    implicitHeight: compact ? Tk.Theme.size.controlLg : Tk.Theme.size.toolbar
    implicitWidth: row.implicitWidth

    Tk.Flex {
        id: row
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: bar.gap
        paddingLeft: Tk.Theme.space.sm
        paddingRight: Tk.Theme.space.sm
    }
}
