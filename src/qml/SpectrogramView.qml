// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the host supplies bounded, already-computed tiles and exact
// sample boundaries plus FFT metadata. This view draws them and requests regions;
// it does not run FFT, interpret normalization, or apply spectral processing.
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
    property string _gestureId: ""
    property int _gestureSerial: 0
    property real _startX: 0
    property real _startY: 0
    property real _endX: 0
    property real _endY: 0
    readonly property int tileCount: root.tiles.length

    signal selectionPreviewRequested(string gestureId, string sourceId, string channelId,
                                     string startSample, string endSample,
                                     real lowHz, real highHz)
    signal selectionCommitted(string gestureId, string sourceId, string channelId,
                              string startSample, string endSample, real lowHz, real highHz)
    signal gestureCanceled(string gestureId)
    signal zoomRequested(string anchorSample, real anchorHz, real factor)
    signal scrollRequested(real timeFraction, real frequencyFraction)

    objectName: "spectrogramView"
    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.waveformHeight * 3
    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Spectrogram view")
    Accessible.description: root.statusText.length ? root.statusText
        : qsTr("%1 point FFT, %2 window, %3 hop, %4 normalization")
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
    function frequencyAt(y) {
        const fraction = 1 - Math.max(0, Math.min(1, y / Math.max(1, root.height)))
        if (root.frequencyScale === "log" && root.minimumHz > 0 && root.maximumHz > root.minimumHz)
            return root.minimumHz * Math.pow(root.maximumHz / root.minimumHz, fraction)
        return root.minimumHz + (root.maximumHz - root.minimumHz) * fraction
    }
    function beginSelectionAt(x, y) {
        if (!root.enabled || !root.selectionEnabled || root.sampleBoundaries.length < 2
                || root._gestureId.length)
            return false
        root._gestureId = "spectral-gesture-" + (++root._gestureSerial).toString()
        root._startX = root._endX = Math.max(0, Math.min(root.width, x))
        root._startY = root._endY = Math.max(0, Math.min(root.height, y))
        updateSelectionAt(x, y)
        return true
    }
    function updateSelectionAt(x, y) {
        if (!root._gestureId.length) return false
        root._endX = Math.max(0, Math.min(root.width, x))
        root._endY = Math.max(0, Math.min(root.height, y))
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
        const start = root.sampleAt(Math.min(root._startX, root._endX))
        const end = root.sampleEndAt(Math.max(root._startX, root._endX))
        const low = Math.min(root.frequencyAt(root._startY), root.frequencyAt(root._endY))
        const high = Math.max(root.frequencyAt(root._startY), root.frequencyAt(root._endY))
        root._gestureId = ""
        root.selectionCommitted(id, root.sourceId, root.channelId, start, end, low, high)
        spectralCanvas.requestPaint()
        return true
    }
    function cancelSelection() {
        if (!root._gestureId.length) return false
        const id = root._gestureId
        root._gestureId = ""
        root.gestureCanceled(id)
        spectralCanvas.requestPaint()
        return true
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
            if (root._gestureId.length) {
                const x = Math.min(root._startX, root._endX), y = Math.min(root._startY, root._endY)
                ctx.fillStyle = root.css(Tk.Theme.color.selection)
                ctx.globalAlpha = Tk.Theme.opacity.muted
                ctx.fillRect(x, y, Math.max(Tk.Theme.size.border, Math.abs(root._endX - root._startX)),
                             Math.max(Tk.Theme.size.border, Math.abs(root._endY - root._startY)))
                ctx.globalAlpha = Tk.Theme.opacity.full
            }
        }
    }
    function yForFrequency(hz) {
        let fraction = 0
        if (root.frequencyScale === "log" && root.minimumHz > 0 && root.maximumHz > root.minimumHz)
            fraction = Math.log(Math.max(root.minimumHz, hz) / root.minimumHz) / Math.log(root.maximumHz / root.minimumHz)
        else
            fraction = (hz - root.minimumHz) / Math.max(1, root.maximumHz - root.minimumHz)
        return (1 - Math.max(0, Math.min(1, fraction))) * height
    }
    function formatHz(hz) { return hz >= 1000 ? (hz / 1000).toFixed(1) + " kHz" : Math.round(hz) + " Hz" }
    function css(c) { return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")" }
    onTilesChanged: spectralCanvas.requestPaint()
    onSampleBoundariesChanged: spectralCanvas.requestPaint()
    onWidthChanged: spectralCanvas.requestPaint()
    onHeightChanged: spectralCanvas.requestPaint()
    onMinimumHzChanged: spectralCanvas.requestPaint()
    onMaximumHzChanged: spectralCanvas.requestPaint()
    onFrequencyScaleChanged: spectralCanvas.requestPaint()
    DragHandler {
        target: null
        enabled: root.enabled && root.selectionEnabled
        onActiveChanged: {
            if (active) root.beginSelectionAt(centroid.position.x, centroid.position.y)
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
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
