// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.AppWindow {
    id: window
    title: qsTr("Local calendar projection")
    width: 900
    height: 600
    property string rangeStart: "2026-09-21T00:00:00Z"
    property string rangeEnd: "2026-09-28T00:00:00Z"
    property string rangeLabel: "Sep 21–27, 2026"

    function moveRange(direction) {
        const delta = direction === "next" ? 7 : -7
        const start = new Date(rangeStart)
        const end = new Date(rangeEnd)
        start.setUTCDate(start.getUTCDate() + delta)
        end.setUTCDate(end.getUTCDate() + delta)
        rangeStart = start.toISOString()
        rangeEnd = end.toISOString()
        rangeLabel = start.toISOString().slice(0, 10) + " – "
            + new Date(end.getTime() - 86400000).toISOString().slice(0, 10)
        projector.project(rangeStart, rangeEnd)
    }
    function seedCalendar() {
        if (!store.load()) {
            store.upsert("events", {
                id: "event-weekly-review", eventId: "event-weekly-review",
                title: qsTr("Weekly planning review"), startLocal: "2026-09-24T09:30:00",
                endLocal: "2026-09-24T10:15:00", zone: "America/Denver", allDay: false,
                calendarId: "studio",
                recurrenceRule: { frequency: "weekly", interval: 1, weekdays: [4], endMode: "never" },
                exceptions: ["event-weekly-review/2026-10-01T09:30:00"]
            })
            store.upsert("events", {
                id: "event-one-off", eventId: "event-one-off", title: qsTr("Equipment pickup"),
                startLocal: "2026-09-22T13:00:00", endLocal: "2026-09-22T13:30:00",
                zone: "America/Denver", allDay: false, calendarId: "studio"
            })
            store.save()
        }
        projector.project(rangeStart, rangeEnd)
    }

    Tk.LocalProductivityStore { id: store; filePath: defaultPath + ".calendar-projection" }
    Tk.RecurrenceService { id: recurrence }
    Tk.LocalCalendarProjectionModel {
        id: projector
        store: store
        recurrenceService: recurrence
    }
    Component.onCompleted: seedCalendar()

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Overline { title: qsTr("Calendar · persisted recurrence projection"); wide: true }
        Tk.CalendarView {
            id: calendar
            Tk.Flex.grow: 1
            width: parent.width
            events: projector.occurrences
            rangeLabel: window.rangeLabel
            zone: "America/Denver"
            errorText: projector.error || store.lastError
            onNavigateRequested: function(requestId, direction) {
                window.moveRange(direction)
                acknowledge(requestId, "accepted")
            }
        }
        Tk.Notice {
            variant: "info"
            title: qsTr("Projection boundary")
            text: qsTr("Agenda rows are generated from the local event snapshot and recurrence service. Navigation changes a UTC range; event IDs, occurrence IDs, wall times and IANA zone are retained.")
        }
    }
}
