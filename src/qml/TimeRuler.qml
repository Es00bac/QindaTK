// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A time scale whose tick density follows the zoom, not the caller.
//
// AGENT-NOTE: every timeline that hand-rolls a ruler ends up with ticks that
// are unreadably dense when zoomed out and comically sparse when zoomed in,
// because the step is a constant somebody picked at one zoom level. The step
// here is chosen from a 1/2/5/10 ladder against the pixels available per
// label, which is the rule that keeps a chart axis readable at any scale.
//
// AGENT-CONTRACT: `pixelsPerUnit` and `originUnits` are the caller's scroll
// state; this draws them and never owns them. A ruler that kept its own
// scroll position would drift from the track area beneath it, and the drift
// is invisible until someone measures a cut against it.
Item {
    id: root

    property real pixelsPerUnit: 24
    property real originUnits: 0
    // Where the playhead sits, in units. Negative hides it.
    property real playheadUnits: -1
    // Turns a unit value into its label. Seconds by default; a caller working
    // in frames or pages supplies its own.
    property var formatUnit: function(units) {
        return Math.round(units) + "s"
    }
    property string tooltip: ""

    signal scrubbed(real units)

    objectName: "timeRuler"
    implicitHeight: Tk.Theme.size.timeRuler
    implicitWidth: Tk.Theme.size.panelMinWidth
    clip: true

    Accessible.role: Accessible.Slider
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : qsTr("Time ruler")

    // The 1/2/5/10 ladder: the smallest step whose labels still have room.
    readonly property real step: {
        const wanted = Tk.Theme.size.labelWidth / Math.max(0.0001, root.pixelsPerUnit)
        const magnitude = Math.pow(10, Math.floor(Math.log(Math.max(wanted, 1e-6)) / Math.LN10))
        const candidates = [1, 2, 5, 10]
        for (let i = 0; i < candidates.length; ++i) {
            const candidate = candidates[i] * magnitude
            if (candidate >= wanted) {
                return candidate
            }
        }
        return 10 * magnitude
    }

    onPixelsPerUnitChanged: ticks.requestPaint()
    onOriginUnitsChanged: ticks.requestPaint()
    onWidthChanged: ticks.requestPaint()
    onHeightChanged: ticks.requestPaint()
    onStepChanged: ticks.requestPaint()

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.surface
    }

    Canvas {
        id: ticks
        objectName: "timeRulerTicks"
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const pxPerUnit = Math.max(0.0001, root.pixelsPerUnit)
            const step = root.step
            if (step <= 0) {
                return
            }
            ctx.strokeStyle = Tk.Theme.color.border
            ctx.fillStyle = Tk.Theme.color.textMuted
            ctx.lineWidth = Tk.Theme.size.border
            ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family

            const first = Math.floor(root.originUnits / step) * step
            const lastUnit = root.originUnits + width / pxPerUnit
            for (let unit = first; unit <= lastUnit; unit += step) {
                const x = Math.round((unit - root.originUnits) * pxPerUnit) + 0.5
                ctx.beginPath()
                ctx.moveTo(x, height * 0.5)
                ctx.lineTo(x, height)
                ctx.stroke()
                ctx.fillText(root.formatUnit(unit), x + Tk.Theme.space.xs,
                             height * 0.5 - Tk.Theme.space.xs)
            }
        }
    }

    Rectangle {
        objectName: "timeRulerPlayhead"
        visible: root.playheadUnits >= 0
        width: Tk.Theme.size.border
        height: parent.height
        color: Tk.Theme.color.danger
        x: (root.playheadUnits - root.originUnits) * root.pixelsPerUnit
    }

    TapHandler {
        onTapped: function(point) {
            root.scrubbed(root.originUnits + point.position.x / Math.max(0.0001, root.pixelsPerUnit))
        }
    }
    DragHandler {
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        onCentroidChanged: {
            if (active) {
                root.scrubbed(root.originUnits
                              + centroid.position.x / Math.max(0.0001, root.pixelsPerUnit))
            }
        }
    }
}
