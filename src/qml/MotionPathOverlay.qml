// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: host supplies the composition, sample times and stable key IDs; this control only displays positions and emits selection requests.
T.Control {
    id: control
    objectName: "motionPathOverlay"
    property Tk.MotionComposition composition: null
    property string layerId: ""
    // Host chooses sample cadence; decimal tick strings are passed through unchanged.
    property var sampleTicks: []
    // Optional stable key snapshots: [{id, ticks}]. Selection is only a request.
    property var keyPoints: []
    property string selectedKeyId: ""
    property var pathPoints: []
    property string tooltip: qsTr("Sampled motion path")
    signal keySelectionRequested(string layerId, string keyId, string timeTicks)
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.waveformHeight * 2
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip
    Accessible.description: pathPoints.length ? qsTr("%1 evaluated positions").arg(pathPoints.length) : qsTr("No evaluated path samples")
    function rebuild() {
        const result = []
        if (composition && layerId.length) {
            for (let i = 0; i < sampleTicks.length && i < 512; ++i) {
                const ticks = String(sampleTicks[i])
                const evaluated = composition.evaluate(ticks)
                if (!evaluated.ok) continue
                const layer = evaluated.layers.find(function(row) { return row.layerId === layerId })
                if (layer && layer.matrix && layer.matrix.length === 6)
                    result.push({ticks: ticks, x: Number(layer.matrix[4]), y: Number(layer.matrix[5])})
            }
        }
        pathPoints = result
        plot.requestPaint()
    }
    function mapPoint(point) {
        if (pathPoints.length < 2) return Qt.point(width / 2, height / 2)
        let minX = pathPoints[0].x, maxX = minX, minY = pathPoints[0].y, maxY = minY
        for (let i = 1; i < pathPoints.length; ++i) {
            minX = Math.min(minX, pathPoints[i].x); maxX = Math.max(maxX, pathPoints[i].x)
            minY = Math.min(minY, pathPoints[i].y); maxY = Math.max(maxY, pathPoints[i].y)
        }
        const inset = Tk.Theme.space.md
        return Qt.point(inset + (point.x - minX) / Math.max(1, maxX - minX) * Math.max(1, plot.width - inset * 2),
                        plot.height - inset - (point.y - minY) / Math.max(1, maxY - minY) * Math.max(1, plot.height - inset * 2))
    }
    function requestNearestKeyAt(x, y) {
        let best = null, distance = Infinity
        for (let i = 0; i < keyPoints.length; ++i) {
            const key = keyPoints[i]
            const sample = pathPoints.find(function(p) { return p.ticks === String(key.ticks) })
            if (!sample) continue
            const p = mapPoint(sample), d = Math.hypot(p.x - x, p.y - y)
            if (d < distance) { best = key; distance = d }
        }
        if (!best || distance >= Tk.Theme.size.controlHeight) return false
        selectedKeyId = String(best.id)
        keySelectionRequested(layerId, String(best.id), String(best.ticks))
        return true
    }
    onCompositionChanged: rebuild()
    onLayerIdChanged: rebuild()
    onSampleTicksChanged: rebuild()
    HoverHandler { id: hover }
    Tk.ToolTip { visible: hover.hovered && control.tooltip.length > 0; text: control.tooltip }
    background: Rectangle { color: Tk.Theme.color.canvas; border.color: Tk.Theme.color.border; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.sm }
    Canvas {
        id: plot
        objectName: "motionPathCanvas"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (control.pathPoints.length < 2) return
            ctx.strokeStyle = Tk.Theme.color.accent
            ctx.lineWidth = Tk.Theme.size.border * 2
            ctx.beginPath()
            for (let i = 0; i < control.pathPoints.length; ++i) {
                const p = control.mapPoint(control.pathPoints[i])
                if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y)
            }
            ctx.stroke()
            for (let j = 0; j < control.keyPoints.length; ++j) {
                const key = control.keyPoints[j]
                const sample = control.pathPoints.find(function(p) { return p.ticks === String(key.ticks) })
                if (!sample) continue
                const at = control.mapPoint(sample)
                ctx.fillStyle = Tk.Theme.color.selection
                ctx.beginPath(); ctx.arc(at.x, at.y, Tk.Theme.size.iconSmall / 3, 0, Math.PI * 2); ctx.fill()
            }
        }
    }
    TapHandler {
        onTapped: function(eventPoint) {
            control.requestNearestKeyAt(eventPoint.position.x, eventPoint.position.y)
        }
    }
    Keys.onPressed: function(event) {
        if (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right) return
        if (!keyPoints.length) return
        let index = keyPoints.findIndex(function(k) { return String(k.id) === control.selectedKeyId })
        if (index < 0) index = event.key === Qt.Key_Right ? -1 : keyPoints.length
        index = Math.max(0, Math.min(keyPoints.length - 1, index + (event.key === Qt.Key_Right ? 1 : -1)))
        const next = keyPoints[index]
        selectedKeyId = String(next.id)
        keySelectionRequested(layerId, String(next.id), String(next.ticks))
        event.accepted = true
    }
}
