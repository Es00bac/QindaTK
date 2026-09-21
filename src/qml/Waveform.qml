// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// An audio envelope: peak magnitude per column, mirrored about a centre line.
//
// AGENT-NOTE: this draws its own shape rather than wrapping Tk.Graph, which
// was tried first and does not fit. Graph fills from the plot EDGE --
// GraphMesh::baseline() returns `plot.mirrored ? 0.0 : plot.height` -- so two
// mirrored series fill to the top and bottom rather than meeting at zero, and
// a quiet clip renders as tall as a loud one. That is exactly the judgement an
// editor reads a waveform to make, so the wrapper was abandoned rather than
// papered over. Giving Graph a fill-to-zero mode would be the better toolkit
// answer, but it means changing two rendering paths that existing consumers
// already depend on.
//
// AGENT-CONTRACT: `peaks` are magnitudes in 0..1, one per column, already
// reduced by the caller. A waveform must be summarised at the width it is
// drawn; handing this a million samples to reduce on every repaint is the
// mistake this contract exists to prevent.
Item {
    id: root

    property var peaks: []
    property color color: Tk.Theme.color.info
    // Drawn behind the envelope so a silent passage still reads as audio
    // rather than as an empty lane.
    property bool showCentreLine: true
    property string tooltip: ""

    readonly property bool empty: root.peaks === undefined || root.peaks === null
                                  || root.peaks.length === 0

    objectName: "waveform"
    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.waveformHeight

    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : qsTr("Audio waveform")

    onPeaksChanged: shape.requestPaint()
    onColorChanged: shape.requestPaint()
    onWidthChanged: shape.requestPaint()
    onHeightChanged: shape.requestPaint()

    Rectangle {
        objectName: "waveformCentreLine"
        visible: root.showCentreLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: Tk.Theme.size.border
        color: Tk.Theme.color.divider
    }

    Canvas {
        id: shape
        objectName: "waveformShape"
        anchors.fill: parent
        visible: !root.empty

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const source = root.peaks
            if (source === undefined || source === null || source.length === 0) {
                return
            }
            const mid = height / 2
            const columns = source.length
            // One column per pixel at most: more samples than pixels cannot be
            // drawn, and asking the canvas to try is how a long clip becomes a
            // slow repaint.
            const step = Math.max(1, Math.ceil(columns / Math.max(1, width)))

            ctx.beginPath()
            ctx.moveTo(0, mid)
            // Out along the top of the envelope...
            for (let index = 0; index < columns; index += step) {
                const x = (index / (columns - 1)) * width
                const peak = Math.min(1, Math.abs(Number(source[index]) || 0))
                ctx.lineTo(x, mid - peak * mid)
            }
            // ...and back along the bottom, so the shape closes on itself and
            // is symmetric because it is one path, not two drawings that have
            // to agree.
            for (let index = columns - 1; index >= 0; index -= step) {
                const x = (index / (columns - 1)) * width
                const peak = Math.min(1, Math.abs(Number(source[index]) || 0))
                ctx.lineTo(x, mid + peak * mid)
            }
            ctx.closePath()
            ctx.fillStyle = root.color
            ctx.globalAlpha = Tk.Theme.opacity.muted
            ctx.fill()
            ctx.globalAlpha = 1.0
            ctx.strokeStyle = root.color
            ctx.lineWidth = Tk.Theme.size.border
            ctx.stroke()
        }
    }

    Tk.ToolTip {
        visible: waveHover.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
    HoverHandler { id: waveHover }
}
