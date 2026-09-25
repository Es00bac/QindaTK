// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the host owns source samples, edits and sample indexing. `columns`
// carries bounded extrema and exact decimal sample boundaries; this view only
// requests a selection or edit gesture and never changes the source snapshot.
// The reference host is Tk.AudioWaveformSource (columns, channelLayout,
// channelColumns, zoom, scroll) plus AudioSession.edits.apply for edit requests:
// editRequested's `parameters` ({gainDb} for gain, {shape} for fades) are the
// edit's own parameters and are merged into the apply() map as they are
// (docs/audio-editing.md, D-134/D-135/D-294).
T.Control {
    id: root

    property string sourceId: ""
    property string channelId: ""
    property var columns: [] // {minimum, maximum, startSample, endSample}
    // Multichannel: one band per {id, label}; channelColumns holds one columns
    // list per entry with the same boundaries as `columns`. Empty: one band.
    property var channelLayout: []
    property var channelColumns: []
    // Host state with a layout: the committed selection's channel id, "all" (or empty) for every channel.
    property string selectionChannelId: ""
    property var regions: [] // {id, startSample, endSample, label}
    property var rulerMarks: [] // {fraction, label}
    property string selectionStartSample: "0"
    property string selectionEndSample: "0"
    property string tooltip: ""
    property string statusText: ""
    property bool editingEnabled: true
    // The gain the G key and the gain handle request, and the fade curve of F / the fade handles.
    property real gainDb: 0.0
    property string fadeShape: "linear" // or "equalPower"
    property real gainDbPerPixel: 0.25
    readonly property real minimumGainDb: -96
    readonly property real maximumGainDb: 48
    readonly property string viewState: root.columns.length ? "ready" : "empty"
    readonly property int bandCount: Math.max(1, root.channelLayout.length)
    readonly property bool hasSelection: root.columns.length > 0
        && root.selectionEndSample !== root.selectionStartSample
    property string _gestureId: ""
    property int _gestureSerial: 0
    property real _gestureStartX: 0
    property real _gestureEndX: 0
    property int _gestureStartBand: 0
    property int _gestureEndBand: 0
    property string _handleDrag: "" // "gain", "fadeIn" or "fadeOut" while a handle moves
    property string _handleGestureId: ""
    property real _handleFraction: 0
    property real _gainAtDragStart: 0

    signal selectionPreviewRequested(string gestureId, string sourceId, string channelId,
                                     string startSample, string endSample)
    signal selectionCommitted(string gestureId, string sourceId, string channelId,
                              string startSample, string endSample)
    signal editRequested(string gestureId, string sourceId, string channelId, string operation,
                         string startSample, string endSample, var parameters)
    signal gestureCanceled(string gestureId)
    signal zoomRequested(string anchorSample, real factor)
    signal scrollRequested(real fractionDelta)

    objectName: "waveformEditor"
    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.waveformHeight * (1 + root.bandCount)
    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Waveform editor")
    Accessible.description: root.statusText.length ? root.statusText
        : qsTr("Audio waveform and sample selection. F fades in, Shift+F fades out, E switches the fade curve, Up and Down set the gain, G applies it, S silences, T crops to the selection and Delete removes it.")
    activeFocusOnTab: true
    focus: true

    // Exact column boundaries first (decimal strings survive beyond 2^53);
    // otherwise linear between the first and last boundary.
    function fractionForSample(sample) {
        if (!root.columns.length) return 0
        const wanted = String(sample)
        for (let i = 0; i < root.columns.length; ++i) {
            if (String(root.columns[i].startSample) === wanted) return i / root.columns.length
            if (String(root.columns[i].endSample) === wanted) return (i + 1) / root.columns.length
        }
        const first = Number(root.columns[0].startSample)
        const last = Number(root.columns[root.columns.length - 1].endSample)
        if (!(last > first)) return 0
        return Math.max(0, Math.min(1, (Number(wanted) - first) / (last - first)))
    }
    function boundaryAt(x) {
        if (!root.columns.length)
            return root.selectionStartSample
        const fraction = Math.max(0, Math.min(0.999999, x / Math.max(1, root.width)))
        const index = Math.min(root.columns.length - 1, Math.floor(fraction * root.columns.length))
        return String(root.columns[index].startSample)
    }
    // The exact column boundary nearest a 0…1 fraction of the waveform.
    function boundaryNearFraction(fraction) {
        const n = root.columns.length
        if (!n) return root.selectionStartSample
        const index = Math.max(0, Math.min(n, Math.round(Math.max(0, Math.min(1, fraction)) * n)))
        return index === n ? String(root.columns[n - 1].endSample) : String(root.columns[index].startSample)
    }
    function bandAt(y) {
        const local = y - selectionPaint.y
        const bands = Math.max(1, selectionPaint.height - Tk.Theme.size.row)
        return Math.max(0, Math.min(root.bandCount - 1, Math.floor(local / (bands / root.bandCount))))
    }
    function _gestureChannel() {
        if (!root.channelLayout.length) return root.channelId
        if (root._gestureStartBand !== root._gestureEndBand) return "all"
        return String(root.channelLayout[root._gestureStartBand].id)
    }
    function editChannel() {
        if (!root.channelLayout.length) return root.channelId
        return root.selectionChannelId.length ? root.selectionChannelId : "all"
    }
    function beginSelectionAt(x, y) {
        if (!root.enabled || !root.editingEnabled || !root.columns.length || root._gestureId.length
                || root._handleDrag.length)
            return false
        root._gestureId = "waveform-gesture-" + (++root._gestureSerial).toString()
        root._gestureStartX = Math.max(0, Math.min(root.width, x))
        root._gestureEndX = root._gestureStartX
        root._gestureStartBand = root._gestureEndBand = root.bandAt(y === undefined ? selectionPaint.y : y)
        updateSelectionAt(x, y)
        return true
    }
    function _selectionEnd(right) {
        const last = Math.min(root.columns.length - 1,
                              Math.floor(Math.max(0, Math.min(0.999999, right / Math.max(1, root.width)))
                                         * root.columns.length))
        return String(root.columns[last].endSample || root.boundaryAt(right))
    }
    function updateSelectionAt(x, y) {
        if (!root._gestureId.length)
            return false
        root._gestureEndX = Math.max(0, Math.min(root.width, x))
        if (y !== undefined) root._gestureEndBand = root.bandAt(y)
        const start = root.boundaryAt(Math.min(root._gestureStartX, root._gestureEndX))
        const end = root._selectionEnd(Math.max(root._gestureStartX, root._gestureEndX))
        root.selectionPreviewRequested(root._gestureId, root.sourceId, root._gestureChannel(), start, end)
        selectionPaint.requestPaint()
        return true
    }
    function finishSelection() {
        if (!root._gestureId.length)
            return false
        const id = root._gestureId
        const start = root.boundaryAt(Math.min(root._gestureStartX, root._gestureEndX))
        const end = root._selectionEnd(Math.max(root._gestureStartX, root._gestureEndX))
        const channel = root._gestureChannel()
        root._gestureId = ""
        root.selectionCommitted(id, root.sourceId, channel, start, end)
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
    function _emitEdit(id, operation, start, end, parameters) {
        root.editRequested(id, root.sourceId, root.editChannel(), operation, start, end, parameters)
        return true
    }
    // Requests `operation` over the committed selection. Gain carries
    // {gainDb} (the gainDb property unless given) and is not requested at
    // 0 dB, which would change nothing; fades carry {shape}.
    function requestEdit(operation, parameters) {
        if (!root.enabled || !root.editingEnabled || !root.columns.length)
            return false
        const params = Object.assign({}, parameters || {})
        if (operation === "gain") {
            if (params.gainDb === undefined) params.gainDb = root.gainDb
            if (!(Math.abs(params.gainDb) > 0)) return false
        } else if ((operation === "fadeIn" || operation === "fadeOut") && params.shape === undefined) {
            params.shape = root.fadeShape
        }
        return root._emitEdit("waveform-edit-" + (++root._gestureSerial).toString(), operation,
                              root.selectionStartSample, root.selectionEndSample, params)
    }
    function setGainDb(value) {
        root.gainDb = Math.round(Math.max(root.minimumGainDb, Math.min(root.maximumGainDb, value)) * 10) / 10
    }
    // Handle gestures: "gain" moves vertically (gainDbPerPixel), "fadeIn" and
    // "fadeOut" move horizontally inside the selection and snap to boundaries.
    function beginHandleDrag(kind) {
        if (!root.enabled || !root.editingEnabled || !root.hasSelection || root._gestureId.length
                || root._handleDrag.length)
            return false
        root._handleGestureId = "waveform-handle-" + (++root._gestureSerial).toString()
        root._handleDrag = kind
        root._gainAtDragStart = root.gainDb
        root._handleFraction = kind === "fadeOut" ? root.fractionForSample(root.selectionEndSample)
                                                  : root.fractionForSample(root.selectionStartSample)
        selectionPaint.requestPaint()
        return true
    }
    function updateHandleDrag(fraction, deltaY) {
        if (!root._handleDrag.length) return false
        if (root._handleDrag === "gain") {
            root.setGainDb(root._gainAtDragStart - deltaY * root.gainDbPerPixel)
        } else {
            const lo = root.fractionForSample(root.selectionStartSample)
            const hi = root.fractionForSample(root.selectionEndSample)
            root._handleFraction = Math.max(Math.min(lo, hi), Math.min(Math.max(lo, hi), fraction))
        }
        selectionPaint.requestPaint()
        return true
    }
    function finishHandleDrag() {
        if (!root._handleDrag.length) return false
        const kind = root._handleDrag
        const id = root._handleGestureId
        root._handleDrag = ""
        root._handleGestureId = ""
        selectionPaint.requestPaint()
        if (kind === "gain") {
            if (!(Math.abs(root.gainDb - root._gainAtDragStart) > 0) || !(Math.abs(root.gainDb) > 0)) return false
            return root._emitEdit(id, "gain", root.selectionStartSample, root.selectionEndSample,
                                  { gainDb: root.gainDb })
        }
        const boundary = root.boundaryNearFraction(root._handleFraction)
        const start = kind === "fadeIn" ? root.selectionStartSample : boundary
        const end = kind === "fadeIn" ? boundary : root.selectionEndSample
        if (start === end || root.fractionForSample(end) <= root.fractionForSample(start)) return false
        return root._emitEdit(id, kind, start, end, { shape: root.fadeShape })
    }
    function cancelHandleDrag() {
        if (!root._handleDrag.length) return false
        const id = root._handleGestureId
        if (root._handleDrag === "gain") root.gainDb = root._gainAtDragStart
        root._handleDrag = ""
        root._handleGestureId = ""
        root.gestureCanceled(id)
        selectionPaint.requestPaint()
        return true
    }
    function cancelActiveGesture() { return root.cancelHandleDrag() || root.cancelSelection() }
    // The factor a pending handle gesture applies at `fraction`, drawn as a preview.
    function previewFactor(fraction) {
        if (!root._handleDrag.length) return 1
        const lo = root.fractionForSample(root.selectionStartSample)
        const hi = root.fractionForSample(root.selectionEndSample)
        if (fraction < lo || fraction > hi) return 1
        if (root._handleDrag === "gain") return Math.pow(10, root.gainDb / 20)
        const shape = x => root.fadeShape === "equalPower" ? Math.sin(Math.PI / 2 * x) : x
        if (root._handleDrag === "fadeIn")
            return fraction >= root._handleFraction ? 1
                : shape((fraction - lo) / Math.max(1e-9, root._handleFraction - lo))
        return fraction <= root._handleFraction ? 1
            : shape((hi - fraction) / Math.max(1e-9, hi - root._handleFraction))
    }
    function formatGain(db) { return (db > 0 ? "+" : "") + db.toFixed(1) + " dB" }

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
        readonly property real bandsHeight: Math.max(1, height - Tk.Theme.size.row)
        readonly property real bandHeight: bandsHeight / root.bandCount
        readonly property real selectionX0: Math.min(root.fractionForSample(root.selectionStartSample),
                                                     root.fractionForSample(root.selectionEndSample)) * width
        readonly property real selectionX1: Math.max(root.fractionForSample(root.selectionStartSample),
                                                     root.fractionForSample(root.selectionEndSample)) * width
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, bands = bandsHeight, bandH = bandHeight
            const layout = root.channelLayout
            const selectedBand = layout.length && root.selectionChannelId.length && root.selectionChannelId !== "all"
                ? layout.findIndex(entry => String(entry.id) === root.selectionChannelId) : -1
            for (let b = 0; b < root.bandCount; ++b) {
                const top = b * bandH
                const data = layout.length ? (root.channelColumns[b] || []) : root.columns
                ctx.fillStyle = root.toCss(Tk.Theme.color.inputBg)
                ctx.fillRect(0, top, w, bandH - (b + 1 < root.bandCount ? Tk.Theme.size.border : 0))
                if (root.hasSelection && (selectedBand < 0 || selectedBand === b)) {
                    ctx.fillStyle = root.toCss(Tk.Theme.color.selection)
                    ctx.globalAlpha = Tk.Theme.opacity.muted
                    ctx.fillRect(selectionX0, top, Math.max(Tk.Theme.size.border, selectionX1 - selectionX0), bandH)
                    ctx.globalAlpha = Tk.Theme.opacity.full
                }
                if (data.length) {
                    ctx.strokeStyle = root.toCss(Tk.Theme.color.info)
                    ctx.globalAlpha = Tk.Theme.opacity.muted
                    ctx.lineWidth = Tk.Theme.size.border
                    const scaled = (i, v) => {
                        const factor = (selectedBand < 0 || selectedBand === b)
                            ? root.previewFactor((i + 0.5) / data.length) : 1
                        return Math.max(-1, Math.min(1, (Number(v) || 0) * factor))
                    }
                    const yFor = v => top + bandH * (0.5 - v * 0.46)
                    if (data.length * 2 <= w) {
                        // Few columns (sample zoom): join the column values so single samples read as a line.
                        ctx.beginPath()
                        for (let i = 0; i < data.length; ++i) {
                            const x = ((i + 0.5) / data.length) * w
                            if (i === 0) ctx.moveTo(x, yFor(scaled(i, data[i].maximum)))
                            else ctx.lineTo(x, yFor(scaled(i, data[i].maximum)))
                        }
                        ctx.stroke()
                    }
                    const step = Math.max(1, Math.ceil(data.length / Math.max(1, w)))
                    for (let i = 0; i < data.length; i += step) {
                        const x = ((i + 0.5) / data.length) * w
                        const high = yFor(scaled(i, data[i].maximum))
                        ctx.beginPath()
                        ctx.moveTo(x, high)
                        ctx.lineTo(x, Math.max(yFor(scaled(i, data[i].minimum)), high + Tk.Theme.size.border))
                        ctx.stroke()
                    }
                    ctx.globalAlpha = Tk.Theme.opacity.full
                }
                if (layout.length) {
                    ctx.font = Tk.Theme.font.micro + "px " + Tk.Theme.font.family
                    ctx.fillStyle = root.toCss(Tk.Theme.color.textMuted)
                    ctx.textBaseline = "bottom"
                    ctx.fillText(String(layout[b].label || layout[b].id), Tk.Theme.space.xs,
                                 top + bandH - Tk.Theme.space.xs)
                }
            }
            for (let r = 0; r < root.regions.length; ++r) {
                const region = root.regions[r]
                const rx0 = root.fractionForSample(region.startSample) * w
                const rx1 = root.fractionForSample(region.endSample) * w
                ctx.fillStyle = root.toCss(Tk.Theme.color.selection)
                ctx.globalAlpha = Tk.Theme.opacity.muted
                ctx.fillRect(rx0, 0, Math.max(Tk.Theme.size.border, rx1 - rx0), bands)
                ctx.globalAlpha = Tk.Theme.opacity.full
                if (region.label) {
                    ctx.font = Tk.Theme.font.micro + "px " + Tk.Theme.font.family
                    ctx.fillStyle = root.toCss(Tk.Theme.color.text)
                    ctx.textBaseline = "top"
                    // Below the selection handles when they show, so a handle never covers the label.
                    ctx.fillText(String(region.label), rx0 + Tk.Theme.space.xs, gainHandle.visible
                                 ? gainHandle.y + gainHandle.height + Tk.Theme.space.xs : Tk.Theme.space.xs)
                }
            }
            if (root._handleDrag === "fadeIn" || root._handleDrag === "fadeOut") {
                // The pending fade curve over the whole band stack.
                const fadeIn = root._handleDrag === "fadeIn"
                const a = fadeIn ? selectionX0 : root._handleFraction * w
                const z = fadeIn ? root._handleFraction * w : selectionX1
                ctx.strokeStyle = root.toCss(Tk.Theme.color.focus)
                ctx.lineWidth = Tk.Theme.size.focusRing
                ctx.beginPath()
                const steps = Math.max(2, Math.ceil(Math.abs(z - a) / Tk.Theme.space.sm))
                for (let s = 0; s <= steps; ++s) {
                    const x = a + (z - a) * s / steps
                    const g = root.previewFactor(x / Math.max(1, w))
                    if (s === 0) ctx.moveTo(x, bands * (1 - g))
                    else ctx.lineTo(x, bands * (1 - g))
                }
                ctx.stroke()
            }
            ctx.font = Tk.Theme.font.micro + "px \"" + Tk.Theme.font.monoFamily + "\""
            ctx.fillStyle = root.toCss(Tk.Theme.color.textMuted)
            ctx.textBaseline = "top"
            for (let i = 0; i < root.rulerMarks.length; ++i) {
                const mark = root.rulerMarks[i]
                const x = Math.max(0, Math.min(w, Number(mark.fraction) * w))
                ctx.strokeStyle = root.toCss(Tk.Theme.color.divider)
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, bands); ctx.stroke()
                ctx.fillText(String(mark.label), x + Tk.Theme.space.xs, bands + Tk.Theme.space.xs)
            }
            if (root._gestureId.length) {
                const x0 = Math.min(root._gestureStartX, root._gestureEndX) / Math.max(1, root.width) * w
                const x1 = Math.max(root._gestureStartX, root._gestureEndX) / Math.max(1, root.width) * w
                const b0 = layout.length ? Math.min(root._gestureStartBand, root._gestureEndBand) : 0
                const b1 = layout.length ? Math.max(root._gestureStartBand, root._gestureEndBand) : 0
                const oneBand = b0 === b1
                ctx.fillStyle = root.toCss(Tk.Theme.color.selection)
                ctx.globalAlpha = Tk.Theme.opacity.muted
                ctx.fillRect(x0, oneBand ? b0 * bandH : 0, Math.max(Tk.Theme.size.border, x1 - x0),
                             oneBand ? bandH : bands)
                ctx.globalAlpha = Tk.Theme.opacity.full
            }
        }

        // Selection handles: gain (drag up/down), fade in/out (drag inwards); gain drops below when narrow.
        Rectangle {
            id: gainHandle
            objectName: "waveformGainHandle"
            visible: root.hasSelection && root.editingEnabled && root.enabled
            width: gainText.implicitWidth + Tk.Theme.space.md
            height: Tk.Theme.size.handle
            x: Math.max(0, Math.min(parent.width - width,
                                    (selectionPaint.selectionX0 + selectionPaint.selectionX1 - width) / 2))
            y: selectionPaint.selectionX1 - selectionPaint.selectionX0 < width + Tk.Theme.size.handle + Tk.Theme.space.sm
               ? Tk.Theme.space.xs * 2 + Tk.Theme.size.handle : Tk.Theme.space.xs
            radius: Tk.Theme.radius.sm
            color: Tk.Theme.color.panel
            border.color: root._handleDrag === "gain" ? Tk.Theme.color.focus : Tk.Theme.color.border
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Selection gain %1").arg(root.formatGain(root.gainDb))
            Tk.Label {
                id: gainText
                objectName: "waveformGainText"
                anchors.centerIn: parent
                mono: true
                font.pixelSize: Tk.Theme.font.micro
                text: root.formatGain(root.gainDb)
            }
            DragHandler {
                objectName: "waveformGainDrag"
                target: null
                xAxis.enabled: false
                onActiveChanged: active ? root.beginHandleDrag("gain") : root.finishHandleDrag()
                onTranslationChanged: if (active) root.updateHandleDrag(0, translation.y)
            }
        }
        Repeater {
            model: ["fadeIn", "fadeOut"]
            delegate: Rectangle {
                required property string modelData
                readonly property bool dragging: root._handleDrag === modelData
                readonly property real restX: modelData === "fadeIn" ? selectionPaint.selectionX0
                                                                     : selectionPaint.selectionX1
                objectName: modelData === "fadeIn" ? "waveformFadeInHandle" : "waveformFadeOutHandle"
                visible: gainHandle.visible
                width: Tk.Theme.size.handle
                height: Tk.Theme.size.handle
                x: (dragging ? root._handleFraction * selectionPaint.width : restX) - width / 2
                y: Tk.Theme.space.xs
                radius: Tk.Theme.radius.xs
                color: dragging ? Tk.Theme.color.focus : Tk.Theme.color.panel
                border.color: Tk.Theme.color.focus
                Accessible.role: Accessible.Slider
                Accessible.name: modelData === "fadeIn" ? qsTr("Fade in length") : qsTr("Fade out length")
                DragHandler {
                    target: null
                    yAxis.enabled: false
                    onActiveChanged: active ? root.beginHandleDrag(parent.modelData) : root.finishHandleDrag()
                    onTranslationChanged: if (active)
                        root.updateHandleDrag((parent.restX + translation.x) / Math.max(1, selectionPaint.width), 0)
                }
            }
        }
    }
    function toCss(c) { return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
                               + Math.round(c.b * 255) + "," + c.a + ")" }
    // Everything the canvas reads; any change repaints it.
    readonly property var _paintInputs: [root.columns, root.channelColumns, root.channelLayout,
        root.selectionChannelId, gainHandle.visible, root.regions, root.rulerMarks, root.selectionStartSample,
        root.selectionEndSample, root.gainDb, root.fadeShape, root.width, root.height]
    on_PaintInputsChanged: selectionPaint.requestPaint()

    DragHandler {
        id: selectionDrag
        objectName: "waveformSelectionDrag"
        target: null
        enabled: root.enabled && root.editingEnabled && !root._handleDrag.length
        onActiveChanged: {
            if (active)
                root.beginSelectionAt(centroid.pressPosition.x, centroid.pressPosition.y)
            else if (root._gestureId.length)
                root.finishSelection()
        }
        onCentroidChanged: if (active) root.updateSelectionAt(centroid.position.x, centroid.position.y)
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
    Keys.onEscapePressed: event => { root.cancelActiveGesture(); event.accepted = true }
    Keys.onPressed: event => {
        const fine = event.modifiers & Qt.ShiftModifier
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            root.scrollRequested(event.key === Qt.Key_Left ? -0.05 : 0.05)
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            root.setGainDb(root.gainDb + (event.key === Qt.Key_Up ? 1 : -1) * (fine ? 0.1 : 1))
        } else if (event.key === Qt.Key_F) {
            root.requestEdit(fine ? "fadeOut" : "fadeIn")
        } else if (event.key === Qt.Key_E) {
            root.fadeShape = root.fadeShape === "equalPower" ? "linear" : "equalPower"
        } else if (event.key === Qt.Key_G) {
            root.requestEdit("gain")
        } else if (event.key === Qt.Key_S) {
            root.requestEdit("silence")
        } else if (event.key === Qt.Key_T) {
            root.requestEdit("crop")
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            root.requestEdit("delete")
        } else {
            return
        }
        event.accepted = true
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
