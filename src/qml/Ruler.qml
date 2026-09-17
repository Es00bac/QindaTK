// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: a tick ruler for canvases and timelines. `origin` is the
// pixel position (along the ruler) of unit 0, `pixelsPerUnit` the pixels per unit;
// major ticks every `majorEvery` units carry micro labels, minor ticks
// every `minorEvery`. `cursorPosition` (px, -1 none) draws the accent
// cursor line. It repaints when the theme changes so colours never go
// stale. 22px thick.
Item {
    id: ruler

    property int orientation: Qt.Horizontal
    property real origin: 0
    property real pixelsPerUnit: 10
    property string unit: ""
    property real majorEvery: 10
    property real minorEvery: 1
    property bool labels: true
    property real cursorPosition: -1
    property real minimumMinorSpacing: 4

    readonly property bool horizontal: ruler.orientation === Qt.Horizontal

    implicitWidth: horizontal ? 200 : Tk.Theme.size.row
    implicitHeight: horizontal ? Tk.Theme.size.row : 200

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Ruler")

    function css(c) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
               + Math.round(c.b * 255) + "," + c.a + ")"
    }
    function format(value) {
        const rounded = Math.round(value * 100) / 100
        return (Number.isInteger(rounded) ? rounded.toString() : rounded.toFixed(2))
               + (ruler.unit.length > 0 && ruler.horizontal ? ruler.unit : "")
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.surface
        Rectangle {
            visible: ruler.horizontal
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Tk.Theme.color.divider
        }
        Rectangle {
            visible: !ruler.horizontal
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 1
            color: Tk.Theme.color.divider
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            if (ruler.pixelsPerUnit <= 0 || ruler.minorEvery <= 0) {
                return
            }
            const length = ruler.horizontal ? width : height
            const thickness = ruler.horizontal ? height : width
            const tickColor = ruler.css(Tk.Theme.color.textMuted)
            const labelColor = ruler.css(Tk.Theme.color.textMuted)
            ctx.strokeStyle = tickColor
            ctx.fillStyle = labelColor
            ctx.lineWidth = 1
            ctx.font = Tk.Theme.font.micro + "px \"" + Tk.Theme.font.monoFamily + "\""
            ctx.textBaseline = "top"
            // Thin out minor ticks that would land closer than the minimum.
            let minor = ruler.minorEvery
            while (minor * ruler.pixelsPerUnit < ruler.minimumMinorSpacing) {
                minor *= 2
            }
            const major = Math.max(ruler.majorEvery, minor)
            const first = Math.floor((0 - ruler.origin) / ruler.pixelsPerUnit / minor) * minor
            const last = Math.ceil((length - ruler.origin) / ruler.pixelsPerUnit / minor) * minor
            ctx.beginPath()
            for (let u = first; u <= last; u += minor) {
                const p = Math.round(ruler.origin + u * ruler.pixelsPerUnit) + 0.5
                const isMajor = Math.abs(u / major - Math.round(u / major)) < 1e-6
                const len = isMajor ? thickness * 0.55 : thickness * 0.25
                if (ruler.horizontal) {
                    ctx.moveTo(p, thickness)
                    ctx.lineTo(p, thickness - len)
                } else {
                    ctx.moveTo(thickness, p)
                    ctx.lineTo(thickness - len, p)
                }
                if (isMajor && ruler.labels) {
                    if (ruler.horizontal) {
                        ctx.fillText(ruler.format(u), p + 2, 1)
                    } else {
                        ctx.save()
                        ctx.translate(2, p - 2)
                        ctx.rotate(-Math.PI / 2)
                        ctx.fillText(ruler.format(u), 0, 0)
                        ctx.restore()
                    }
                }
            }
            ctx.stroke()
        }
    }

    Rectangle {
        visible: ruler.cursorPosition >= 0
        x: ruler.horizontal ? Math.round(ruler.cursorPosition) : 0
        y: ruler.horizontal ? 0 : Math.round(ruler.cursorPosition)
        width: ruler.horizontal ? 1 : parent.width
        height: ruler.horizontal ? parent.height : 1
        color: Tk.Theme.color.accent
    }

    onOriginChanged: canvas.requestPaint()
    onScaleChanged: canvas.requestPaint()
    onMajorEveryChanged: canvas.requestPaint()
    onMinorEveryChanged: canvas.requestPaint()
    onLabelsChanged: canvas.requestPaint()
    onUnitChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Connections {
        target: Tk.Theme
        function onChanged() { canvas.requestPaint() }
    }
}
