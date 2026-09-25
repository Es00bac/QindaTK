// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// CalendarGridView over the local store: LocalCalendarProjectionModel
// expands the stored events (RecurrenceService) into the grid's visible
// range, and every gesture comes back as a request that this host applies
// to the store and acknowledges. Moves use store.shiftEventLocal (scoped for
// repeating events); a resize rewrites endLocal (the whole event) or adds a
// single-occurrence override. The grid redraws only when the projection
// changes. Literal colours below are demo data (calendar colours).
Rectangle {
    id: root
    width: 1280
    height: 800
    color: Tk.Theme.color.bg

    // A real host passes the wall clock in the calendar's zone and refreshes
    // it every minute; the demo pins it so screenshots are reproducible.
    property string now: "2026-09-24T10:40:00"
    property string message: qsTr("Drag to move, drag the bottom edge to resize, drag empty time to create.")
    readonly property var modes: [{ text: qsTr("Day"), value: "day" }, { text: qsTr("Work week"), value: "workweek" },
                                  { text: qsTr("Week"), value: "week" }, { text: qsTr("Month"), value: "month" }]
    readonly property var scopes: [{ text: qsTr("This event"), value: "occurrence" }, { text: qsTr("This and following"), value: "following" },
                                   { text: qsTr("All events"), value: "series" }]

    Tk.LocalProductivityStore { id: store; filePath: defaultPath + ".calendar-grid" }
    Tk.RecurrenceService { id: recurrence }
    Tk.LocalCalendarProjectionModel { id: projector; store: store; recurrenceService: recurrence }

    function record(eventId) { return store.events.find(function(row) { return String(row.eventId || row.id) === eventId }) || null }
    // Wall-clock minutes on local text; the store validates zones on save paths.
    function addMinutes(local, minutes) {
        const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(local)
        const t = new Date(Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5]) + minutes * 60000)
        const p = function(n) { return (n < 10 ? "0" : "") + n }
        return t.getUTCFullYear() + "-" + p(t.getUTCMonth() + 1) + "-" + p(t.getUTCDate()) + "T" + p(t.getUTCHours()) + ":" + p(t.getUTCMinutes()) + ":00"
    }
    // The grid reports civil dates; a day of margin on both sides covers
    // every zone offset, and rows outside the visible days are not drawn.
    function project(startDate, endDate) {
        const start = new Date(startDate + "T00:00:00Z"), end = new Date(endDate + "T00:00:00Z")
        start.setUTCDate(start.getUTCDate() - 1); end.setUTCDate(end.getUTCDate() + 1)
        projector.project(start.toISOString(), end.toISOString())
    }
    function apply(requestId, eventId, proposed) {
        const row = record(eventId)
        let result = { ok: false, error: qsTr("Event not found") }
        if (row && proposed.kind === "shift-local") {
            result = store.shiftEventLocal(eventId, proposed.minutes, proposed.scope, proposed.occurrenceId)
        } else if (row && proposed.kind === "resize-local" && proposed.scope === "occurrence" && proposed.occurrenceId) {
            const shown = projector.occurrences.find(function(o) { return o.occurrenceId === proposed.occurrenceId })
            result = shown ? store.setOccurrenceOverride(eventId, proposed.occurrenceId,
                                                         { startLocal: shown.startLocal, endLocal: addMinutes(shown.endLocal, proposed.minutes) })
                           : result
        } else if (row && proposed.kind === "resize-local" && proposed.scope === "series") {
            result = store.upsert("events", Object.assign({}, row, { endLocal: addMinutes(row.endLocal, proposed.minutes) }))
        } else if (row) {
            result = { ok: false, error: qsTr("Resizing this and following is not supported here") }
        }
        const ok = result.ok && store.save()
        grid.acknowledge(requestId, ok ? "accepted" : "rejected", ok ? qsTr("Saved") : result.error || store.lastError)
        message = ok ? qsTr("%1 saved · undo is available").arg(proposed.kind) : result.error || store.lastError
    }
    function create(startLocal, endLocal, allDay) {
        const id = "event-" + Date.now()
        store.upsert("events", { id: id, eventId: id, title: qsTr("New event"), startLocal: startLocal, endLocal: endLocal,
                                 zone: "America/Denver", allDay: allDay, calendarId: "personal" })
        message = store.save() ? qsTr("Created %1").arg(startLocal) : store.lastError
    }
    function remove(eventId, occurrenceId) {
        const row = record(eventId)
        const result = row && row.recurrenceRule && row.recurrenceRule.frequency
            ? store.setOccurrenceExcluded(eventId, occurrenceId, true) : store.remove("events", String(row ? row.id : eventId))
        message = result.ok && store.save() ? qsTr("Deleted") : result.error || store.lastError
    }
    function seed() {
        const zone = "America/Denver"
        const add = function(row) { store.upsert("events", Object.assign({ eventId: row.id, zone: zone, allDay: false, exceptions: [], overrides: [] }, row)) }
        add({ id: "standup", title: qsTr("Standup"), startLocal: "2026-09-21T09:00:00", endLocal: "2026-09-21T09:15:00", calendarId: "work",
              recurrenceRule: { frequency: "weekly", interval: 1, weekdays: [1, 2, 3, 4, 5], endMode: "never" } })
        add({ id: "review", title: qsTr("Design review"), startLocal: "2026-09-24T10:00:00", endLocal: "2026-09-24T11:30:00", calendarId: "work",
              location: qsTr("Studio B"), recurrenceRule: { frequency: "weekly", interval: 1, weekdays: [4], endMode: "never" } })
        add({ id: "focus", title: qsTr("Focus time"), startLocal: "2026-09-24T10:15:00", endLocal: "2026-09-24T12:30:00", calendarId: "personal" })
        add({ id: "lunch", title: qsTr("Lunch with Mira"), startLocal: "2026-09-24T12:30:00", endLocal: "2026-09-24T13:30:00", calendarId: "personal", location: qsTr("Café Rio") })
        add({ id: "board", title: qsTr("Prepare board deck"), startLocal: "2026-09-23T14:00:00", endLocal: "2026-09-23T16:30:00", calendarId: "work", location: qsTr("Room 4") })
        add({ id: "one-on-one", title: qsTr("1:1 with Ari"), startLocal: "2026-09-22T15:00:00", endLocal: "2026-09-22T15:30:00", calendarId: "work" })
        add({ id: "deploy", title: qsTr("Deploy window"), startLocal: "2026-09-25T17:00:00", endLocal: "2026-09-25T19:00:00", calendarId: "work" })
        add({ id: "offsite", title: qsTr("Studio offsite"), startLocal: "2026-09-26", endLocal: "2026-09-28", allDay: true, calendarId: "personal" })
        add({ id: "equinox", title: qsTr("Autumn equinox"), startLocal: "2026-09-22", endLocal: "2026-09-23", allDay: true, calendarId: "holidays", readOnly: true })
        store.save()
    }

    Component.onCompleted: {
        if (!store.load()) seed()
        project(grid.rangeStart, grid.rangeEnd)
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            wrap: Tk.Flex.Wrap
            Tk.Button { objectName: "calendarToday"; text: qsTr("Today"); tooltip: qsTr("Go to today"); onClicked: grid.goToday(root.now.slice(0, 10)) }
            Tk.IconButton { objectName: "calendarPrevious"; iconName: "chevron-left"; tooltip: qsTr("Previous"); onClicked: grid.navigate(-1) }
            Tk.IconButton { objectName: "calendarNext"; iconName: "chevron-right"; tooltip: qsTr("Next"); onClicked: grid.navigate(1) }
            Tk.Heading { objectName: "calendarTitle"; level: 2; text: grid.title; Tk.Flex.grow: 1; Tk.Flex.basis: 0; Tk.Flex.minWidth: Tk.Theme.size.fieldWidth }
            Tk.Segmented {
                objectName: "calendarScope"
                small: true
                model: root.scopes
                currentIndex: 0
                tooltip: qsTr("What a move or resize of a repeating event changes")
                onActivated: function(index) { grid.moveScope = root.scopes[index].value }
            }
            Tk.Segmented {
                objectName: "calendarMode"
                model: root.modes
                currentIndex: 2
                tooltip: qsTr("Calendar view")
                onActivated: function(index) { grid.setMode(root.modes[index].value) }
            }
        }
        Tk.Box {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            borderWidth: Tk.Theme.size.border
            borderColor: Tk.Theme.color.border
            radius: Tk.Theme.radius.md
            clipContent: true
            Tk.CalendarGridView {
                id: grid
                anchorDate: "2026-09-24"
                now: root.now
                zone: "America/Denver"
                calendars: [{ id: "work", name: qsTr("Work"), color: "#4f7cff" }, { id: "personal", name: qsTr("Personal"), color: "#22b37a" },
                            { id: "holidays", name: qsTr("Holidays") }]
                events: projector.occurrences
                errorText: projector.error
                onRangeChanged: function(start, end) { root.project(start, end) }
                onEventChangeRequested: function(requestId, eventId, proposed) { root.apply(requestId, eventId, proposed) }
                onCreateRequested: function(startLocal, endLocal, allDay) { root.create(startLocal, endLocal, allDay) }
                onDeleteRequested: function(eventId, occurrenceId) { root.remove(eventId, occurrenceId) }
                onOpenRequested: function(eventId, occurrenceId) { root.message = qsTr("Open %1").arg(occurrenceId) }
                onMoreRequested: function(date) { grid.anchorDate = date; grid.setMode("day") }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Caption { objectName: "calendarStatus"; text: root.message; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Button { text: qsTr("Undo"); small: true; tooltip: qsTr("Undo the last calendar change"); enabled: store.canUndoEventChange
                        onClicked: { const r = store.undoEventChange(); root.message = r.ok && store.save() ? qsTr("Undone") : r.error || store.lastError } }
        }
    }
}
