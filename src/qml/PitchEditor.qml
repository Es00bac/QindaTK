// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the pitch host owns analysis, pitch/time/formant constraints,
// parameter storage, DSP, rendered audio, cancellation and undo. This control
// draws the supplied analysis snapshot and emits preview/commit/cancel requests;
// it never detects pitch or applies an edit locally.
T.Control {
    id: root

    property string sourceId: ""
    property string channelId: ""
    property var analysisSnapshot: ({ available: false, segments: [], sampleBoundaries: [] })
    property int snapshotRevision: 0
    property string selectedSegmentId: ""
    property string selectedNoteId: ""
    property string operation: "pitch" // pitch, time, formant
    property var editAcknowledgement: ({ status: "", message: "" })
    property string statusText: ""
    property string tooltip: ""
    property bool editingEnabled: true
    property string _gestureId: ""
    property string _statusMessage: ""
    property int _gestureSerial: 0
    property string _gestureSegmentId: ""
    property string _gestureNoteId: ""
    property string _gestureOperation: ""
    property string _anchorSample: ""
    property string _targetSample: ""
    property real _targetPitchCents: 0
    property real _targetFormantRatio: 1

    readonly property bool analysisAvailable: Boolean(root.analysisSnapshot && root.analysisSnapshot.available)
    readonly property string effectiveStatus: root.statusText.length ? root.statusText
        : root._statusMessage.length ? root._statusMessage : String(root.editAcknowledgement.message || "")
    readonly property bool pitchRangeAvailable: isFinite(Number(root.analysisSnapshot.minimumPitchCents))
        && isFinite(Number(root.analysisSnapshot.maximumPitchCents))
        && Number(root.analysisSnapshot.maximumPitchCents) > Number(root.analysisSnapshot.minimumPitchCents)
    readonly property int maximumPaintNotes: 4096

    signal selectionRequested(string sourceId, string channelId, string segmentId, string noteId)
    signal editPreviewRequested(string gestureId, string sourceId, string channelId,
                                string segmentId, string noteId, string operation,
                                string anchorSample, string targetSample,
                                real targetPitchCents, real targetFormantRatio)
    signal editCommitRequested(string gestureId, string sourceId, string channelId,
                               string segmentId, string noteId, string operation,
                               string anchorSample, string targetSample,
                               real targetPitchCents, real targetFormantRatio)
    signal gestureCanceled(string gestureId, string sourceId, string segmentId, string noteId)
    signal zoomRequested(string anchorSample, real factor)
    signal scrollRequested(real fractionDelta)

    objectName: "pitchEditor"
    implicitWidth: Tk.Theme.size.sparkline * 4
    implicitHeight: Tk.Theme.size.waveformHeight * 3
    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Pitch correction editor")
    Accessible.description: root.effectiveStatus.length ? root.effectiveStatus
        : root.analysisAvailable ? qsTr("Pitch analysis snapshot. Select a note and drag to request pitch, time, or formant correction. Escape cancels the active request.")
        : qsTr("Pitch analysis is unavailable")
    activeFocusOnTab: true
    focus: true

    function segments() { return root.analysisSnapshot && root.analysisSnapshot.segments || [] }
    function segmentById(id) {
        const list = root.segments()
        for (let i = 0; i < list.length; ++i)
            if (String(list[i].id) === String(id)) return list[i]
        return null
    }
    function noteById(segment, id) {
        const notes = segment && segment.notes || []
        for (let i = 0; i < notes.length; ++i)
            if (String(notes[i].id) === String(id)) return notes[i]
        return null
    }
    function nearestBoundary(x) {
        const boundaries = root.analysisSnapshot && root.analysisSnapshot.sampleBoundaries || []
        if (!boundaries.length) return ""
        const fraction = Math.max(0, Math.min(1, x / Math.max(1, root.width)))
        let nearest = boundaries[0], best = Infinity
        for (let i = 0; i < boundaries.length; ++i) {
            const boundary = boundaries[i]
            const bx = boundary.x === undefined ? i / Math.max(1, boundaries.length - 1) : Number(boundary.x)
            if (Math.abs(bx - fraction) < best) { nearest = boundary; best = Math.abs(bx - fraction) }
        }
        return String(nearest.sample || "")
    }
    function pitchAtY(y) {
        const low = Number(root.analysisSnapshot.minimumPitchCents)
        const high = Number(root.analysisSnapshot.maximumPitchCents)
        if (!isFinite(low) || !isFinite(high) || high <= low) return low || 0
        return high - Math.max(0, Math.min(1, y / Math.max(1, root.height))) * (high - low)
    }
    function formantAtY(segment, note, y) {
        const minimum = segment.formantMinimum !== undefined ? Number(segment.formantMinimum)
            : Number(root.analysisSnapshot.formantMinimum)
        const maximum = segment.formantMaximum !== undefined ? Number(segment.formantMaximum)
            : Number(root.analysisSnapshot.formantMaximum)
        if (!isFinite(minimum) || !isFinite(maximum) || maximum <= minimum) return NaN
        const fraction = 1 - Math.max(0, Math.min(1, y / Math.max(1, root.height)))
        return minimum + fraction * (maximum - minimum)
    }
    function hitNote(x, y) {
        if (!root.pitchRangeAvailable) return null
        const px = x / Math.max(1, root.width), py = y / Math.max(1, root.height)
        const low = Number(root.analysisSnapshot.minimumPitchCents) || 0
        const high = Number(root.analysisSnapshot.maximumPitchCents) || 1
        const notes = []
        const segments = root.segments()
        for (let s = 0; s < segments.length; ++s) {
            const items = segments[s].notes || []
            for (let n = 0; n < items.length; ++n) {
                const note = items[n]
                const centerY = 1 - (Number(note.pitchCents) - low) / Math.max(1, high - low)
                if (px >= Number(note.x0) && px <= Number(note.x1)
                        && Math.abs(py - centerY) <= Tk.Theme.size.controlSm / Math.max(1, root.height))
                    return { segment: segments[s], note: note }
            }
        }
        return null
    }
    function select(segmentId, noteId) {
        if (!root.analysisAvailable || !root.segmentById(segmentId) || !root.noteById(root.segmentById(segmentId), noteId))
            return false
        root.selectionRequested(root.sourceId, root.channelId, String(segmentId), String(noteId))
        return true
    }
    function beginEdit(segmentId, noteId, editOperation, x, y) {
        if (!root.enabled || !root.editingEnabled || !root.analysisAvailable || root._gestureId.length
                || ["pitch", "time", "formant"].indexOf(String(editOperation)) < 0)
            return false
        const segment = root.segmentById(segmentId)
        const note = root.noteById(segment, noteId)
        if (!segment || !note) return false
        const formantMinimum = segment.formantMinimum !== undefined ? Number(segment.formantMinimum)
            : Number(root.analysisSnapshot.formantMinimum)
        const formantMaximum = segment.formantMaximum !== undefined ? Number(segment.formantMaximum)
            : Number(root.analysisSnapshot.formantMaximum)
        if (!note.startSample || (editOperation === "pitch" && !root.pitchRangeAvailable)
                || (editOperation === "time" && !(root.analysisSnapshot.sampleBoundaries || []).length)
                || (editOperation === "formant" && (!isFinite(formantMinimum) || !isFinite(formantMaximum)
                    || formantMaximum <= formantMinimum || !isFinite(Number(note.formantRatio))))) {
            root._statusMessage = editOperation === "pitch" ? qsTr("Pitch range unavailable in analysis snapshot")
                : editOperation === "time" ? qsTr("Exact sample boundaries unavailable in analysis snapshot")
                : qsTr("Formant range or current value unavailable in analysis snapshot")
            return false
        }
        root._statusMessage = ""
        root.select(segmentId, noteId)
        root._gestureSerial += 1
        root._gestureId = "pitch-edit-" + root._gestureSerial
        root._gestureSegmentId = String(segmentId)
        root._gestureNoteId = String(noteId)
        root._gestureOperation = String(editOperation)
        root._anchorSample = String(editOperation === "time" ? note.startSample : note.startSample)
        root._targetSample = root._anchorSample
        root._targetPitchCents = Number(note.pitchCents)
        root._targetFormantRatio = isFinite(Number(note.formantRatio)) ? Number(note.formantRatio) : 0
        updateEdit(x, y)
        return true
    }
    function updateEdit(x, y) {
        if (!root._gestureId.length) return false
        const segment = root.segmentById(root._gestureSegmentId)
        const note = root.noteById(segment, root._gestureNoteId)
        if (!note) return root.cancelEdit()
        if (root._gestureOperation === "time")
            root._targetSample = root.nearestBoundary(x)
        else if (root._gestureOperation === "pitch")
            root._targetPitchCents = root.pitchAtY(y)
        else
            root._targetFormantRatio = root.formantAtY(segment, note, y)
        root.editPreviewRequested(root._gestureId, root.sourceId, root.channelId,
                                  root._gestureSegmentId, root._gestureNoteId, root._gestureOperation,
                                  root._anchorSample, root._targetSample,
                                  root._targetPitchCents, root._targetFormantRatio)
        pitchCanvas.requestPaint()
        return true
    }
    function finishEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId
        root.editCommitRequested(id, root.sourceId, root.channelId,
                                 root._gestureSegmentId, root._gestureNoteId, root._gestureOperation,
                                 root._anchorSample, root._targetSample,
                                 root._targetPitchCents, root._targetFormantRatio)
        root._gestureId = ""
        pitchCanvas.requestPaint()
        return true
    }
    function cancelEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId, segmentId = root._gestureSegmentId, noteId = root._gestureNoteId
        root._gestureId = ""
        root.gestureCanceled(id, root.sourceId, segmentId, noteId)
        pitchCanvas.requestPaint()
        return true
    }
    function beginSelectedEdit(editOperation, x, y) {
        return root.beginEdit(root.selectedSegmentId, root.selectedNoteId, editOperation, x, y)
    }
    function css(color) {
        return "rgba(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + ","
                + Math.round(color.b * 255) + "," + color.a + ")"
    }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Canvas {
        id: pitchCanvas
        objectName: "pitchContourCanvas"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = root.css(Tk.Theme.color.inputBg)
            ctx.fillRect(0, 0, width, height)
            if (!root.analysisAvailable || !root.pitchRangeAvailable) return
            const low = Number(root.analysisSnapshot.minimumPitchCents) || 0
            const high = Number(root.analysisSnapshot.maximumPitchCents) || 1
            const segments = root.segments()
            let painted = 0
            for (let s = 0; s < segments.length; ++s) {
                const segment = segments[s]
                const notes = segment.notes || []
                for (let n = 0; n < notes.length && painted < root.maximumPaintNotes; ++n, ++painted) {
                    const note = notes[n]
                    const y = (1 - (Number(note.pitchCents) - low) / Math.max(1, high - low)) * height
                    const x0 = Number(note.x0) * width, x1 = Number(note.x1) * width
                    const selected = String(note.id) === root.selectedNoteId
                    ctx.fillStyle = root.css(selected ? Tk.Theme.color.controlActiveBg : Tk.Theme.color.surface)
                    ctx.fillRect(x0, y - Tk.Theme.size.controlSm / 2, Math.max(Tk.Theme.size.border, x1 - x0), Tk.Theme.size.controlSm)
                    ctx.strokeStyle = root.css(selected ? Tk.Theme.color.accent : Tk.Theme.color.border)
                    ctx.lineWidth = Tk.Theme.size.border
                    ctx.strokeRect(x0, y - Tk.Theme.size.controlSm / 2, Math.max(Tk.Theme.size.border, x1 - x0), Tk.Theme.size.controlSm)
                    ctx.fillStyle = root.css(Tk.Theme.color.text)
                    ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
                    ctx.fillText(String(note.label || note.pitchLabel || ""), x0 + Tk.Theme.space.xs, y - Tk.Theme.space.xs)
                }
                const contour = segment.contour || []
                ctx.beginPath()
                let started = false
                for (let p = 0; p < contour.length && painted < root.maximumPaintNotes; ++p, ++painted) {
                    const point = contour[p]
                    if (point.voiced === false || Number(point.confidence) < Number(root.analysisSnapshot.confidenceThreshold || 0)) {
                        started = false
                        continue
                    }
                    const x = Number(point.x) * width
                    const y = (1 - (Number(point.pitchCents) - low) / Math.max(1, high - low)) * height
                    if (started) ctx.lineTo(x, y); else { ctx.moveTo(x, y); started = true }
                }
                ctx.strokeStyle = root.css(Tk.Theme.color.info)
                ctx.lineWidth = Tk.Theme.size.border
                ctx.stroke()
            }
            if (root._gestureId.length) {
                ctx.fillStyle = root.css(Tk.Theme.color.selection)
                ctx.globalAlpha = Tk.Theme.opacity.muted
                ctx.fillRect(0, 0, width, height)
                ctx.globalAlpha = Tk.Theme.opacity.full
            }
            ctx.fillStyle = root.css(Tk.Theme.color.textMuted)
            ctx.font = Tk.Theme.font.micro + "px " + Tk.Theme.font.monoFamily
            // Axis labels sit inside the canvas: top-aligned at the top edge, bottom-aligned at the bottom.
            ctx.textBaseline = "top"
            ctx.fillText(String(high) + " cents", Tk.Theme.space.xs, Tk.Theme.space.xs)
            ctx.textBaseline = "bottom"
            ctx.fillText(String(low) + " cents", Tk.Theme.space.xs, height - Tk.Theme.space.xs)
            ctx.textBaseline = "alphabetic"
        }
    }
    onAnalysisSnapshotChanged: pitchCanvas.requestPaint()
    onSnapshotRevisionChanged: pitchCanvas.requestPaint()
    onSelectedNoteIdChanged: pitchCanvas.requestPaint()
    onWidthChanged: pitchCanvas.requestPaint()
    onHeightChanged: pitchCanvas.requestPaint()
    Tk.Caption {
        objectName: "pitchEditorStatus"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !root.analysisAvailable || !root.pitchRangeAvailable || root.effectiveStatus.length > 0
                 || String(root.editAcknowledgement.status || "").length > 0
        text: !root.analysisAvailable ? String(root.analysisSnapshot.error || qsTr("Pitch analysis unavailable"))
              : !root.pitchRangeAvailable ? qsTr("Pitch range unavailable in analysis snapshot")
              : root.effectiveStatus.length ? root.effectiveStatus
              : String(root.editAcknowledgement.status || "")
        color: !root.analysisAvailable || !root.pitchRangeAvailable || String(root.editAcknowledgement.status || "") === "rejected"
               ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        elide: Text.ElideRight
    }
    DragHandler {
        id: editDrag
        target: null
        enabled: root.analysisAvailable && root.editingEnabled
        onActiveChanged: {
            if (active) {
                const hit = root.hitNote(centroid.position.x, centroid.position.y)
                if (hit) root.beginEdit(String(hit.segment.id), String(hit.note.id), root.operation,
                                        centroid.position.x, centroid.position.y)
            } else if (root._gestureId.length) root.finishEdit()
        }
        onCentroidChanged: if (active) root.updateEdit(centroid.position.x, centroid.position.y)
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint) {
            const hit = root.hitNote(eventPoint.position.x, eventPoint.position.y)
            if (hit) root.select(String(hit.segment.id), String(hit.note.id))
        }
    }
    Keys.onEscapePressed: event => { root.cancelEdit(); event.accepted = true }
    Keys.onReturnPressed: event => { root.finishEdit(); event.accepted = true }
    Keys.onEnterPressed: event => { root.finishEdit(); event.accepted = true }
    Keys.onLeftPressed: event => { root.scrollRequested(-1); event.accepted = true }
    Keys.onRightPressed: event => { root.scrollRequested(1); event.accepted = true }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
