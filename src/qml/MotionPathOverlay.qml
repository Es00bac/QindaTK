// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: host supplies the composition, sample times and stable key
// IDs (keyPoints ticks are composition ticks listed in sampleTicks). A key or
// tangent drag calls MotionComposition.movePathKey/setPathTangent once, at
// release: one AutomationModel transaction per gesture. Selection is a request.
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
    // World frame size in pixels. When set, world space maps into the
    // letterboxed frame exactly as CompositionView draws it; otherwise the
    // sampled path is fitted to the control (y down in both cases).
    property int frameWidth: 0
    property int frameHeight: 0
    property bool editingEnabled: true
    property var pathPoints: []
    // pathHandles() of the selected key: {position, inHandle, outHandle} in world pixels.
    property var handles: ({})
    property var dragState: null
    property string statusText: ""
    property string tooltip: qsTr("Sampled motion path")
    readonly property var worldView: {
        if (frameWidth > 0 && frameHeight > 0) {
            const s = Math.max(0, Math.min(availableWidth / frameWidth, availableHeight / frameHeight))
            return {x: leftPadding + (availableWidth - frameWidth * s) / 2,
                    y: topPadding + (availableHeight - frameHeight * s) / 2, minX: 0, minY: 0, sx: s, sy: s}
        }
        if (pathPoints.length < 2) {
            const only = pathPoints.length ? pathPoints[0] : Qt.point(0, 0)
            return {x: width / 2, y: height / 2, minX: only.x, minY: only.y, sx: 1, sy: 1}
        }
        let minX = pathPoints[0].x, maxX = minX, minY = pathPoints[0].y, maxY = minY
        for (let i = 1; i < pathPoints.length; ++i) {
            minX = Math.min(minX, pathPoints[i].x); maxX = Math.max(maxX, pathPoints[i].x)
            minY = Math.min(minY, pathPoints[i].y); maxY = Math.max(maxY, pathPoints[i].y)
        }
        const inset = Tk.Theme.space.md
        return {x: inset, y: inset, minX: minX, minY: minY,
                sx: Math.max(1, width - inset * 2) / Math.max(1, maxX - minX),
                sy: Math.max(1, height - inset * 2) / Math.max(1, maxY - minY)}
    }
    signal keySelectionRequested(string layerId, string keyId, string timeTicks)
    signal pathEditCommitted(string transactionId)
    signal pathEditRejected(string reason)
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.waveformHeight * 2
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip
    Accessible.description: (pathPoints.length ? qsTr("%1 evaluated positions").arg(pathPoints.length)
        : qsTr("No evaluated path samples")) + (statusText.length ? " · " + statusText : "")
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
        refreshHandles()
    }
    function keyById(id) {
        for (let i = 0; i < keyPoints.length; ++i)
            if (String(keyPoints[i].id) === String(id)) return keyPoints[i]
        return null
    }
    function samplePoint(key) {
        return pathPoints.find(function(p) { return p.ticks === String(key.ticks) }) || null
    }
    function refreshHandles() {
        const key = keyById(selectedKeyId)
        const result = composition && key && layerId.length ? composition.pathHandles(layerId, String(key.ticks)) : null
        handles = result && result.ok === true ? result : ({})
        plot.requestPaint()
    }
    function mapPoint(point) {
        const v = worldView
        return Qt.point(v.x + (point.x - v.minX) * v.sx, v.y + (point.y - v.minY) * v.sy)
    }
    function unmapPoint(x, y) {
        const v = worldView
        return Qt.point(v.minX + (x - v.x) / v.sx, v.minY + (y - v.y) / v.sy)
    }
    function requestNearestKeyAt(x, y) {
        let best = null, distance = Infinity
        for (let i = 0; i < keyPoints.length; ++i) {
            const sample = samplePoint(keyPoints[i])
            if (!sample) continue
            const p = mapPoint(sample), d = Math.hypot(p.x - x, p.y - y)
            if (d < distance) { best = keyPoints[i]; distance = d }
        }
        if (!best || distance >= Tk.Theme.size.control) return false
        selectedKeyId = String(best.id)
        keySelectionRequested(layerId, String(best.id), String(best.ticks))
        return true
    }
    // Tangent handles of the selected key win over key points at equal reach.
    function hitTest(x, y) {
        let best = null, distance = Tk.Theme.size.control
        const key = keyById(selectedKeyId)
        const sides = ["in", "out"]
        for (let s = 0; key && s < sides.length; ++s) {
            const handle = sides[s] === "in" ? handles.inHandle : handles.outHandle
            if (!handle) continue
            const p = mapPoint(handle), d = Math.hypot(p.x - x, p.y - y)
            if (d < distance) { best = {kind: sides[s], keyId: String(key.id), ticks: String(key.ticks)}; distance = d }
        }
        for (let i = 0; i < keyPoints.length; ++i) {
            const sample = samplePoint(keyPoints[i])
            if (!sample) continue
            const p = mapPoint(sample), d = Math.hypot(p.x - x, p.y - y)
            if (d < distance) { best = {kind: "key", keyId: String(keyPoints[i].id), ticks: String(keyPoints[i].ticks)}; distance = d }
        }
        return best
    }
    function beginDrag(x, y) {
        if (!editingEnabled || !composition || dragState) return false
        const hit = hitTest(x, y)
        if (!hit) return false
        if (hit.kind === "key" && hit.keyId !== selectedKeyId) {
            selectedKeyId = hit.keyId
            keySelectionRequested(layerId, hit.keyId, hit.ticks)
        }
        dragState = {kind: hit.kind, keyId: hit.keyId, ticks: hit.ticks, x: x, y: y}
        plot.requestPaint()
        return true
    }
    function updateDrag(x, y) {
        if (!dragState) return
        dragState = {kind: dragState.kind, keyId: dragState.keyId, ticks: dragState.ticks, x: x, y: y}
        plot.requestPaint()
    }
    // One gesture, one call: the composition records a single transaction.
    function finishDrag() {
        if (!dragState) return false
        const drag = dragState
        dragState = null
        const world = unmapPoint(drag.x, drag.y)
        const result = drag.kind === "key"
            ? composition.movePathKey(layerId, drag.ticks, world.x, world.y)
            : composition.setPathTangent(layerId, drag.ticks, drag.kind, world.x, world.y)
        if (!result || result.ok !== true) {
            statusText = result && result.error ? String(result.error) : qsTr("Path edit was rejected")
            pathEditRejected(statusText)
            plot.requestPaint()
            return false
        }
        statusText = drag.kind === "key" ? qsTr("Moved key at %1 ticks").arg(drag.ticks)
            : qsTr("Edited %1 tangent at %2 ticks").arg(drag.kind).arg(drag.ticks)
        rebuild()
        if (result.transactionId) pathEditCommitted(String(result.transactionId))
        return true
    }
    function cancelDrag() {
        if (!dragState) return false
        dragState = null
        plot.requestPaint()
        return true
    }
    onCompositionChanged: rebuild()
    onLayerIdChanged: rebuild()
    onSampleTicksChanged: rebuild()
    onSelectedKeyIdChanged: refreshHandles()
    onKeyPointsChanged: refreshHandles()
    onWorldViewChanged: plot.requestPaint()
    Connections {
        target: control.composition
        function onLayersChanged() { Qt.callLater(control.rebuild) }
    }
    Connections {
        target: control.composition ? control.composition.animationModel : null
        function onRevisionChanged() { Qt.callLater(control.rebuild) }
    }
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
            const radius = Tk.Theme.size.iconSm / 3
            ctx.strokeStyle = Tk.Theme.color.accent
            ctx.lineWidth = Tk.Theme.size.border * 2
            ctx.beginPath()
            for (let i = 0; i < control.pathPoints.length; ++i) {
                const p = control.mapPoint(control.pathPoints[i])
                if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y)
            }
            ctx.stroke()
            const key = control.keyById(control.selectedKeyId)
            const origin = key ? control.samplePoint(key) : null
            if (origin) {
                const at = control.mapPoint(origin)
                ctx.strokeStyle = Tk.Theme.color.textMuted
                ctx.lineWidth = Tk.Theme.size.border
                ctx.fillStyle = Tk.Theme.color.text
                const sides = ["inHandle", "outHandle"]
                for (let s = 0; s < sides.length; ++s) {
                    const handle = control.handles[sides[s]]
                    if (!handle) continue
                    const h = control.mapPoint(handle)
                    ctx.beginPath(); ctx.moveTo(at.x, at.y); ctx.lineTo(h.x, h.y); ctx.stroke()
                    ctx.fillRect(h.x - radius, h.y - radius, radius * 2, radius * 2)
                }
            }
            for (let j = 0; j < control.keyPoints.length; ++j) {
                const sample = control.samplePoint(control.keyPoints[j])
                if (!sample) continue
                const at = control.mapPoint(sample)
                ctx.fillStyle = String(control.keyPoints[j].id) === control.selectedKeyId
                    ? Tk.Theme.color.accent : Tk.Theme.color.selection
                ctx.beginPath(); ctx.arc(at.x, at.y, radius, 0, Math.PI * 2); ctx.fill()
            }
            if (control.dragState) {
                ctx.strokeStyle = Tk.Theme.color.focus
                ctx.lineWidth = Tk.Theme.size.border * 2
                ctx.beginPath(); ctx.arc(control.dragState.x, control.dragState.y, radius * 2, 0, Math.PI * 2); ctx.stroke()
            }
        }
    }
    TapHandler {
        onTapped: function(eventPoint) {
            control.requestNearestKeyAt(eventPoint.position.x, eventPoint.position.y)
        }
    }
    DragHandler {
        target: null
        enabled: control.editingEnabled && control.composition !== null
        onActiveChanged: {
            if (active) control.beginDrag(centroid.pressPosition.x, centroid.pressPosition.y)
            else control.finishDrag()
        }
        onCentroidChanged: if (active) control.updateDrag(centroid.position.x, centroid.position.y)
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) { event.accepted = cancelDrag(); return }
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
