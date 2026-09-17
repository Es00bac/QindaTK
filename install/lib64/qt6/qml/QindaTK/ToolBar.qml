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
    // Wrap onto further rows instead of overflowing: a command is never
    // hidden for want of width. The bar grows by whole rows.
    property bool wrap: false
    readonly property real baseHeight: compact ? Tk.Theme.size.controlLg : Tk.Theme.size.toolbar

    color: Tk.Theme.color.surface
    borderColor: Tk.Theme.color.divider
    borderBottom: edge === "bottom" ? Tk.Theme.size.border : 0
    borderTop: edge === "top" ? Tk.Theme.size.border : 0
    borderLeft: 0
    borderRight: 0
    implicitHeight: bar.wrap ? Math.max(bar.baseHeight, row.implicitHeight) : bar.baseHeight
    implicitWidth: row.implicitWidth

    Tk.Flex {
        id: row
        direction: Tk.Flex.Row
        wrap: bar.wrap ? Tk.Flex.Wrap : Tk.Flex.NoWrap
        align: Tk.Flex.Center
        alignContent: Tk.Flex.Center
        gap: bar.gap
        rowGap: Tk.Theme.space.xs
        paddingLeft: Tk.Theme.space.sm
        paddingRight: Tk.Theme.space.sm
        paddingTop: bar.wrap ? Tk.Theme.space.xs : 0
        paddingBottom: bar.wrap ? Tk.Theme.space.xs : 0
    }
}
