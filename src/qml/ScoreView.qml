// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the notation host owns semantic score records, rhythmic
// interpretation, engraving coordinates, pagination, selection, and edits.
// `layoutSnapshot` supplies already-laid-out staff/measure/event coordinates
// and exact insertion tick strings. This control draws that snapshot and sends
// stable-ID requests; it does not infer spacing, spelling, rhythm, or playback.
// Snapshot coordinates (`x0`, `x1`, `top`, `lineGap`, event `x`, `staffStep`,
// playbackCursorX, and entry-position `x`) are normalized to this control.
// `staffStep` is the host's diatonic engraving step from the bottom staff line;
// exact insertion ticks remain strings and are never parsed as JS numbers.
T.Control {
    id: root

    property string scoreId: ""
    property string scoreName: ""
    property var layoutSnapshot: ({ available: false, systems: [] })
    property int snapshotRevision: 0
    property string selectedEventId: ""
    property string activePartId: ""
    property string activeStaffId: ""
    property string activeVoiceId: ""
    property string activeMeasureId: ""
    property string entryTick: ""
    property string entryPitch: "C4"
    property string entryDurationId: "quarter"
    property string entryMode: "note" // note or rest
    property string tooltip: ""
    property string errorText: ""
    property var unsupportedFeatures: []
    property real playbackCursorX: -1 // host-provided normalized display coordinate; no playback is produced here
    property string _requestStatus: ""
    property int _serial: 0
    readonly property bool layoutAvailable: Boolean(root.layoutSnapshot && root.layoutSnapshot.available)
    readonly property string effectiveError: root.errorText.length ? root.errorText
        : String(root.layoutSnapshot.error || "")
    readonly property int maximumPaintEvents: 4096

    signal eventSelectionRequested(string requestId, string scoreId, string eventId,
                                   string partId, string staffId, string voiceId, string measureId)
    signal measureSelectionRequested(string requestId, string scoreId, string partId,
                                     string staffId, string measureId)
    signal voiceSelectionRequested(string scoreId, string partId, string staffId, string voiceId)
    signal noteEntryRequested(string requestId, string scoreId, string partId, string staffId,
                              string voiceId, string measureId, string positionTicks,
                              string pitch, string durationId)
    signal restEntryRequested(string requestId, string scoreId, string partId, string staffId,
                              string voiceId, string measureId, string positionTicks,
                              string durationId)
    signal navigateRequested(string scoreId, string selectedEventId, string direction)
    signal playbackLocateRequested(string measureId, string positionTicks)

    objectName: "scoreView"
    implicitWidth: Tk.Theme.size.sparkline * 4
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length ? root.tooltip
        : (root.scoreName.length ? root.scoreName : qsTr("Score view"))
    Accessible.description: root.effectiveError.length ? root.effectiveError
        : root.unsupportedFeatures.length ? qsTr("Unsupported notation features: %1").arg(root.unsupportedFeatures.join(", "))
        : root.layoutAvailable ? qsTr("Notation score. Use arrow keys to navigate events; Enter requests note or rest entry at the host cursor.")
        : qsTr("Notation layout is not available")
    activeFocusOnTab: true
    focus: true

    function css(color) {
        return "rgba(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + ","
               + Math.round(color.b * 255) + "," + color.a + ")"
    }
    function allStaves() {
        const result = []
        const systems = root.layoutSnapshot && root.layoutSnapshot.systems || []
        for (let s = 0; s < systems.length; ++s) {
            const staves = systems[s].staves || []
            for (let i = 0; i < staves.length; ++i)
                result.push({ systemId: String(systems[s].id || ""), staff: staves[i] })
        }
        return result
    }
    function measureFor(staff, measureId) {
        const measures = staff && staff.measures || []
        for (let i = 0; i < measures.length; ++i)
            if (String(measures[i].id) === String(measureId)) return measures[i]
        return null
    }
    function voiceFor(measure, voiceId) {
        const voices = measure && measure.voices || []
        for (let i = 0; i < voices.length; ++i)
            if (String(voices[i].id) === String(voiceId)) return voices[i]
        return null
    }
    function createRequestId(prefix) {
        root._serial += 1
        return prefix + "-" + root._serial
    }
    function eventContext(event) {
        const staves = root.allStaves()
        for (let i = 0; i < staves.length; ++i) {
            const staff = staves[i].staff
            const measures = staff.measures || []
            for (let m = 0; m < measures.length; ++m) {
                const voices = measures[m].voices || []
                for (let v = 0; v < voices.length; ++v) {
                    const events = voices[v].events || []
                    for (let e = 0; e < events.length; ++e)
                        if (String(events[e].id) === String(event.id))
                            return { partId: String(staff.partId || ""), staffId: String(staff.id || ""),
                                     voiceId: String(voices[v].id || ""), measureId: String(measures[m].id || "") }
                }
            }
        }
        return null
    }
    function eventAt(x, y) {
        if (!root.layoutAvailable) return null
        const staves = root.allStaves()
        let nearest = null
        let best = Tk.Theme.size.controlSm / Math.max(1, root.width)
        for (let i = 0; i < staves.length; ++i) {
            const staff = staves[i].staff
            const gap = Number(staff.lineGap) || 0
            const top = Number(staff.top) || 0
            const staffY = top + 2 * gap
            const measures = staff.measures || []
            for (let m = 0; m < measures.length; ++m) {
                const voices = measures[m].voices || []
                for (let v = 0; v < voices.length; ++v) {
                    const events = voices[v].events || []
                    for (let e = 0; e < events.length; ++e) {
                        const event = events[e]
                        const dx = Math.abs(Number(event.x) - x / Math.max(1, root.width))
                        const dy = Math.abs(staffY + (4 - Number(event.staffStep || 0)) * gap / 2
                                            - y / Math.max(1, root.height))
                        const score = dx + dy
                        if (score < best) { nearest = event; best = score }
                    }
                }
            }
        }
        return nearest
    }
    function requestSelection(eventId) {
        if (!root.enabled || !root.layoutAvailable || !eventId) return false
        const staves = root.allStaves()
        let event = null
        for (let i = 0; i < staves.length && !event; ++i) {
            const measures = staves[i].staff.measures || []
            for (let m = 0; m < measures.length && !event; ++m) {
                const voices = measures[m].voices || []
                for (let v = 0; v < voices.length && !event; ++v) {
                    const events = voices[v].events || []
                    for (let e = 0; e < events.length; ++e)
                        if (String(events[e].id) === String(eventId)) { event = events[e]; break }
                }
            }
        }
        const context = event ? root.eventContext(event) : null
        if (!context) return false
        const requestId = root.createRequestId("score-select")
        root.eventSelectionRequested(requestId, root.scoreId, String(eventId), context.partId,
                                     context.staffId, context.voiceId, context.measureId)
        root._requestStatus = qsTr("Selection request sent for %1").arg(eventId)
        return true
    }
    function requestMeasureSelection(partId, staffId, measureId) {
        if (!root.enabled || !root.layoutAvailable || !measureId) return false
        root.measureSelectionRequested(root.createRequestId("score-measure"), root.scoreId,
                                       String(partId), String(staffId), String(measureId))
        return true
    }
    function requestEntryAt(x, y) {
        if (!root.enabled || !root.layoutAvailable || root.effectiveError.length
                || root.unsupportedFeatures.length > 0)
            return false
        const staves = root.allStaves()
        const nx = Math.max(0, Math.min(1, x / Math.max(1, root.width)))
        const ny = Math.max(0, Math.min(1, y / Math.max(1, root.height)))
        for (let i = 0; i < staves.length; ++i) {
            const staff = staves[i].staff
            const top = Number(staff.top) || 0
            const bottom = top + 4 * (Number(staff.lineGap) || 0)
            if (ny < top - (Number(staff.lineGap) || 0) || ny > bottom + (Number(staff.lineGap) || 0)) continue
            const measures = staff.measures || []
            for (let m = 0; m < measures.length; ++m) {
                const measure = measures[m]
                if (nx < Number(measure.x0) || nx > Number(measure.x1)) continue
                const positions = measure.entryPositions || []
                let nearest = null, distance = Infinity
                for (let p = 0; p < positions.length; ++p) {
                    const candidateDistance = Math.abs(nx - Number(positions[p].x))
                    if (candidateDistance < distance) { nearest = positions[p]; distance = candidateDistance }
                }
                if (!nearest || !nearest.ticks) return false
                let voiceId = root.activeVoiceId
                if (!root.voiceFor(measure, voiceId)) {
                    const voices = measure.voices || []
                    if (!voices.length) return false
                    voiceId = String(voices[0].id)
                }
                const requestId = root.createRequestId(root.entryMode === "rest" ? "score-rest" : "score-note")
                if (root.entryMode === "rest")
                    root.restEntryRequested(requestId, root.scoreId, String(staff.partId), String(staff.id),
                                            voiceId, String(measure.id), String(nearest.ticks), root.entryDurationId)
                else
                    root.noteEntryRequested(requestId, root.scoreId, String(staff.partId), String(staff.id),
                                            voiceId, String(measure.id), String(nearest.ticks),
                                            root.entryPitch, root.entryDurationId)
                return true
            }
        }
        return false
    }
    function requestCursorEntry() {
        if (!root.activeStaffId || !root.activeMeasureId || !root.entryTick || !root.activeVoiceId
                || !root.enabled || !root.layoutAvailable || root.effectiveError.length
                || root.unsupportedFeatures.length > 0)
            return false
        const id = root.createRequestId(root.entryMode === "rest" ? "score-rest" : "score-note")
        if (root.entryMode === "rest")
            root.restEntryRequested(id, root.scoreId, root.activePartId, root.activeStaffId,
                                    root.activeVoiceId, root.activeMeasureId, root.entryTick, root.entryDurationId)
        else
            root.noteEntryRequested(id, root.scoreId, root.activePartId, root.activeStaffId,
                                    root.activeVoiceId, root.activeMeasureId, root.entryTick,
                                    root.entryPitch, root.entryDurationId)
        return true
    }
    function navigate(direction) {
        if (!root.enabled || !root.layoutAvailable) return false
        root.navigateRequested(root.scoreId, root.selectedEventId, String(direction))
        return true
    }
    function locateAtCursor() {
        if (!root.activeMeasureId || !root.entryTick) return false
        root.playbackLocateRequested(root.activeMeasureId, root.entryTick)
        return true
    }
    function acknowledgeRequest(message) {
        root._requestStatus = String(message || "")
    }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Canvas {
        id: scoreCanvas
        objectName: "scoreCanvas"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            ctx.fillStyle = root.css(Tk.Theme.color.surface)
            ctx.fillRect(0, 0, w, h)
            if (!root.layoutAvailable) return
            ctx.strokeStyle = root.css(Tk.Theme.color.textMuted)
            ctx.fillStyle = root.css(Tk.Theme.color.text)
            ctx.lineWidth = Tk.Theme.size.border
            ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
            ctx.textBaseline = "middle"
            const systems = root.layoutSnapshot.systems || []
            let painted = 0
            for (let s = 0; s < systems.length; ++s) {
                const staves = systems[s].staves || []
                for (let t = 0; t < staves.length; ++t) {
                    const staff = staves[t]
                    const x0 = Number(staff.x0) * w, x1 = Number(staff.x1) * w
                    const top = Number(staff.top) * h, gap = Number(staff.lineGap) * h
                    for (let line = 0; line < 5; ++line) {
                        const y = top + line * gap
                        ctx.beginPath(); ctx.moveTo(x0, y); ctx.lineTo(x1, y); ctx.stroke()
                    }
                    ctx.fillStyle = root.css(Tk.Theme.color.textMuted)
                    ctx.fillText(String(staff.label || staff.clef || ""), Math.max(0, x0 - Tk.Theme.size.labelWidth / 2), top + 2 * gap)
                    const measures = staff.measures || []
                    for (let m = 0; m < measures.length; ++m) {
                        const measure = measures[m]
                        const mx0 = Number(measure.x0) * w, mx1 = Number(measure.x1) * w
                        ctx.strokeStyle = root.css(Tk.Theme.color.border)
                        ctx.beginPath(); ctx.moveTo(mx0, top); ctx.lineTo(mx0, top + 4 * gap); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(mx1, top); ctx.lineTo(mx1, top + 4 * gap); ctx.stroke()
                        ctx.fillStyle = root.css(Tk.Theme.color.textMuted)
                        ctx.textBaseline = "top"
                        ctx.fillText(String(measure.number || ""), mx0 + Tk.Theme.space.xs, top - Tk.Theme.size.row)
                        const key = String(measure.keySignature || "")
                        if (key.length) ctx.fillText(key, mx0 + Tk.Theme.space.lg, top + 2 * gap)
                        const time = String(measure.timeSignature || "")
                        if (time.length) ctx.fillText(time, mx0 + Tk.Theme.size.fieldWidth / 3, top + 2 * gap)
                        const voices = measure.voices || []
                        for (let v = 0; v < voices.length && painted < root.maximumPaintEvents; ++v) {
                            const voice = voices[v], events = voice.events || []
                            for (let e = 0; e < events.length && painted < root.maximumPaintEvents; ++e, ++painted) {
                                const event = events[e]
                                const x = Number(event.x) * w
                                const y = top + (4 - Number(event.staffStep || 0)) * gap / 2
                                if (String(event.kind) === "rest") {
                                    ctx.fillStyle = root.css(event.id === root.selectedEventId ? Tk.Theme.color.accent : Tk.Theme.color.text)
                                    ctx.fillRect(x - Tk.Theme.space.xs, y - Tk.Theme.size.border, Tk.Theme.space.xs * 2, Tk.Theme.size.border * 2)
                                    if (Number(event.dotCount) > 0) {
                                        ctx.beginPath()
                                        ctx.arc(x + Tk.Theme.space.md, y, Tk.Theme.size.border, 0, Math.PI * 2)
                                        ctx.fill()
                                    }
                                } else {
                                    const selected = String(event.id) === root.selectedEventId
                                    ctx.fillStyle = root.css(selected ? Tk.Theme.color.accent : Tk.Theme.color.text)
                                    ctx.beginPath()
                                    ctx.ellipse(x, y, Tk.Theme.size.controlSm / 3, Tk.Theme.size.controlSm / 4,
                                                -0.25, 0, 2 * Math.PI)
                                    if (event.filledHead !== false) ctx.fill(); else ctx.stroke()
                                    const stem = String(event.stemDirection || "")
                                    if (stem.length) {
                                        const up = stem !== "down"
                                        const sx = x + (up ? Tk.Theme.size.controlSm / 3 : -Tk.Theme.size.controlSm / 3)
                                        const sy = y + (up ? 0 : 0)
                                        ctx.beginPath(); ctx.moveTo(sx, sy); ctx.lineTo(sx, sy + (up ? -3 : 3) * gap); ctx.stroke()
                                    }
                                    const accidental = String(event.accidental || "")
                                    if (accidental.length) { ctx.fillStyle = root.css(Tk.Theme.color.text); ctx.fillText(accidental, x - Tk.Theme.space.md, y) }
                                    const dots = Math.max(0, Math.min(3, Number(event.dotCount) || 0))
                                    for (let d = 0; d < dots; ++d) {
                                        ctx.beginPath(); ctx.arc(x + Tk.Theme.space.md + d * Tk.Theme.space.sm, y, Tk.Theme.size.border, 0, 2 * Math.PI); ctx.fill()
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (root.playbackCursorX >= 0) {
                const cx = root.playbackCursorX * w
                ctx.strokeStyle = root.css(Tk.Theme.color.accent)
                ctx.beginPath(); ctx.moveTo(cx, 0); ctx.lineTo(cx, h); ctx.stroke()
            }
        }
    }
    function repaint() { scoreCanvas.requestPaint() }
    onLayoutSnapshotChanged: root.repaint()
    onSnapshotRevisionChanged: root.repaint()
    onSelectedEventIdChanged: root.repaint()
    onPlaybackCursorXChanged: root.repaint()
    onWidthChanged: root.repaint()
    onHeightChanged: root.repaint()
    Tk.Caption {
        objectName: "scoreStatus"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.effectiveError.length > 0 || !root.layoutAvailable
                 || root.unsupportedFeatures.length > 0 || root._requestStatus.length > 0
        text: root.effectiveError.length ? root.effectiveError
              : !root.layoutAvailable ? qsTr("Notation layout unavailable")
              : root.unsupportedFeatures.length ? qsTr("Unsupported by provider: %1").arg(root.unsupportedFeatures.join(", "))
              : root._requestStatus
        color: root.effectiveError.length || !root.layoutAvailable || root.unsupportedFeatures.length
               ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        elide: Text.ElideRight
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: function(eventPoint) {
            const x = eventPoint.position.x, y = eventPoint.position.y
            const event = root.eventAt(x, y)
            if (event) root.requestSelection(String(event.id))
            else root.requestEntryAt(x, y)
        }
    }
    Keys.onEscapePressed: event => { root.acknowledgeRequest(qsTr("No pending score gesture")); event.accepted = true }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            root.navigate(event.key === Qt.Key_Left ? "previous" : "next")
            event.accepted = true
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            root.navigate(event.key === Qt.Key_Up ? "voice-up" : "voice-down")
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.requestCursorEntry()
            event.accepted = true
        }
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
