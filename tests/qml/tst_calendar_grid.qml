// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// CalendarGridView (D-301): visible ranges per mode and week start, the
// month 6-week grid across a year boundary, overlap column packing, the
// all-day lane, month "+k more", pointer and keyboard proposals (snapped,
// scoped, refused for read-only rows), create by drag, acknowledgment
// matching, objectNames, accessible names, geometry at two densities, and
// that the view never mutates `events`.
TestCase {
    id: root
    name: "CalendarGrid"
    visible: true
    when: windowShown
    width: 1100
    height: 800

    Component {
        id: gridComponent
        Tk.CalendarGridView {
            width: 1100
            height: 760
            locale: Qt.locale("en_US")
            use24Hour: true
            weekStart: 1
            anchorDate: "2026-09-24"
            now: "2026-09-24T10:40:00"
            zone: "America/Denver"
        }
    }
    Component { id: spyComponent; SignalSpy { } }

    function settle(item) { waitForRendering(item); wait(0) }
    function spy(target, signalName) { return createTemporaryObject(spyComponent, root, { target: target, signalName: signalName }) }
    function make(props) { const grid = createTemporaryObject(gridComponent, root, props || {}); verify(grid); settle(grid); return grid }
    function ev(id, start, end, extra) {
        return Object.assign({ eventId: id, occurrenceId: id, title: id, startLocal: start, endLocal: end,
                               zone: "America/Denver", calendarId: "work" }, extra || {})
    }
    // SignalSpy keeps each emission as an Arguments object.
    function args(signalSpy, index) { return Array.prototype.slice.call(signalSpy.signalArguments[index]) }
    // Rows cross a QVariant boundary, which reorders keys: compare canonically.
    function canonical(rows) { return JSON.stringify(rows.map(function(row) { const out = {}; Object.keys(row).sort().forEach(function(k) { out[k] = row[k] }); return out })) }
    function block(grid, key) { const item = findChild(grid, "calendarGridEvent_" + key); verify(item, key); return item }
    function dragBy(item, x, y, dx, dy) {
        mousePress(item, x, y)
        mouseMove(item, x + 12, y + 12, -1, Qt.LeftButton)   // past the drag threshold
        mouseMove(item, x + dx, y + dy, -1, Qt.LeftButton)
    }

    function test_ranges_per_mode_and_week_start() {
        const grid = make()
        compare(grid.rangeStart, "2026-09-21"); compare(grid.rangeEnd, "2026-09-28"); compare(grid.days.length, 7)
        compare(grid.title, "21 – 27 Sep 2026")
        grid.weekStart = 7
        compare(grid.rangeStart, "2026-09-20"); compare(grid.rangeEnd, "2026-09-27")
        grid.weekStart = 6
        compare(grid.rangeStart, "2026-09-19")
        verify(grid.setMode("workweek"))
        compare(grid.days, ["2026-09-21", "2026-09-22", "2026-09-23", "2026-09-24", "2026-09-25"], "work days of the Saturday-start week")
        compare(grid.rangeEnd, "2026-09-26")
        grid.workDays = [2, 4]
        compare(grid.days, ["2026-09-22", "2026-09-24"])
        verify(grid.setMode("day"))
        compare(grid.days, ["2026-09-24"]); compare(grid.rangeEnd, "2026-09-25")
        compare(grid.title, "Thursday 24 September 2026")
        verify(!grid.setMode("year"))
        grid.weekStart = 1
        verify(grid.setMode("month"))
        compare(grid.days.length, 42)
        compare(grid.rangeStart, "2026-08-31"); compare(grid.rangeEnd, "2026-10-12")
        compare(grid.title, "September 2026")
        grid.weekStart = 7
        compare(grid.rangeStart, "2026-08-30"); compare(grid.rangeEnd, "2026-10-11")
    }

    function test_month_grid_across_year_boundary_and_navigation() {
        const grid = make({ mode: "month", anchorDate: "2026-12-15" })
        compare(grid.rangeStart, "2026-11-30"); compare(grid.days[41], "2027-01-10"); compare(grid.rangeEnd, "2027-01-11")
        const ranges = spy(grid, "rangeChanged")
        verify(grid.navigate(1))
        compare(grid.anchorDate, "2027-01-15")
        compare(grid.rangeStart, "2026-12-28"); compare(grid.days[41], "2027-02-07")
        compare(ranges.count, 1)
        compare(args(ranges, 0), ["2026-12-28", "2027-02-08"])
        verify(findChild(grid, "calendarGridMonthCell_2026-12-28")); verify(findChild(grid, "calendarGridMonthCell_2027-02-07"))
        verify(!findChild(grid, "calendarGridMonthCell_2027-02-08"))
        grid.anchorDate = "2026-01-31"
        grid.navigate(1)
        compare(grid.anchorDate, "2026-02-28", "a month step clamps the day")
        grid.setMode("week"); grid.anchorDate = "2026-12-31"
        compare(grid.title, "28 Dec 2026 – 3 Jan 2027")
        grid.navigate(-1)
        compare(grid.title, "21 – 27 Dec 2026")
        grid.anchorDate = "2026-09-30"
        compare(grid.title, "28 Sep – 4 Oct 2026")
        verify(grid.goToday())
        compare(grid.anchorDate, "2026-09-24", "goToday() without an argument uses the host's now")
        verify(grid.goToday("2026-10-02")); compare(grid.rangeStart, "2026-09-28")
    }

    function test_overlap_columns_are_deterministic() {
        const rows = [ev("a", "2026-09-24T10:00:00", "2026-09-24T11:30:00"), ev("b", "2026-09-24T10:15:00", "2026-09-24T12:30:00"),
                      ev("c", "2026-09-24T10:30:00", "2026-09-24T11:00:00"), ev("d", "2026-09-24T12:00:00", "2026-09-24T13:00:00"),
                      ev("e", "2026-09-24T14:00:00", "2026-09-24T15:00:00"),
                      ev("tie-b", "2026-09-24T16:00:00", "2026-09-24T17:00:00"), ev("tie-a", "2026-09-24T16:00:00", "2026-09-24T17:00:00"),
                      ev("tie-long", "2026-09-24T16:00:00", "2026-09-24T18:00:00"),
                      ev("x1", "2026-09-24T03:00:00", "2026-09-24T05:00:00"), ev("x2", "2026-09-24T03:00:00", "2026-09-24T04:00:00"),
                      ev("x3", "2026-09-24T03:00:00", "2026-09-24T03:30:00"), ev("x4", "2026-09-24T04:00:00", "2026-09-24T05:00:00")]
        const grid = make({ mode: "day", events: rows })
        const a = block(grid, "a"), b = block(grid, "b"), c = block(grid, "c"), d = block(grid, "d"), e = block(grid, "e")
        verify(a.x < b.x && b.x < c.x, "one cluster, three columns in start order")
        fuzzyCompare(a.width, b.width, 1); fuzzyCompare(b.width, c.width, 1)
        compare(d.x, a.x, "12:00 reuses the first free column")
        verify(e.width > a.width * 2.5, "a lone event takes the whole day")
        const long = block(grid, "tie-long"), tieA = block(grid, "tie-a"), tieB = block(grid, "tie-b")
        verify(long.x < tieA.x && tieA.x < tieB.x, "same start: longer first, then eventId")
        const x2 = block(grid, "x2"), x4 = block(grid, "x4")
        compare(x4.x, x2.x, "x4 takes the column x2 freed")
        fuzzyCompare(x4.width, x2.width * 2 + Tk.Theme.size.border, 1, "and widens into the free column on its right")
    }

    function test_geometry_follows_start_time_at_both_densities() {
        const oldDensity = Tk.Density.mode
        try {
            for (const density of [Tk.Density.Compact, Tk.Density.Comfortable]) {
                Tk.Density.mode = density
                const grid = make({ mode: "day", events: [ev("slot", "2026-09-24T09:30:00", "2026-09-24T10:15:00")] })
                const hour = grid.hourHeight
                compare(hour, Tk.Theme.size.row * 2)
                const item = block(grid, "slot")
                compare(item.y, 9.5 * hour + Tk.Theme.size.border)
                fuzzyCompare(item.height, 0.75 * hour - Tk.Theme.size.border, 0.01)
                const now = findChild(grid, "calendarGridNowLine")
                verify(now.visible)
                fuzzyCompare(now.y + now.height / 2, (10 + 40 / 60) * hour, 0.01)
            }
        } finally {
            Tk.Density.mode = oldDensity
        }
    }

    function test_all_day_lane_and_overflow() {
        const rows = [ev("span", "2026-09-21", "2026-09-24", { allDay: true }), ev("tue", "2026-09-22", "2026-09-23", { allDay: true }),
                      ev("long", "2026-09-24T12:00:00", "2026-09-25T12:00:00"), ev("timed", "2026-09-22T09:00:00", "2026-09-22T09:15:00")]
        const grid = make({ events: rows })
        const lane = findChild(grid, "calendarGridAllDay")
        const span = findChild(lane, "calendarGridEvent_span"), tue = findChild(lane, "calendarGridEvent_tue")
        verify(span && tue && findChild(lane, "calendarGridEvent_long"), "all-day and 24 h rows sit in the lane")
        verify(!findChild(lane, "calendarGridEvent_timed"))
        const column = findChild(grid, "calendarGridDay_2026-09-22").width
        verify(span.width > column * 2.5, "a three-day bar spans three columns")
        verify(tue.y > span.y, "an overlapping bar takes the next lane")
        const crowded = []
        for (let i = 0; i < 4; ++i) crowded.push(ev("fri-" + i, "2026-09-25", "2026-09-26", { allDay: true }))
        grid.events = crowded
        settle(grid)
        const more = findChild(grid, "calendarGridMore_2026-09-25")
        verify(more && more.visible)
        compare(more.text, "+2 more", "two lanes shown, the third lane is the more row")
        const moreSpy = spy(grid, "moreRequested")
        mouseClick(more)
        compare(moreSpy.signalArguments[0][0], "2026-09-25")
    }

    function test_month_cells_more_and_create() {
        const rows = []
        for (let i = 0; i < 5; ++i) rows.push(ev("m" + i, "2026-09-24T0" + (i + 1) + ":00:00", "2026-09-24T0" + (i + 1) + ":30:00"))
        rows.push(ev("allday", "2026-09-24", "2026-09-25", { allDay: true }))
        const grid = make({ mode: "month", height: 480, events: rows })
        const cell = findChild(grid, "calendarGridMonthCell_2026-09-24")
        const more = findChild(grid, "calendarGridMore_2026-09-24")
        verify(cell && more && more.visible)
        let chips = 0
        for (const row of rows) {
            const chip = findChild(cell, "calendarGridEvent_" + row.occurrenceId)
            if (!chip) continue
            ++chips
            verify(chip.y + chip.height <= cell.height, "chips stay inside their cell")
        }
        verify(findChild(cell, "calendarGridEvent_allday"), "all-day chips come first")
        compare(chips + more.count, rows.length)
        verify(more.y + more.height <= cell.height)
        const moreSpy = spy(grid, "moreRequested")
        mouseClick(more)
        compare(moreSpy.signalArguments[0][0], "2026-09-24")
        const created = spy(grid, "createRequested")
        const empty = findChild(grid, "calendarGridMonthCell_2026-09-10")
        mouseDoubleClickSequence(empty, empty.width / 2, empty.height - 4)
        compare(args(created, 0), ["2026-09-10", "2026-09-11", true])
        // Month moves are whole days, for timed rows too.
        const changes = spy(grid, "eventChangeRequested")
        const chip = findChild(cell, "calendarGridEvent_allday")
        dragBy(chip, chip.width / 2, chip.height / 2, cell.width, 0)
        mouseRelease(chip, chip.width / 2 + cell.width, chip.height / 2)
        compare(changes.count, 1)
        compare(changes.signalArguments[0][2].minutes, 1440)
    }

    function test_drag_move_proposes_snapped_shift_and_acknowledges() {
        const rows = [ev("move", "2026-09-22T09:00:00", "2026-09-22T10:00:00")]
        const before = canonical(rows)
        const grid = make({ events: rows })
        const changes = spy(grid, "eventChangeRequested")
        const item = block(grid, "move")
        const column = findChild(grid, "calendarGridDay_2026-09-22").width
        const x = item.width / 2, y = item.height / 2
        dragBy(item, x, y, column, grid.hourHeight / 2 + 3)
        verify(findChild(grid, "calendarGridGhost"), "a ghost shows the proposed time")
        compare(grid.dragState.delta, 1440 + 30)
        mouseRelease(item, x + column, y + grid.hourHeight / 2 + 3)
        compare(changes.count, 1)
        const emitted = args(changes, 0)
        compare(emitted[1], "move")
        compare(emitted[2], { kind: "shift-local", minutes: 1470, zone: "America/Denver", scope: "series", occurrenceId: "" })
        compare(canonical(grid.events), before, "the view never moves the row itself")
        verify(grid.dragState.pending && findChild(grid, "calendarGridGhost"), "the ghost waits for the host")
        verify(!grid.acknowledge("other-request", "accepted", ""))
        verify(grid.acknowledge(emitted[0], "accepted", "Saved"))
        compare(grid.requestState, "accepted")
        verify(!grid.dragState)
        verify(!grid.acknowledge(emitted[0], "accepted", ""), "one acknowledgment per request")
        // Escape cancels a drag in progress; the release then proposes nothing.
        const moved = block(grid, "move")
        dragBy(moved, x, y, 0, grid.hourHeight)
        verify(grid.dragState)
        keyClick(Qt.Key_Escape)
        verify(!grid.dragState)
        mouseRelease(moved, x, y + grid.hourHeight)
        compare(changes.count, 1)
    }

    function test_resize_scope_and_read_only_refusal() {
        const recurring = ev("weekly", "2026-09-23T13:00:00", "2026-09-23T14:00:00",
                             { occurrenceId: "weekly/2026-09-23T13:00:00", recurrenceRule: { frequency: "weekly" } })
        const locked = ev("locked", "2026-09-21T13:00:00", "2026-09-21T14:00:00", { readOnly: true })
        const grid = make({ events: [recurring, locked], moveScope: "following" })
        const changes = spy(grid, "eventChangeRequested")
        const item = block(grid, "weekly/2026-09-23T13:00:00")
        const x = item.width / 2, y = item.height - 2
        dragBy(item, x, y, 0, grid.hourHeight)
        mouseRelease(item, x, y + grid.hourHeight)
        compare(changes.signalArguments[0][2], { kind: "resize-local", minutes: 60, zone: "America/Denver",
                                                 scope: "following", occurrenceId: "weekly/2026-09-23T13:00:00" })
        const again = block(grid, "weekly/2026-09-23T13:00:00")
        dragBy(again, x, y, 0, -grid.hourHeight * 3)
        mouseRelease(again, x, y - grid.hourHeight * 3)
        compare(changes.signalArguments[1][2].minutes, -45, "an event keeps at least one snap step")
        verify(grid.acknowledge(grid.lastRequestId, "accepted", ""))
        const lockedItem = block(grid, "locked")
        dragBy(lockedItem, 10, 10, 0, grid.hourHeight)
        verify(!grid.dragState, "read-only rows cannot be dragged")
        mouseRelease(lockedItem, 10, 10 + grid.hourHeight)
        compare(changes.count, 2)
        compare(grid.proposeMove(locked, 15), "")
        grid.readOnly = true
        compare(grid.proposeMove(recurring, 15), "")
        grid.readOnly = false; grid.offline = true
        compare(grid.proposeMove(recurring, 15), "")
        verify(findChild(grid, "calendarGridNotice").visible)
        grid.offline = false
        compare(grid.proposeMove(ev("allday", "2026-09-22", "2026-09-23", { allDay: true }), 60), "", "all-day rows move in whole days")
    }

    function test_create_by_drag_and_double_click() {
        const grid = make()
        const created = spy(grid, "createRequested")
        const day = findChild(grid, "calendarGridDay_2026-09-23")
        const hour = grid.hourHeight
        mousePress(day, day.width / 2, 14 * hour + 2)
        mouseMove(day, day.width / 2, 14 * hour + 20, -1, Qt.LeftButton)
        mouseMove(day, day.width / 2, (15 + 10 / 60) * hour, -1, Qt.LeftButton)
        verify(findChild(grid, "calendarGridGhost"))
        mouseRelease(day, day.width / 2, (15 + 10 / 60) * hour)
        compare(args(created, 0), ["2026-09-23T14:00:00", "2026-09-23T15:15:00", false])
        mouseDoubleClickSequence(day, day.width / 2, (16 + 7 / 60) * hour)
        compare(args(created, 1), ["2026-09-23T16:00:00", "2026-09-23T16:30:00", false])
        const lane = findChild(grid, "calendarGridAllDay")
        const gutter = findChild(grid, "calendarGridGutter").width
        mouseDoubleClickSequence(lane, gutter + day.width * 2.5, lane.height / 2)
        compare(args(created, 2), ["2026-09-23", "2026-09-24", true])
        compare(grid.events.length, 0, "creating is a request too")
    }

    function test_keyboard_moves_traversal_and_focus_restore() {
        const standup = ev("standup", "2026-09-22T09:00:00", "2026-09-22T09:15:00", { title: "Standup" })
        const rows = [standup, ev("review", "2026-09-22T11:00:00", "2026-09-22T12:00:00"),
                      ev("offsite", "2026-09-25", "2026-09-26", { allDay: true })]
        const grid = make({ events: rows })
        const changes = spy(grid, "eventChangeRequested"), opened = spy(grid, "openRequested"), deleted = spy(grid, "deleteRequested")
        const item = block(grid, "standup")
        item.forceActiveFocus()
        keyClick(Qt.Key_Down, Qt.AltModifier)
        keyClick(Qt.Key_Up, Qt.ControlModifier)
        keyClick(Qt.Key_Right, Qt.AltModifier)
        keyClick(Qt.Key_Down, Qt.AltModifier | Qt.ShiftModifier)
        compare(changes.signalArguments.map(function(a) { return a[2].kind + ":" + a[2].minutes }),
                ["shift-local:15", "shift-local:-15", "shift-local:1440", "resize-local:15"])
        keyClick(Qt.Key_Return)
        compare(args(opened, 0), ["standup", "standup"])
        keyClick(Qt.Key_Delete)
        compare(args(deleted, 0), ["standup", "standup"])
        keyClick(Qt.Key_Down)
        verify(block(grid, "review").activeFocus, "arrows walk events in time order")
        compare(grid.selectedOccurrenceId, "review")
        keyClick(Qt.Key_Tab)
        const offsite = block(grid, "offsite")
        verify(offsite.activeFocus)
        keyClick(Qt.Key_Down, Qt.AltModifier)
        compare(changes.signalArguments[4][2].minutes, 1440, "all-day rows move by days")
        // The host re-projects the moved row: focus follows the occurrence.
        grid.events = [standup, rows[1], ev("offsite", "2026-09-26", "2026-09-27", { allDay: true })]
        tryVerify(function() { const moved = findChild(grid, "calendarGridEvent_offsite"); return moved && moved.activeFocus })
        compare(block(grid, "standup").Accessible.name, "Standup, Tuesday 22 September, 09:00 to 09:15")
    }

    function test_object_names_colours_and_hidden_calendars() {
        const rows = [ev("red", "2026-09-22T09:00:00", "2026-09-22T10:00:00", { calendarId: "red" }),
                      ev("hidden", "2026-09-23T09:00:00", "2026-09-23T10:00:00", { calendarId: "private" }),
                      ev("plain", "2026-09-24T09:00:00", "2026-09-24T10:00:00", { calendarId: "" })]
        const grid = make({ events: rows, calendars: [{ id: "red", name: "Red", color: "#ff0000" }, { id: "private", visible: false }] })
        for (const name of ["calendarGrid", "calendarGridHeader", "calendarGridGutter", "calendarGridAllDay", "calendarGridScroll",
                            "calendarGridDay_2026-09-21", "calendarGridDay_2026-09-27", "calendarGridNowLine"])
            verify(grid.objectName === name || findChild(grid, name), name)
        verify(Qt.colorEqual(block(grid, "red").tone, "#ff0000"), "a calendar colour is used as given")
        verify(Qt.colorEqual(block(grid, "plain").tone, Tk.Theme.color.accent))
        verify(!findChild(grid, "calendarGridEvent_hidden"), "hidden calendars are not drawn")
        compare(block(grid, "red").Accessible.role, Accessible.Button)
        compare(grid.Accessible.role, Accessible.Table)
        const selected = spy(grid, "occurrenceSelected")
        mouseClick(block(grid, "red"))
        compare(args(selected, 0), ["red", "red"])
        grid.anchorDate = "2026-10-15"
        verify(!findChild(grid, "calendarGridNowLine").visible, "no now line outside the visible days")
    }
}
