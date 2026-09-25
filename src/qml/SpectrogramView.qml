// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the host supplies bounded, already-computed tiles and exact
// sample boundaries plus FFT metadata. This view draws them and requests regions;
// it does not run FFT, interpret normalization, or apply spectral processing.
// The reference host is Tk.AudioSpectrogramSource (tiles, boundaries, metadata)
// plus AudioSession.edits.preview/commitPreview for processRequested
// (docs/audio-editing.md, D-136/D-137/D-186). The committed selection and the
// committed mask are host state in sample/Hz coordinates, so they keep their
// place through zoom. processRequested's `parameters` ({mask} for a mask) are
// merged into the edit map as they are.
T.Control {
    id: root

    property string sourceId: ""
    property string channelId: ""
    property var tiles: [] // bounded {x0, x1, lowHz, highHz, intensity}; normalized x and 0..1 intensity
    property var sampleBoundaries: [] // exact decimal strings, one per horizontal column boundary
    property real minimumHz: 20
    property real maximumHz: 20000
    property string frequencyScale: "log"
    property string fftWindow: ""
    property string fftNormalization: ""
    property int fftSize: 0
    property int hopSize: 0
    property string statusText: ""
    property string tooltip: ""
    property bool selectionEnabled: true
    // "rectangle" (time × band), "lasso" (one freehand polygon) or "paint" (brush dabs added to the mask).
    property string selectionMode: "rectangle"
    property real brushRadius: Tk.Theme.size.grip
    // Committed region, drawn at any zoom: exact samples and Hz.
    property string selectionStartSample: ""
    property string selectionEndSample: ""
    property real selectionLowHz: 0
    property real selectionHighHz: 0
    // Committed mask: [[{sample, hz}, …], …], one polygon per lasso or brush dab.
    property var maskPolygons: []
    readonly property int maskPointLimit: 4096 // AudioDsp::kMaxMaskPoints
    property string _gestureId: ""
    property int _gestureSerial: 0
    property real _startX: 0
    property real _startY: 0
    property real _endX: 0
    property real _endY: 0
    property var _path: [] // lasso vertices or paint dab centres, in view pixels
    readonly property int tileCount: root.tiles.length

    signal selectionPreviewRequested(string gestureId, string sourceId, string channelId,
                                     string startSample, string endSample,
                                     real lowHz, real highHz)
    signal selectionCommitted(string gestureId, string sourceId, string channelId,
                              string startSample, string endSample, real lowHz, real highHz)
    signal maskCommitted(string gestureId, string sourceId, string channelId, var polygons,
                         string startSample, string endSample, real lowHz, real highHz)
    signal gestureCanceled(string gestureId)
    signal processRequested(string gestureId, string sourceId, string channelId, string operation,
                            string startSample, string endSample, real lowHz, real highHz, var parameters)
    signal zoomRequested(string anchorSample, real anchorHz, real factor)
    signal scrollRequested(real timeFraction, real frequencyFraction)

    objectName: "spectrogramView"
    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.waveformHeight * 3
    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Spectrogram view")
    Accessible.description: root.statusText.length ? root.statusText
        : qsTr("%1 point FFT, %2 window, %3 hop, %4 normalization. R selects rectangles, L draws a lasso, B paints a mask; Delete removes the selected region or mask.")
          .arg(root.fftSize).arg(root.fftWindow).arg(root.hopSize).arg(root.fftNormalization)
    activeFocusOnTab: true
    focus: true

    function boundaryIndex(x) {
        return Math.max(0, Math.min(root.sampleBoundaries.length - 1,
                                    Math.floor(Math.max(0, Math.min(0.999999,
                                         x / Math.max(1, root.width)))
                                               * Math.max(0, root.sampleBoundaries.length - 1))))
    }
    function sampleAt(x) {
        if (!root.sampleBoundaries.length) return ""
        return String(root.sampleBoundaries[root.boundaryIndex(x)])
    }
    function sampleEndAt(x) {
        if (root.sampleBoundaries.length < 2) return ""
        const index = Math.min(root.sampleBoundaries.length - 1, root.boundaryIndex(x) + 1)
        return String(root.sampleBoundaries[index])
    }
    // Exact boundary strings first; otherwise linear between the first and last.
    function fractionForSample(sample) {
        const count = root.sampleBoundaries.length
        if (count < 2) return 0
        const wanted = String(sample)
        for (let i = 0; i < count; ++i)
            if (String(root.sampleBoundaries[i]) === wanted) return i / (count - 1)
        const first = Number(root.sampleBoundaries[0])
        const last = Number(root.sampleBoundaries[count - 1])
        if (!(last > first)) return 0
        return Math.max(0, Math.min(1, (Number(wanted) - first) / (last - first)))
    }
    // Decimal sample strings stay exact past 2^53: add a small offset digit by digit.
    function addSamples(text, offset) {
        const value = Number(text)
        if (Number.isSafeInteger(value) && Number.isSafeInteger(value + offset)) return String(value + offset)
        const digits = String(text)
        let out = "", carry = offset
        for (let i = digits.length - 1; i >= 0 || carry > 0; --i) {
            const d = (i >= 0 ? Number(digits[i]) : 0) + carry
            out = String(d % 10) + out
            carry = Math.floor(d / 10)
        }
        return out
    }
    function compareSamples(a, b) {
        const x = String(a), y = String(b)
        return x.length !== y.length ? x.length - y.length : (x < y ? -1 : x > y ? 1 : 0)
    }
    // The sample under x, interpolated inside its column from the exact left boundary.
    function sampleAtX(x) {
        const count = root.sampleBoundaries.length
        if (count < 2) return ""
        const position = Math.max(0, Math.min(1, x / Math.max(1, root.width))) * (count - 1)
        const i = Math.min(count - 2, Math.floor(position))
        const span = Number(root.sampleBoundaries[i + 1]) - Number(root.sampleBoundaries[i])
        return root.addSamples(root.sampleBoundaries[i], Math.max(0, Math.round((position - i) * span)))
    }
    readonly property bool hasSelection: root.selectionStartSample.length > 0
        && root.selectionEndSample.length > 0 && root.selectionStartSample !== root.selectionEndSample
        && root.selectionHighHz > root.selectionLowHz
    readonly property bool hasMask: root.maskPolygons.length > 0
    // {startSample, endSample, lowHz, highHz} bounding the polygons.
    function maskExtent(polygons) {
        let start = "", end = "", low = Infinity, high = -Infinity
        for (const polygon of polygons)
            for (const point of polygon) {
                const s = String(point.sample)
                if (!start.length || root.compareSamples(s, start) < 0) start = s
                if (!end.length || root.compareSamples(s, end) > 0) end = s
                low = Math.min(low, Number(point.hz))
                high = Math.max(high, Number(point.hz))
            }
        return { startSample: start, endSample: end, lowHz: low, highHz: high }
    }
    // The committed mask when there is one, otherwise the committed rectangle.
    function requestProcess(operation) {
        if (!root.enabled || !root.selectionEnabled || !(root.hasMask || root.hasSelection)) return false
        const id = "spectral-process-" + (++root._gestureSerial).toString()
        if (root.hasMask) {
            const e = root.maskExtent(root.maskPolygons)
            root.processRequested(id, root.sourceId, root.channelId, operation, e.startSample, e.endSample,
                                  e.lowHz, e.highHz, { mask: root.maskPolygons })
        } else {
            root.processRequested(id, root.sourceId, root.channelId, operation, root.selectionStartSample,
                                  root.selectionEndSample, root.selectionLowHz, root.selectionHighHz, {})
        }
        return true
    }
    function frequencyAt(y) {
        const fraction = 1 - Math.max(0, Math.min(1, y / Math.max(1, root.height)))
        if (root.frequencyScale === "log" && root.minimumHz > 0 && root.maximumHz > root.minimumHz)
            return root.minimumHz * Math.pow(root.maximumHz / root.minimumHz, fraction)
        return root.minimumHz + (root.maximumHz - root.minimumHz) * fraction
    }
    function maskPoint(x, y) { return { sample: root.sampleAtX(x), hz: root.frequencyAt(y) } }
    // One brush dab: an octagon of brushRadius pixels around (x, y).
    function dabPolygon(x, y) {
        const points = []
        for (let k = 0; k < 8; ++k) {
            const a = Math.PI * 2 * k / 8
            points.push(root.maskPoint(Math.max(0, Math.min(root.width, x + root.brushRadius * Math.cos(a))),
                                       Math.max(0, Math.min(root.height, y + root.brushRadius * Math.sin(a)))))
        }
        return points
    }
    function _pathCapacity() {
        let used = 0
        for (const polygon of root.maskPolygons) used += polygon.length
        return root.selectionMode === "paint" ? Math.floor((root.maskPointLimit - used) / 8) : root.maskPointLimit
    }
    function beginSelectionAt(x, y) {
        if (!root.enabled || !root.selectionEnabled || root.sampleBoundaries.length < 2
                || root._gestureId.length)
            return false
        root._gestureId = "spectral-gesture-" + (++root._gestureSerial).toString()
        root._startX = root._endX = Math.max(0, Math.min(root.width, x))
        root._startY = root._endY = Math.max(0, Math.min(root.height, y))
        root._path = []
        updateSelectionAt(x, y)
        return true
    }
    function updateSelectionAt(x, y) {
        if (!root._gestureId.length) return false
        root._endX = Math.max(0, Math.min(root.width, x))
        root._endY = Math.max(0, Math.min(root.height, y))
        if (root.selectionMode !== "rectangle") {
            // Lasso vertices every space.sm pixels; dabs every brush radius.
            const spacing = root.selectionMode === "paint" ? root.brushRadius : Tk.Theme.space.sm
            const last = root._path.length ? root._path[root._path.length - 1] : null
            if ((!last || Math.hypot(root._endX - last.x, root._endY - last.y) >= spacing)
                    && root._path.length < root._pathCapacity())
                root._path = root._path.concat([{ x: root._endX, y: root._endY }])
            spectralCanvas.requestPaint()
            return true
        }
        const start = root.sampleAt(Math.min(root._startX, root._endX))
        const end = root.sampleEndAt(Math.max(root._startX, root._endX))
        const low = Math.min(root.frequencyAt(root._startY), root.frequencyAt(root._endY))
        const high = Math.max(root.frequencyAt(root._startY), root.frequencyAt(root._endY))
        root.selectionPreviewRequested(root._gestureId, root.sourceId, root.channelId, start, end, low, high)
        spectralCanvas.requestPaint()
        return true
    }
    function finishSelection() {
        if (!root._gestureId.length) return false
        const id = root._gestureId
        root._gestureId = ""
        spectralCanvas.requestPaint()
        if (root.selectionMode !== "rectangle") {
            const path = root._path
            root._path = []
            let polygons = []
            if (root.selectionMode === "lasso") {
                if (path.length < 3) { root.gestureCanceled(id); return false }
                polygons = [path.map(p => root.maskPoint(p.x, p.y))]
            } else {
                if (!path.length) { root.gestureCanceled(id); return false }
                polygons = root.maskPolygons.concat(path.map(p => root.dabPolygon(p.x, p.y)))
            }
            const e = root.maskExtent(polygons)
            root.maskCommitted(id, root.sourceId, root.channelId, polygons, e.startSample, e.endSample,
                               e.lowHz, e.highHz)
            return true
        }
        const start = root.sampleAt(Math.min(root._startX, root._endX))
        const end = root.sampleEndAt(Math.max(root._startX, root._endX))
        const low = Math.min(root.frequencyAt(root._startY), root.frequencyAt(root._endY))
        const high = Math.max(root.frequencyAt(root._startY), root.frequencyAt(root._endY))
        root.selectionCommitted(id, root.sourceId, root.channelId, start, end, low, high)
        return true
    }
    function cancelSelection() {
        if (!root._gestureId.length) return false
        const id = root._gestureId
        root._gestureId = ""
        root._path = []
        root.gestureCanceled(id)
        spectralCanvas.requestPaint()
        return true
    }
    // Where a committed mask vertex sits now, in canvas pixels (follows zoom).
    function maskPointPosition(polygonIndex, pointIndex) {
        const point = root.maskPolygons[polygonIndex][pointIndex]
        return Qt.point(root.fractionForSample(point.sample) * spectralCanvas.width,
                        root.yForFrequency(Number(point.hz)))
    }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Canvas {
        id: spectralCanvas
        objectName: "spectrogramTileCanvas"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = root.css(Tk.Theme.color.inputBg)
            ctx.fillRect(0, 0, width, height)
            // Hard cap is defensive; the host's provider remains responsible for bounded requests.
            const count = Math.min(root.tiles.length, 12000)
            for (let i = 0; i < count; ++i) {
                const tile = root.tiles[i]
                const x0 = Math.max(0, Math.min(1, Number(tile.x0))) * width
                const x1 = Math.max(0, Math.min(1, Number(tile.x1))) * width
                const y0 = root.yForFrequency(Number(tile.highHz))
                const y1 = root.yForFrequency(Number(tile.lowHz))
                const intensity = Math.max(0, Math.min(1, Number(tile.intensity) || 0))
                ctx.fillStyle = root.css(Tk.Theme.color.info)
                ctx.globalAlpha = intensity * intensity * Tk.Theme.opacity.muted
                ctx.fillRect(x0, y0, Math.max(Tk.Theme.size.border, x1 - x0), Math.max(Tk.Theme.size.border, y1 - y0))
            }
            ctx.globalAlpha = Tk.Theme.opacity.full
            ctx.fillStyle = root.css(Tk.Theme.color.panelAlt)
            ctx.globalAlpha = Tk.Theme.opacity.muted
            ctx.fillRect(0, 0, Tk.Theme.size.fieldWidth, Tk.Theme.size.row)
            ctx.fillRect(0, height - Tk.Theme.size.row, Tk.Theme.size.fieldWidth, Tk.Theme.size.row)
            ctx.globalAlpha = Tk.Theme.opacity.full
            ctx.font = Tk.Theme.font.micro + "px \"" + Tk.Theme.font.monoFamily + "\""
            ctx.fillStyle = root.css(Tk.Theme.color.textMuted)
            ctx.textBaseline = "top"
            ctx.fillText(root.formatHz(root.maximumHz), Tk.Theme.space.xs, Tk.Theme.space.xs)
            ctx.textBaseline = "bottom"
            ctx.fillText(root.formatHz(root.minimumHz), Tk.Theme.space.xs, height - Tk.Theme.space.xs)
            // Committed mask polygons, mapped from sample/Hz at the current zoom.
            ctx.fillStyle = root.css(Tk.Theme.color.selection)
            ctx.strokeStyle = root.css(Tk.Theme.color.focus)
            ctx.lineWidth = Tk.Theme.size.border
            for (const polygon of root.maskPolygons) {
                ctx.beginPath()
                for (let p = 0; p < polygon.length; ++p) {
                    const x = root.fractionForSample(polygon[p].sample) * width
                    const y = root.yForFrequency(Number(polygon[p].hz))
                    if (p === 0) ctx.moveTo(x, y)
                    else ctx.lineTo(x, y)
                }
                ctx.closePath()
                ctx.globalAlpha = Tk.Theme.opacity.ghost
                ctx.fill()
                ctx.globalAlpha = Tk.Theme.opacity.full
                ctx.stroke()
            }
            if (!root._gestureId.length) return
            ctx.fillStyle = root.css(Tk.Theme.color.selection)
            ctx.globalAlpha = Tk.Theme.opacity.muted
            if (root.selectionMode === "rectangle") {
                const x = Math.min(root._startX, root._endX), y = Math.min(root._startY, root._endY)
                ctx.fillRect(x, y, Math.max(Tk.Theme.size.border, Math.abs(root._endX - root._startX)),
                             Math.max(Tk.Theme.size.border, Math.abs(root._endY - root._startY)))
            } else if (root.selectionMode === "lasso" && root._path.length) {
                ctx.beginPath()
                ctx.moveTo(root._path[0].x, root._path[0].y)
                for (let i = 1; i < root._path.length; ++i) ctx.lineTo(root._path[i].x, root._path[i].y)
                ctx.closePath()
                ctx.fill()
            } else {
                for (const dab of root._path) {
                    ctx.beginPath()
                    ctx.arc(dab.x, dab.y, root.brushRadius, 0, Math.PI * 2)
                    ctx.fill()
                }
            }
            ctx.globalAlpha = Tk.Theme.opacity.full
        }
        // The committed rectangle, placed from its sample/Hz coordinates at the current zoom.
        Rectangle {
            id: selectionOverlay
            objectName: "spectrogramSelection"
            readonly property real f0: root.fractionForSample(root.selectionStartSample)
            readonly property real f1: root.fractionForSample(root.selectionEndSample)
            visible: root.hasSelection && !root.hasMask && !root._gestureId.length
            x: Math.min(f0, f1) * spectralCanvas.width
            width: Math.max(Tk.Theme.size.border, Math.abs(f1 - f0) * spectralCanvas.width)
            y: root.yForFrequency(root.selectionHighHz)
            height: Math.max(Tk.Theme.size.border, root.yForFrequency(root.selectionLowHz) - y)
            color: "transparent"
            border.color: Tk.Theme.color.focus
            border.width: Tk.Theme.size.border
        }
    }
    function yForFrequency(hz) {
        let fraction = 0
        if (root.frequencyScale === "log" && root.minimumHz > 0 && root.maximumHz > root.minimumHz)
            fraction = Math.log(Math.max(root.minimumHz, hz) / root.minimumHz) / Math.log(root.maximumHz / root.minimumHz)
        else
            fraction = (hz - root.minimumHz) / Math.max(1, root.maximumHz - root.minimumHz)
        return (1 - Math.max(0, Math.min(1, fraction))) * spectralCanvas.height
    }
    function formatHz(hz) { return hz >= 1000 ? (hz / 1000).toFixed(1) + " kHz" : Math.round(hz) + " Hz" }
    function css(c) { return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")" }
    // Everything the canvas reads; any change repaints it.
    readonly property var _paintInputs: [root.tiles, root.sampleBoundaries, root.width, root.height,
        root.minimumHz, root.maximumHz, root.frequencyScale, root.maskPolygons]
    on_PaintInputsChanged: spectralCanvas.requestPaint()
    DragHandler {
        target: null
        enabled: root.enabled && root.selectionEnabled
        onActiveChanged: {
            if (active) root.beginSelectionAt(centroid.pressPosition.x, centroid.pressPosition.y)
            else if (root._gestureId.length) root.finishSelection()
        }
        onCentroidChanged: if (active) root.updateSelectionAt(centroid.position.x, centroid.position.y)
    }
    WheelHandler {
        target: null
        onWheel: function(event) {
            const sample = root.sampleAt(event.x)
            const frequency = root.frequencyAt(event.y)
            if (event.modifiers & Qt.ControlModifier)
                root.zoomRequested(sample, frequency, event.angleDelta.y > 0 ? 1.25 : 0.8)
            else
                root.scrollRequested(event.angleDelta.y > 0 ? -0.1 : 0.1,
                                     event.angleDelta.x > 0 ? 0.1 : 0)
            event.accepted = true
        }
    }
    Keys.onEscapePressed: event => { root.cancelSelection(); event.accepted = true }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) root.requestProcess("spectralGain")
        else if (event.key === Qt.Key_R) root.selectionMode = "rectangle"
        else if (event.key === Qt.Key_L) root.selectionMode = "lasso"
        else if (event.key === Qt.Key_B) root.selectionMode = "paint"
        else return
        event.accepted = true
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
