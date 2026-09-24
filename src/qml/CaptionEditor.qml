// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: caption host owns cue content, exact timing, style semantics,
// overlap rules, serialization, edit transactions and clip export. The
// CaptionFrameRenderer handles supported per-frame burn-in; this editor shows
// supplied interval geometry and submits text/style/timing requests only.
T.Control {
    id: root
    property string sourceId: ""
    property string trackId: ""
    property var cues: [] // {id, inTime, outTime, x0, x1, lane, text, language, styleId}
    property var timeMarks: [] // host supplied {x, time, label}; exact time strings
    property var moveTargets: [] // host supplied {x, inTime, outTime} pairs for exact shifts
    property var styles: [] // host supplied {id, label, supported, reason}
    property string selectedCueId: ""
    property string selectedCueText: ""
    property string selectedCueStyleId: ""
    property string selectedCueLanguage: ""
    property var editAcknowledgement: ({ status: "", message: "" })
    property string warningText: ""
    property string statusText: ""
    property string tooltip: ""
    property bool editingEnabled: true

    property string _draftText: ""
    property string _draftStyleId: ""
    property bool _draftDirty: false
    property bool _syncingText: false
    property int _serial: 0
    property string _gestureId: ""
    property string _gestureCueId: ""
    property string _gestureOperation: ""
    property string _gestureInTime: ""
    property string _gestureOutTime: ""
    property string _targetInTime: ""
    property string _targetOutTime: ""
    property real _gestureStartX: 0
    property real _gestureEndX: 0
    readonly property string effectiveStatus: root.statusText.length ? root.statusText
        : String(root.editAcknowledgement.message || root.editAcknowledgement.status || root.warningText)

    signal cueSelectionRequested(string sourceId, string trackId, string cueId)
    signal cueEditPreviewRequested(string requestId, string sourceId, string trackId, string cueId,
                                   string operation, string inTime, string outTime,
                                   string text, string styleId)
    signal cueEditCommitRequested(string requestId, string sourceId, string trackId, string cueId,
                                  string operation, string inTime, string outTime,
                                  string text, string styleId)
    signal cueEditCanceled(string requestId, string sourceId, string trackId, string cueId)
    signal navigationRequested(string sourceId, string cueId, string direction)
    signal exportRequested(string sourceId, string trackId, string formatId)

    objectName: "captionEditor"
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.waveformHeight * 5
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Caption editor")
    Accessible.description: root.effectiveStatus.length ? root.effectiveStatus
        : qsTr("Timed caption cues. Selected cue %1, language %2. Cue text is accessible separately from rendered video pixels.")
          .arg(root.selectedCueId || qsTr("none")).arg(root.selectedCueLanguage || qsTr("unspecified"))
    activeFocusOnTab: true
    focus: true

    function cueById(id) {
        for (let i = 0; i < root.cues.length; ++i)
            if (String(root.cues[i].id) === String(id)) return root.cues[i]
        return null
    }
    function styleIndex(id) {
        for (let i = 0; i < root.styles.length; ++i)
            if (String(root.styles[i].id) === String(id)) return i
        return -1
    }
    function requestId(prefix) { return prefix + "-" + (++root._serial) }
    function requestCueSelection(id) {
        const cue = root.cueById(id)
        if (!root.enabled || !cue) return false
        root.cueSelectionRequested(root.sourceId, root.trackId, String(cue.id))
        return true
    }
    function requestExport(formatId) {
        if (!root.enabled || !String(formatId).length) return false
        root.exportRequested(root.sourceId, root.trackId, String(formatId))
        return true
    }
    function syncDraftFromHost() {
        if (root._draftDirty) return
        root._syncingText = true
        captionText.text = root.selectedCueText
        root._draftText = root.selectedCueText
        root._draftStyleId = root.selectedCueStyleId
        styleChoice.currentIndex = root.styleIndex(root.selectedCueStyleId)
        root._syncingText = false
    }
    function currentCueTimes() {
        const cue = root.cueById(root.selectedCueId)
        return cue ? [String(cue.inTime), String(cue.outTime)] : ["", ""]
    }
    function previewText(text) {
        const cue = root.cueById(root.selectedCueId)
        if (!root.enabled || !root.editingEnabled || !cue) return false
        root._draftText = String(text)
        root._draftDirty = true
        const times = root.currentCueTimes()
        root.cueEditPreviewRequested(root.requestId("caption-text"), root.sourceId, root.trackId,
                                     String(cue.id), "text", times[0], times[1], root._draftText, root._draftStyleId)
        return true
    }
    function previewStyle(styleId) {
        const cue = root.cueById(root.selectedCueId)
        if (!root.enabled || !root.editingEnabled || !cue) return false
        root._draftStyleId = String(styleId)
        root._draftDirty = true
        const times = root.currentCueTimes()
        root.cueEditPreviewRequested(root.requestId("caption-style"), root.sourceId, root.trackId,
                                     String(cue.id), "style", times[0], times[1], root._draftText, root._draftStyleId)
        return true
    }
    function commitTextAndStyle() {
        const cue = root.cueById(root.selectedCueId)
        if (!root.enabled || !root.editingEnabled || !cue || !root._draftDirty) return false
        const times = root.currentCueTimes()
        root.cueEditCommitRequested(root.requestId("caption-commit"), root.sourceId, root.trackId,
                                    String(cue.id), "text-style", times[0], times[1], root._draftText, root._draftStyleId)
        root._draftDirty = false
        return true
    }
    function cancelTextAndStyle() {
        const cue = root.cueById(root.selectedCueId)
        if (!cue || !root._draftDirty) return false
        root.cueEditCanceled(root.requestId("caption-cancel"), root.sourceId, root.trackId, String(cue.id))
        root._draftDirty = false
        root.syncDraftFromHost()
        return true
    }
    function nearestTimeMark(x) {
        let nearest = null, distance = Infinity
        for (let i = 0; i < root.timeMarks.length; ++i) {
            const item = root.timeMarks[i]
            const d = Math.abs(Number(item.x) - x / Math.max(1, root.width))
            if (d < distance) { nearest = item; distance = d }
        }
        return nearest ? String(nearest.time) : ""
    }
    function nearestMoveTarget(x) {
        let nearest = null, distance = Infinity
        for (let i = 0; i < root.moveTargets.length; ++i) {
            const item = root.moveTargets[i]
            const d = Math.abs(Number(item.x) - x / Math.max(1, root.width))
            if (d < distance) { nearest = item; distance = d }
        }
        return nearest
    }
    function hitCue(x, y) {
        const nx = x / Math.max(1, root.width), ny = y / Math.max(1, timelineCanvas.height)
        for (let i = root.cues.length - 1; i >= 0; --i) {
            const cue = root.cues[i]
            const lane = Number(cue.lane || 0), top = lane * 0.22 + 0.18
            if (nx >= Number(cue.x0) && nx <= Number(cue.x1) && ny >= top && ny <= top + 0.18)
                return cue
        }
        return null
    }
    function beginCueGesture(cueId, operation, x) {
        const cue = root.cueById(cueId)
        if (!root.enabled || !root.editingEnabled || !cue || root._gestureId.length) return false
        root._gestureId = root.requestId("caption-time")
        root._gestureCueId = String(cue.id)
        root._gestureOperation = String(operation)
        root._gestureInTime = String(cue.inTime)
        root._gestureOutTime = String(cue.outTime)
        root._targetInTime = root._gestureInTime
        root._targetOutTime = root._gestureOutTime
        root._gestureStartX = root._gestureEndX = x
        root.requestCueSelection(cue.id)
        return updateCueGesture(x)
    }
    function updateCueGesture(x) {
        if (!root._gestureId.length) return false
        root._gestureEndX = Math.max(0, Math.min(root.width, x))
        if (root._gestureOperation === "move") {
            const pair = root.nearestMoveTarget(root._gestureEndX)
            if (!pair) return false
            root._targetInTime = String(pair.inTime)
            root._targetOutTime = String(pair.outTime)
        } else {
            const time = root.nearestTimeMark(root._gestureEndX)
            if (!time) return false
            if (root._gestureOperation === "trim-in") root._targetInTime = time
            else root._targetOutTime = time
        }
        root.cueEditPreviewRequested(root._gestureId, root.sourceId, root.trackId, root._gestureCueId,
                                     root._gestureOperation, root._targetInTime, root._targetOutTime,
                                     root._draftText, root._draftStyleId)
        timelineCanvas.requestPaint()
        return true
    }
    function finishCueGesture() {
        if (!root._gestureId.length) return false
        root.cueEditCommitRequested(root._gestureId, root.sourceId, root.trackId, root._gestureCueId,
                                    root._gestureOperation, root._targetInTime, root._targetOutTime,
                                    root._draftText, root._draftStyleId)
        root._gestureId = ""
        timelineCanvas.requestPaint()
        return true
    }
    function cancelCueGesture() {
        if (!root._gestureId.length) return false
        const id = root._gestureId, cueId = root._gestureCueId
        root._gestureId = ""
        root.cueEditCanceled(id, root.sourceId, root.trackId, cueId)
        timelineCanvas.requestPaint()
        return true
    }
    function css(c) { return "rgba(" + Math.round(c.r*255) + "," + Math.round(c.g*255) + "," + Math.round(c.b*255) + "," + c.a + ")" }

    background: Rectangle { color: Tk.Theme.color.panelAlt; border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border; radius: Tk.Theme.radius.sm }
    Canvas {
        id: timelineCanvas
        objectName: "captionTimelineCanvas"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height * 0.38
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            ctx.fillStyle = root.css(Tk.Theme.color.inputBg); ctx.fillRect(0, 0, width, height)
            ctx.font = Tk.Theme.font.micro + "px " + Tk.Theme.font.monoFamily
            ctx.fillStyle = root.css(Tk.Theme.color.textMuted)
            ctx.textBaseline = "top"
            for (let m = 0; m < root.timeMarks.length; ++m) {
                const mark = root.timeMarks[m], x = Number(mark.x) * width
                ctx.strokeStyle = root.css(Tk.Theme.color.border)
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                ctx.fillText(String(mark.label || ""), x + Tk.Theme.space.xs, Tk.Theme.space.xs)
            }
            for (let c = 0; c < root.cues.length; ++c) {
                const cue = root.cues[c], x = Number(cue.x0) * width, w = (Number(cue.x1)-Number(cue.x0))*width
                const y = (Number(cue.lane || 0) * 0.22 + 0.18) * height
                const selected = String(cue.id) === root.selectedCueId
                ctx.fillStyle = root.css(selected ? Tk.Theme.color.controlActiveBg : Tk.Theme.color.surface)
                ctx.fillRect(x, y, w, height * 0.18)
                ctx.strokeStyle = root.css(selected ? Tk.Theme.color.accent : Tk.Theme.color.border)
                ctx.strokeRect(x, y, w, height * 0.18)
                ctx.fillStyle = root.css(Tk.Theme.color.text)
                ctx.fillText(String(cue.text || ""), x + Tk.Theme.space.xs, y + Tk.Theme.space.xs)
            }
            if (root._gestureId.length) {
                ctx.strokeStyle = root.css(Tk.Theme.color.warning)
                ctx.lineWidth = Tk.Theme.size.border
                const gx = root._gestureEndX / Math.max(1, root.width) * width
                ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, height); ctx.stroke()
            }
        }
    }
    Tk.TextArea {
        id: captionText
        objectName: "captionTextEditor"
        anchors.left: parent.left
        anchors.right: styleChoice.left
        anchors.top: timelineCanvas.bottom
        anchors.bottom: captionApply.top
        anchors.margins: Tk.Theme.space.sm
        placeholderText: qsTr("Select a cue to edit its text")
        tooltip: qsTr("Caption text; screen-reader text remains available independently of rendered video")
        enabled: root.editingEnabled && Boolean(root.cueById(root.selectedCueId))
        onTextChanged: if (!root._syncingText && activeFocus) root.previewText(text)
    }
    Tk.ComboBox {
        id: styleChoice
        objectName: "captionStyleChoice"
        anchors.right: parent.right
        anchors.top: timelineCanvas.bottom
        anchors.topMargin: Tk.Theme.space.sm
        width: Tk.Theme.size.fieldWidth
        model: root.styles
        textRole: "label"
        enabled: root.editingEnabled && Boolean(root.cueById(root.selectedCueId))
        tooltip: qsTr("Caption style supplied by the host")
        onActivated: function(index) { if (index >= 0 && index < root.styles.length) root.previewStyle(String(root.styles[index].id)) }
    }
    Tk.Button {
        id: captionCancel
        objectName: "captionCancel"
        anchors.right: captionApply.left
        anchors.bottom: parent.bottom
        anchors.rightMargin: Tk.Theme.space.xs
        text: qsTr("Cancel")
        variant: "outline"
        enabled: root._draftDirty
        onClicked: root.cancelTextAndStyle()
    }
    Tk.Button {
        id: captionApply
        objectName: "captionApply"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        text: qsTr("Request edit")
        variant: "accent"
        enabled: root._draftDirty && root.editingEnabled
        onClicked: root.commitTextAndStyle()
    }
    Tk.Caption {
        objectName: "captionEditorStatus"
        anchors.left: parent.left
        anchors.right: captionCancel.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Tk.Theme.space.xs
        text: root.effectiveStatus
        color: root.warningText.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        elide: Text.ElideRight
    }
    DragHandler {
        target: null
        enabled: root.editingEnabled
        onActiveChanged: {
            if (active) {
                const cue = root.hitCue(centroid.position.x, centroid.position.y)
                if (!cue) return
                const left = Number(cue.x0) * root.width, right = Number(cue.x1) * root.width
                const edge = Tk.Theme.size.controlSm
                const operation = Math.abs(centroid.position.x - left) < edge ? "trim-in"
                    : Math.abs(centroid.position.x - right) < edge ? "trim-out" : "move"
                root.beginCueGesture(String(cue.id), operation, centroid.position.x)
            } else if (root._gestureId.length) root.finishCueGesture()
        }
        onCentroidChanged: if (active) root.updateCueGesture(centroid.position.x)
    }
    TapHandler {
        onTapped: function(point) {
            const cue = root.hitCue(point.position.x, point.position.y)
            if (cue) root.requestCueSelection(String(cue.id))
        }
    }
    Keys.onEscapePressed: event => { if (!root.cancelCueGesture()) root.cancelTextAndStyle(); event.accepted = true }
    Keys.onLeftPressed: event => { root.navigationRequested(root.sourceId, root.selectedCueId, "previous"); event.accepted = true }
    Keys.onRightPressed: event => { root.navigationRequested(root.sourceId, root.selectedCueId, "next"); event.accepted = true }
    onCuesChanged: timelineCanvas.requestPaint()
    onTimeMarksChanged: timelineCanvas.requestPaint()
    onSelectedCueIdChanged: { syncDraftFromHost(); timelineCanvas.requestPaint() }
    onStylesChanged: syncDraftFromHost()
    onSelectedCueTextChanged: syncDraftFromHost()
    onSelectedCueStyleIdChanged: syncDraftFromHost()
    onEditAcknowledgementChanged: {
        const outcome = String(root.editAcknowledgement.status || "")
        if (outcome === "accepted" || outcome === "rejected") {
            root._draftDirty = false
            root.syncDraftFromHost()
        }
    }
    onWidthChanged: timelineCanvas.requestPaint()
    onHeightChanged: timelineCanvas.requestPaint()
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
