// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A single-series Graph sized for a table cell or a label row: no grid, no
// baseline, no axis -- just the shape of a history. Feed it with append().
//
// AGENT-NOTE: this is a wrapper rather than its own C++ item so that the one
// plotting implementation (and its two drawing paths) serves both sizes. A
// row's sparkline and a panel's full graph cannot disagree about a trace.
Tk.Graph {
    id: root

    // Convenience passthroughs so a caller never reaches into series[0].
    property alias color: line.color
    property alias ramp: line.ramp
    property alias fill: line.fill
    property alias fillOpacity: line.fillOpacity
    property alias lineWidth: line.lineWidth
    property alias values: line.values
    readonly property alias last: line.last
    readonly property alias peak: line.peak

    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.sparklineHeight

    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip
    property string tooltip: ""

    Tk.GraphSeries {
        id: line
        color: Tk.Theme.color.accent
        fillOpacity: 0.35
        lineWidth: 1
    }
}
