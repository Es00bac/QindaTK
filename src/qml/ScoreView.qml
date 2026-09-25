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
// The reference host is Tk.ScoreModel (WP-067, D-178): its layoutSnapshot(page)
// is this view's snapshot and every request below maps to one of its calls
// (docs/notation.md#editing). Drawing is Tk.ScoreCanvasItem, the same C++
// painter as PDF and image output (D-179).
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
    // The host's spoken description of the selected event (snapshot `label`).
    readonly property string selectedLabel: {
        const event = root.selectedEventId.length ? root.findEvent(root.selectedEventId) : null
        return event && event.label ? String(event.label) : ""
    }

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
    signal eventDeleteRequested(string requestId, string scoreId, string eventId)
    signal notationEditRequested(string requestId, string scoreId, string eventId, string edit, var value)
    signal playbackLocateRequested(string measureId, string positionTicks)

    objectName: "scoreView"
    implicitWidth: Tk.Theme.size.sparkline * 4
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length ? root.tooltip
        : (root.scoreName.length ? root.scoreName : qsTr("Score view"))
    Accessible.description: root.effectiveError.length ? root.effectiveError
        : root.unsupportedFeatures.length ? qsTr("Unsupported notation features: %1").arg(root.unsupportedFeatures.join(", "))
        : root.selectedLabel.length ? root.selectedLabel
        : root.layoutAvailable ? qsTr("Notation score. Use arrow keys to navigate events; Enter requests note or rest entry at the host cursor.")
        : qsTr("Notation layout is not available")
    activeFocusOnTab: true
    focus: true

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
    function findEvent(eventId) {
        const staves = root.allStaves()
        for (let i = 0; i < staves.length; ++i) {
            const measures = staves[i].staff.measures || []
            for (let m = 0; m < measures.length; ++m) {
                const voices = measures[m].voices || []
                for (let v = 0; v < voices.length; ++v) {
                    const events = voices[v].events || []
                    for (let e = 0; e < events.length; ++e)
                        if (String(events[e].id) === String(eventId)) return events[e]
                }
            }
        }
        return null
    }
    function requestNotationEdit(edit, value) {
        if (!root.enabled || !root.layoutAvailable || !root.selectedEventId.length) return false
        root.notationEditRequested(root.createRequestId("score-edit"), root.scoreId, root.selectedEventId,
                                   String(edit), value === undefined ? "" : value)
        return true
    }
    function requestDelete() {
        if (!root.enabled || !root.layoutAvailable || !root.selectedEventId.length) return false
        root.eventDeleteRequested(root.createRequestId("score-delete"), root.scoreId, root.selectedEventId)
        return true
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
        const event = root.findEvent(eventId)
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
    Tk.ScoreCanvasItem {
        id: scoreCanvas
        objectName: "scoreCanvas"
        anchors.fill: parent
        snapshot: root.layoutAvailable ? root.layoutSnapshot : ({})
        selectedEventId: root.selectedEventId
        cursorX: root.playbackCursorX
        maximumEvents: root.maximumPaintEvents
        surfaceColor: Tk.Theme.color.surface
        inkColor: Tk.Theme.color.text
        mutedColor: Tk.Theme.color.textMuted
        borderColor: Tk.Theme.color.textMuted
        accentColor: Tk.Theme.color.accent
        fontFamily: Tk.Theme.font.family
    }
    function repaint() { scoreCanvas.update() }
    onSnapshotRevisionChanged: root.repaint()
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
        } else if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down) && (event.modifiers & Qt.ShiftModifier)) {
            root.requestNotationEdit("step", event.key === Qt.Key_Up ? 1 : -1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            root.navigate(event.key === Qt.Key_Up ? "voice-up" : "voice-down")
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            root.requestDelete()
            event.accepted = true
        } else if (event.key === Qt.Key_Period) {
            root.requestNotationEdit("dot")
            event.accepted = true
        } else if (event.key === Qt.Key_T) {
            root.requestNotationEdit("tie")
            event.accepted = true
        } else if (event.key === Qt.Key_J) {
            root.requestNotationEdit("respell")
            event.accepted = true
        } else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_7) {
            // 1 = 64th ... 7 = whole, as in common notation editors.
            const names = ["64th", "32nd", "16th", "eighth", "quarter", "half", "whole"]
            root.entryDurationId = names[event.key - Qt.Key_1]
            root.requestNotationEdit("duration", root.entryDurationId)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.requestCursorEntry()
            event.accepted = true
        }
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
