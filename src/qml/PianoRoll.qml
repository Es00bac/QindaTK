// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: note records and tick boundaries are immutable host snapshots.
// Every edit request names a stable note ID and carries exact decimal tick strings;
// the MIDI model owns duration math, validation, preview state, commit, and undo.
// AGENT-GUARD (AUD-22, WP-064): every auditionRequested is matched by one
// auditionReleased — on finish, cancel, a pitch change, disabling and
// destruction — so a host wired to AudioEngine.auditionNoteOn/Off never
// leaves a note sounding.
T.Control {
    id: root

    property string patternId: ""
    property var notes: [] // {id, pitch, velocity, x0, x1, startTicks, endTicks}
    property var tickBoundaries: [] // exact decimal strings for snapped grid positions
    property int lowestPitch: 48
    property int pitchRows: 24
    property string selectedNoteId: ""
    property string tooltip: ""
    property string statusText: ""
    property bool editingEnabled: true
    property bool auditionEnabled: true
    property int _auditionPitch: -1
    property real _auditionVelocity: 0.8
    property string _gestureId: ""
    property string _gestureNoteId: ""
    property string _gestureKind: ""
    property string _gestureTicks: ""
    property int _gesturePitch: 0
    property real _gestureX: 0
    property real _gestureY: 0
    property real _targetX: 0
    property int _gestureSerial: 0

    signal noteSelected(string noteId)
    signal auditionRequested(int pitch, real velocity)
    signal auditionReleased(int pitch)
    signal notePreviewRequested(string requestId, string patternId, string noteId,
                                string operation, string targetTicks, int targetPitch)
    signal noteEditFinished(string requestId)
    signal noteEditCanceled(string requestId)
    signal noteInsertRequested(string requestId, string patternId, string startTicks,
                               string endTicks, int pitch, real velocity)
    signal noteDeleteRequested(string patternId, string noteId)
    signal zoomRequested(string anchorTicks, real factor)
    signal scrollRequested(real tickFraction, int pitchRows)

    objectName: "pianoRoll"
    implicitWidth: Tk.Theme.size.sparkline * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 4
    Accessible.role: Accessible.Graphic
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Piano roll")
    Accessible.description: root.statusText.length ? root.statusText
        : qsTr("MIDI note grid. Drag notes to move; drag the right edge to resize; double-click to insert.")
    activeFocusOnTab: true
    focus: true

    function toCss(color) {
        return "rgba(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + ","
               + Math.round(color.b * 255) + "," + color.a + ")"
    }
    function pitchAt(y) {
        return Math.max(root.lowestPitch, Math.min(root.lowestPitch + root.pitchRows - 1,
            root.lowestPitch + root.pitchRows - 1 - Math.floor(y / Math.max(1, root.height) * root.pitchRows)))
    }
    function tickAt(x) {
        if (!root.tickBoundaries.length) return "0"
        const fraction = Math.max(0, Math.min(0.999999, x / Math.max(1, root.width)))
        return String(root.tickBoundaries[Math.min(root.tickBoundaries.length - 1,
            Math.floor(fraction * (root.tickBoundaries.length - 1)))])
    }
    function noteAt(x, y) {
        const pitch = root.pitchAt(y)
        const fraction = x / Math.max(1, root.width)
        for (let i = root.notes.length - 1; i >= 0; --i) {
            const note = root.notes[i]
            if (Number(note.pitch) === pitch && fraction >= Number(note.x0) && fraction <= Number(note.x1))
                return note
        }
        return null
    }
    function auditionPitch(pitch, velocity) {
        if (pitch === root._auditionPitch) return false
        root.releaseAudition()
        if (!root.auditionEnabled || !root.enabled || pitch < 0 || pitch > 127) return false
        root._auditionPitch = pitch
        root._auditionVelocity = velocity
        root.auditionRequested(pitch, velocity)
        return true
    }
    function releaseAudition() {
        if (root._auditionPitch < 0) return false
        const pitch = root._auditionPitch
        root._auditionPitch = -1
        root.auditionReleased(pitch)
        return true
    }
    function beginEditAt(x, y) {
        if (!root.enabled || !root.editingEnabled || root._gestureId.length) return false
        const note = root.noteAt(x, y)
        if (!note) return false
        root._gestureSerial += 1
        root._gestureId = "piano-edit-" + root._gestureSerial
        root._gestureNoteId = String(note.id)
        root.selectedNoteId = root._gestureNoteId
        root.noteSelected(root.selectedNoteId)
        root._gestureKind = x / Math.max(1, root.width) >= Number(note.x1) - Tk.Theme.size.controlSm / Math.max(1, root.width)
                              ? "resizeEnd" : "move"
        root._gestureTicks = String(note.startTicks)
        root._gesturePitch = Number(note.pitch)
        root._gestureX = x
        root._gestureY = y
        root._targetX = Number(note.x0)
        root.auditionPitch(root._gesturePitch, Number(note.velocity) || 0.8)
        return true
    }
    function updateEditAt(x, y) {
        if (!root._gestureId.length) return false
        root._targetX = Math.max(0, Math.min(1, x / Math.max(1, root.width)))
        const ticks = root.tickAt(x)
        const pitch = root._gestureKind === "move" ? root.pitchAt(y) : root._gesturePitch
        root.auditionPitch(pitch, root._auditionVelocity)
        root.notePreviewRequested(root._gestureId, root.patternId, root._gestureNoteId,
                                  root._gestureKind, ticks, pitch)
        pianoCanvas.requestPaint()
        return true
    }
    function finishEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId
        root._gestureId = ""
        root.releaseAudition()
        root.noteEditFinished(id)
        pianoCanvas.requestPaint()
        return true
    }
    function cancelEdit() {
        if (!root._gestureId.length) return false
        const id = root._gestureId
        root._gestureId = ""
        root.releaseAudition()
        root.noteEditCanceled(id)
        pianoCanvas.requestPaint()
        return true
    }
    function requestInsertAt(x, y) {
        if (!root.enabled || !root.editingEnabled || root.tickBoundaries.length < 2) return false
        const index = Math.min(root.tickBoundaries.length - 2,
            Math.floor(Math.max(0, Math.min(0.999999, x / Math.max(1, root.width)))
                       * (root.tickBoundaries.length - 1)))
        root._gestureSerial += 1
        root.noteInsertRequested("piano-insert-" + root._gestureSerial, root.patternId,
            String(root.tickBoundaries[index]), String(root.tickBoundaries[index + 1]), root.pitchAt(y), 0.8)
        return true
    }
    function requestDelete(noteId) {
        if (!root.enabled || !root.editingEnabled || !noteId) return false
        root.noteDeleteRequested(root.patternId, String(noteId))
        return true
    }
    function cancelActiveEdit() { return root.cancelEdit() }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Canvas {
        id: pianoCanvas
        objectName: "pianoRollCanvas"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            const rowHeight = h / Math.max(1, root.pitchRows)
            ctx.fillStyle = root.toCss(Tk.Theme.color.panelAlt)
            ctx.fillRect(0, 0, w, h)
            for (let row = 0; row < root.pitchRows; ++row) {
                const pitch = root.lowestPitch + root.pitchRows - row - 1
                if (pitch % 12 === 0) {
                    ctx.fillStyle = root.toCss(Tk.Theme.color.inputBg)
                    ctx.fillRect(0, row * rowHeight, w, rowHeight)
                }
                ctx.strokeStyle = root.toCss(pitch % 12 === 0 ? Tk.Theme.color.border : Tk.Theme.color.divider)
                ctx.lineWidth = Tk.Theme.size.border
                ctx.beginPath(); ctx.moveTo(0, row * rowHeight); ctx.lineTo(w, row * rowHeight); ctx.stroke()
            }
            const count = root.tickBoundaries.length - 1
            for (let i = 0; i <= count; ++i) {
                const x = count > 0 ? i / count * w : 0
                ctx.strokeStyle = root.toCss(i % 4 === 0 ? Tk.Theme.color.border : Tk.Theme.color.divider)
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke()
            }
            for (let i = 0; i < root.notes.length; ++i) {
                const note = root.notes[i]
                const row = root.lowestPitch + root.pitchRows - 1 - Number(note.pitch)
                const x = Number(note.x0) * w, x2 = Number(note.x1) * w
                const y = row * rowHeight + Tk.Theme.size.border
                ctx.fillStyle = root.toCss(note.id === root.selectedNoteId ? Tk.Theme.color.accent : Tk.Theme.color.info)
                ctx.fillRect(x, y, Math.max(Tk.Theme.size.controlSm, x2 - x), Math.max(Tk.Theme.size.border, rowHeight - 2 * Tk.Theme.size.border))
                ctx.fillStyle = root.toCss(Tk.Theme.color.text)
                ctx.globalAlpha = Tk.Theme.opacity.disabled + (Tk.Theme.opacity.full - Tk.Theme.opacity.disabled) * Math.max(0, Math.min(1, Number(note.velocity) || 0.8))
                ctx.fillRect(x, y, Math.max(Tk.Theme.size.border, x2 - x) * Math.max(0, Math.min(1, Number(note.velocity) || 0.8)), Tk.Theme.size.border)
                ctx.globalAlpha = Tk.Theme.opacity.full
            }
            if (root._gestureId.length) {
                ctx.strokeStyle = root.toCss(Tk.Theme.color.focus)
                ctx.lineWidth = Tk.Theme.size.focusRing
                ctx.strokeRect(root._targetX * w, 0, Tk.Theme.size.border, h)
            }
        }
    }
    onEnabledChanged: if (!root.enabled) { root.cancelEdit(); root.releaseAudition() }
    onAuditionEnabledChanged: if (!root.auditionEnabled) root.releaseAudition()
    Component.onDestruction: root.releaseAudition()
    onNotesChanged: pianoCanvas.requestPaint()
    onSelectedNoteIdChanged: pianoCanvas.requestPaint()
    onTickBoundariesChanged: pianoCanvas.requestPaint()
    onWidthChanged: pianoCanvas.requestPaint()
    onHeightChanged: pianoCanvas.requestPaint()

    DragHandler {
        target: null
        enabled: root.enabled && root.editingEnabled
        onActiveChanged: {
            if (active) root.beginEditAt(centroid.position.x, centroid.position.y)
            else if (root._gestureId.length) root.finishEdit()
        }
        onCentroidChanged: if (active) root.updateEditAt(centroid.position.x, centroid.position.y)
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: function(eventPoint) { root.requestInsertAt(eventPoint.position.x, eventPoint.position.y) }
        onTapped: function(eventPoint) {
            const note = root.noteAt(eventPoint.position.x, eventPoint.position.y)
            if (note) { root.selectedNoteId = String(note.id); root.noteSelected(root.selectedNoteId) }
        }
    }
    WheelHandler {
        target: null
        onWheel: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                root.zoomRequested(root.tickAt(event.x), event.angleDelta.y > 0 ? 1.2 : 0.83)
            else
                root.scrollRequested(event.x / Math.max(1, root.width), event.angleDelta.y > 0 ? 2 : -2)
            event.accepted = true
        }
    }
    Keys.onEscapePressed: event => { root.cancelEdit(); event.accepted = true }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Delete && root.selectedNoteId.length) {
            root.requestDelete(root.selectedNoteId)
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            root.scrollRequested(0, event.key === Qt.Key_Left ? -1 : 1)
            event.accepted = true
        }
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
