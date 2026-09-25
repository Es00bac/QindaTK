// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-067: ScoreView edits a real Tk.ScoreModel. Each view request maps to
// one model call; the view shows the model's layout snapshot (docs/notation.md).
TestCase {
    id: root
    name: "ScoreEdit"
    when: windowShown
    visible: true
    width: 620
    height: 880
    property var lastResult: null

    Tk.ScoreModel { id: model }

    Tk.ScoreView {
        id: view
        width: 595
        height: 842
        visible: true
        scoreId: model.scoreId
        scoreName: model.title
        layoutSnapshot: model.layoutRevision >= 0 ? model.layoutSnapshot(0) : ({ available: false })
        onNoteEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, pitch, duration) {
            root.lastResult = model.insertNote(partId, staffId, voiceId, measureId, ticks, pitch, duration)
        }
        onRestEntryRequested: function(requestId, scoreId, partId, staffId, voiceId, measureId, ticks, duration) {
            root.lastResult = model.insertRest(partId, staffId, voiceId, measureId, ticks, duration)
        }
        onNotationEditRequested: function(requestId, scoreId, eventId, edit, value) {
            root.lastResult = model.editEvent(eventId, edit, value)
        }
        onEventDeleteRequested: function(requestId, scoreId, eventId) {
            root.lastResult = model.deleteEvents([eventId])
        }
        onEventSelectionRequested: function(requestId, scoreId, eventId) {
            view.selectedEventId = eventId
        }
    }

    function init() {
        const made = model.newScore("Edit", 2, "4/4", 0)
        verify(made.ok)
        view.activePartId = made.partId
        view.activeStaffId = made.staffId
        view.activeVoiceId = made.voiceId
        view.activeMeasureId = model.measures[0].id
        view.entryTick = "0"
        view.entryPitch = "E4"
        view.entryDurationId = "quarter"
        view.entryMode = "note"
        view.selectedEventId = ""
        root.lastResult = null
    }

    function snapshotEvent(eventId) {
        return view.findEvent(eventId)
    }

    function test_keyboard_entry_edits_and_undo() {
        verify(view.requestCursorEntry())
        verify(root.lastResult.ok)
        const eventId = root.lastResult.eventId
        compare(model.eventData(eventId).notes[0].pitch, "E4")
        compare(snapshotEvent(eventId).heads[0].staffStep, 0) // E4 sits on the bottom line
        view.selectedEventId = eventId
        view.forceActiveFocus()
        keyClick(Qt.Key_Period)
        compare(model.eventData(eventId).durationId, "dotted-quarter")
        keyClick(Qt.Key_Up, Qt.ShiftModifier)
        compare(model.eventData(eventId).notes[0].pitch, "F4")
        compare(snapshotEvent(eventId).heads[0].staffStep, 1)
        keyClick(Qt.Key_4)
        compare(model.eventData(eventId).durationId, "eighth")
        compare(view.entryDurationId, "eighth")
        compare(view.selectedLabel, "Measure 1, beat 1, voice 1: eighth note F 4")
        keyClick(Qt.Key_Delete)
        verify(model.eventData(eventId).rest)
        verify(model.undo())
        verify(!model.eventData(eventId).rest)
        compare(model.undoText, "Change duration")
    }

    function test_rest_entry_and_refusal_leave_the_score_valid() {
        view.entryMode = "rest"
        view.entryDurationId = "half"
        view.entryTick = "1920"
        verify(view.requestCursorEntry())
        verify(root.lastResult.ok)
        compare(model.eventData(root.lastResult.eventId).tick, "1920")
        view.entryMode = "note"
        view.entryDurationId = "dotted-whole" // does not fit the measure
        verify(view.requestCursorEntry())
        verify(!root.lastResult.ok)
        verify(model.lastError.length > 0)
        const report = model.rhythmReport()
        for (let i = 0; i < report.length; ++i) compare(report[i].status, "complete")
    }

    function test_click_enters_at_an_exact_position() {
        const staff = view.layoutSnapshot.systems[0].staves[0]
        const measure = staff.measures[0]
        let position = null
        for (let i = 0; i < measure.entryPositions.length; ++i)
            if (measure.entryPositions[i].ticks === "2880") position = measure.entryPositions[i]
        verify(position !== null)
        mouseClick(view, position.x * view.width, (staff.top + 2 * staff.lineGap) * view.height)
        verify(root.lastResult !== null && root.lastResult.ok)
        const data = model.eventData(root.lastResult.eventId)
        compare(data.tick, "2880")
        compare(data.notes[0].pitch, "E4")
        compare(data.durationId, "quarter")
    }
}
