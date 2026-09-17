// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a 14px rotating arc in the accent
// colour (Lucide's loader-circle glyph). Stops rotating, but stays visible,
// when reduced motion zeroes the motion ladder.
T.BusyIndicator {
    id: control
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property real size: Tk.Theme.size.icon
    property color color: Tk.Theme.color.accent
    property string tooltip: ""

    implicitWidth: control.size
    implicitHeight: control.size
    padding: 0

    Accessible.role: Accessible.Indicator
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Busy")

    contentItem: Tk.Icon {
        name: "loader-circle"
        size: control.size
        color: control.color
        strokeWidth: 2.4
        visible: control.running || control.visible

        RotationAnimation on rotation {
            running: control.running && control.visible && Tk.Theme.motion.slow > 0
            from: 0
            to: 360
            duration: Tk.Theme.motion.slow * 4
            loops: Animation.Infinite
        }
    }
}
