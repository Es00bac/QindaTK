// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the host owns source samples, edits and sample indexing. `columns`
// carries bounded extrema and exact decimal sample boundaries; this view only
// requests a selection or edit gesture and never changes the source snapshot.
T.Control {
    id: root

    property string sourceId: ""
    property string channelId: ""
    property var columns: [] // {minimum, maximum, startSample, endSample}
    property var regions: [] // {id, startSample, endSample, label}
    property var rulerMarks: [] // {fraction, label}
    property string selectionStartSample: "0"
    property string selectionEndSample: "0"
    property string tooltip: ""
    property string statusText: ""
    property bool editingEnabled: true
    readonly property string viewState: root.columns.length ? "ready" : "empty"
    property string _gestureId: ""
    property int _gestureSerial: 0
    property real _gestureStartX: 0
    property real _gestureEndX: 0

    signal selectionPreviewRequested(string gestureId, string sourceId, string channelId,
                                     string startSample, string endSample)
    signal selectionCommitted(string gestureId, string sourceId, string channelId,
                              string startSample, string endSample)
    signal editRequested(string gestureId, string sourceId, string channelId, string operation,
                         string startSample, string endSample)
    signal gestureCanceled(string gestureId)
    signal zoomRequested(string anchorSample, real factor)
    signal scrollRequested(real fractionDelta)

    objectName: "waveformEditor"
    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.waveformHeight * 2
    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Waveform editor")
    Accessible.description: root.statusText.length ? root.statusText : qsTr("Audio waveform and sample selection. F requests fade, G requests gain, Delete requests trim.")
    activeFocusOnTab: true
    focus: true

    function fractionForSample(sample) {
        if (!root.columns.length) return 0
        const wanted = String(sample)
        for (let i = 0; i < root.columns.length; ++i) {
            if (String(root.columns[i].startSample) === wanted) return i / root.columns.length
            if (String(root.columns[i].endSample) === wanted) return (i + 1) / root.columns.length
        }
        return 0
    }
    function boundaryAt(x) {
        if (!root.columns.length)
            return root.selectionStartSample
        const fraction = Math.max(0, Math.min(0.999999, x / Math.max(1, root.width)))
        const index = Math.min(root.columns.length - 1, Math.floor(fraction * root.columns.length))
        return String(root.columns[index].startSample)
    }
    function beginSelectionAt(x) {
        if (!root.enabled || !root.editingEnabled || !root.columns.length || root._gestureId.length)
            return false
        root._gestureId = "waveform-gesture-" + (++root._gestureSerial).toString()
        root._gestureStartX = Math.max(0, Math.min(root.width, x))
        root._gestureEndX = root._gestureStartX
        updateSelectionAt(x)
        return true
    }
    function updateSelectionAt(x) {
        if (!root._gestureId.length)
            return false
        root._gestureEndX = Math.max(0, Math.min(root.width, x))
        const a = root.boundaryAt(Math.min(root._gestureStartX, root._gestureEndX))
        const b = root.boundaryAt(Math.max(root._gestureStartX, root._gestureEndX))
        const endIndex = Math.min(root.columns.length - 1,
                                  Math.floor(Math.max(0, Math.min(0.999999,
                                      Math.max(root._gestureStartX, root._gestureEndX)
                                      / Math.max(1, root.width))) * root.columns.length))
        const end = String(root.columns[endIndex].endSample || b)
        root.selectionPreviewRequested(root._gestureId, root.sourceId, root.channelId, a, end)
        selectionPaint.requestPaint()
        return true
    }
    function finishSelection() {
        if (!root._gestureId.length)
            return false
        const id = root._gestureId
        const left = Math.min(root._gestureStartX, root._gestureEndX)
        const right = Math.max(root._gestureStartX, root._gestureEndX)
        const start = root.boundaryAt(left)
        const last = Math.min(root.columns.length - 1,
                              Math.floor(Math.max(0, Math.min(0.999999, right / Math.max(1, root.width)))
                                         * root.columns.length))
        const end = String(root.columns[last].endSample || root.boundaryAt(right))
        root._gestureId = ""
        root.selectionCommitted(id, root.sourceId, root.channelId, start, end)
        selectionPaint.requestPaint()
        return true
    }
    function cancelSelection() {
        if (!root._gestureId.length)
            return false
        const id = root._gestureId
        root._gestureId = ""
        root.gestureCanceled(id)
        selectionPaint.requestPaint()
        return true
    }
    function requestEdit(operation) {
        if (!root.enabled || !root.editingEnabled || !root.columns.length)
            return false
        const id = "waveform-edit-" + (++root._gestureSerial).toString()
        root.editRequested(id, root.sourceId, root.channelId, operation,
                           root.selectionStartSample, root.selectionEndSample)
        return true
    }
    function cancelActiveGesture() { return root.cancelSelection() }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }

    Canvas {
        id: selectionPaint
        objectName: "waveformSelectionCanvas"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            const data = root.columns
            const bandHeight = Math.max(1, h - Tk.Theme.size.row)
            ctx.fillStyle = root.toCss(Tk.Theme.color.inputBg)
            ctx.fillRect(0, 0, w, bandHeight)
            for (let r = 0; r < root.regions.length; ++r) {
                const region = root.regions[r]
                const rx0 = root.fractionForSample(region.startSample) * w
                const rx1 = root.fractionForSample(region.endSample) * w
                ctx.fillStyle = root.toCss(Tk.Theme.color.selection)
                ctx.globalAlpha = Tk.Theme.opacity.muted
                ctx.fillRect(rx0, 0, Math.max(Tk.Theme.size.border, rx1 - rx0), bandHeight)
                ctx.globalAlpha = Tk.Theme.opacity.full
                if (region.label) {
                    ctx.font = Tk.Theme.font.micro + "px " + Tk.Theme.font.family
                    ctx.fillStyle = root.toCss(Tk.Theme.color.text)
                    ctx.textBaseline = "top"
                    ctx.fillText(String(region.label), rx0 + Tk.Theme.space.xs, Tk.Theme.space.xs)
                }
            }
            if (data.length) {
                ctx.strokeStyle = root.toCss(Tk.Theme.color.info)
                ctx.globalAlpha = Tk.Theme.opacity.muted
                ctx.lineWidth = Tk.Theme.size.border
                const step = Math.max(1, Math.ceil(data.length / Math.max(1, w)))
                for (let i = 0; i < data.length; i += step) {
                    const item = data[i]
                    const x = ((i + 0.5) / data.length) * w
                    const lo = Math.max(-1, Math.min(1, Number(item.minimum) || 0))
                    const hi = Math.max(-1, Math.min(1, Number(item.maximum) || 0))
                    ctx.beginPath()
                    ctx.moveTo(x, bandHeight * (0.5 - hi * 0.46))
                    ctx.lineTo(x, bandHeight * (0.5 - lo * 0.46))
                    ctx.stroke()
                }
                ctx.globalAlpha = Tk.Theme.opacity.full
            }
            ctx.font = Tk.Theme.font.micro + "px \"" + Tk.Theme.font.monoFamily + "\""
            ctx.fillStyle = root.toCss(Tk.Theme.color.textMuted)
            ctx.textBaseline = "top"
            for (let i = 0; i < root.rulerMarks.length; ++i) {
                const mark = root.rulerMarks[i]
                const x = Math.max(0, Math.min(w, Number(mark.fraction) * w))
                ctx.strokeStyle = root.toCss(Tk.Theme.color.divider)
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, bandHeight); ctx.stroke()
                ctx.fillText(String(mark.label), x + Tk.Theme.space.xs, bandHeight + Tk.Theme.space.xs)
            }
            if (root._gestureId.length) {
                const x0 = Math.min(root._gestureStartX, root._gestureEndX) / Math.max(1, root.width) * w
                const x1 = Math.max(root._gestureStartX, root._gestureEndX) / Math.max(1, root.width) * w
                ctx.fillStyle = root.toCss(Tk.Theme.color.selection)
                ctx.globalAlpha = Tk.Theme.opacity.muted
                ctx.fillRect(x0, 0, Math.max(Tk.Theme.size.border, x1 - x0), bandHeight)
                ctx.globalAlpha = Tk.Theme.opacity.full
            }
        }
    }
    function toCss(c) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
               + Math.round(c.b * 255) + "," + c.a + ")"
    }
    onColumnsChanged: selectionPaint.requestPaint()
    onRegionsChanged: selectionPaint.requestPaint()
    onRulerMarksChanged: selectionPaint.requestPaint()
    onSelectionStartSampleChanged: selectionPaint.requestPaint()
    onSelectionEndSampleChanged: selectionPaint.requestPaint()
    onWidthChanged: selectionPaint.requestPaint()
    onHeightChanged: selectionPaint.requestPaint()

    DragHandler {
        id: selectionDrag
        objectName: "waveformSelectionDrag"
        target: null
        enabled: root.enabled && root.editingEnabled
        onActiveChanged: {
            if (active)
                root.beginSelectionAt(centroid.position.x)
            else if (root._gestureId.length)
                root.finishSelection()
        }
        onCentroidChanged: if (active) root.updateSelectionAt(centroid.position.x)
    }
    WheelHandler {
        target: null
        onWheel: function(event) {
            const anchor = root.boundaryAt(event.x)
            if (event.modifiers & Qt.ControlModifier)
                root.zoomRequested(anchor, event.angleDelta.y > 0 ? 1.25 : 0.8)
            else
                root.scrollRequested(event.angleDelta.y > 0 ? -0.1 : 0.1)
            event.accepted = true
        }
    }
    Keys.onEscapePressed: event => { root.cancelSelection(); event.accepted = true }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            root.scrollRequested(event.key === Qt.Key_Left ? -0.05 : 0.05)
            event.accepted = true
        } else if (event.key === Qt.Key_F) {
            root.requestEdit("fade")
            event.accepted = true
        } else if (event.key === Qt.Key_G) {
            root.requestEdit("gain")
            event.accepted = true
        } else if (event.key === Qt.Key_Delete) {
            root.requestEdit("trim")
            event.accepted = true
        }
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
