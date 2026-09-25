// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import "CalendarGridLayout.js" as CalendarLayout

// Day / work week / week / month time grid over host-projected occurrences.
// AGENT-CONTRACT (LocalCalendarProjectionModel, LocalProductivityStore
// .shiftEventLocal, CalendarView): `events` are projection rows
// {eventId, occurrenceId, title, startLocal, endLocal, zone, allDay,
// calendarId, recurrenceRule, overridden, location, readOnly}. The grid
// never expands recurrence, never converts zones and never mutates `events`
// (D-301): a drag or key move emits eventChangeRequested with
// {kind: "shift-local" | "resize-local", minutes, zone, scope, occurrenceId}
// (scope from `moveScope` for a recurring row, "series" otherwise; all-day
// and month moves in whole days) and waits for the host to re-project.
// `now` is supplied by the host; the grid never reads the clock.
T.Control {
    id: control
    objectName: "calendarGrid"

    // Host snapshot ---------------------------------------------------------
    property var events: []
    property var calendars: []               // [{id, name, color, visible}]
    property string mode: "week"             // day | workweek | week | month
    property string anchorDate: ""           // ISO date inside the visible range
    property int weekStart: control.locale.firstDayOfWeek === 0 ? 7 : control.locale.firstDayOfWeek // 1 = Mon … 7 = Sun
    property var workDays: [1, 2, 3, 4, 5]
    property int dayStartHour: 8
    property int dayEndHour: 18
    // AGENT-NOTE: two list rows per hour, so a half hour is one row and the
    // grid follows density and text zoom without a calendar-only metric.
    property real hourHeight: Tk.Theme.size.row * 2
    property string now: ""                  // host local "yyyy-MM-ddTHH:mm[:ss]"
    property string selectedOccurrenceId: ""
    property string zone: ""                 // label only
    property bool offline: false
    property bool readOnly: false
    property string errorText: ""
    property string moveScope: "occurrence"  // for recurring rows: occurrence | following | series
    property int snapMinutes: 15
    property int createMinutes: 30
    property bool use24Hour: !/a/i.test(control.locale.timeFormat(Locale.ShortFormat))
    property string tooltip: qsTr("Calendar")
    property string requestState: "idle"
    property string requestMessage: ""
    property string lastRequestId: ""
    property int _requestSerial: 0

    // Derived, read-only ------------------------------------------------------
    readonly property string _anchor: CalendarLayout.validDate(anchorDate) ? anchorDate
        : CalendarLayout.validDate(now.slice(0, 10)) ? now.slice(0, 10) : ""
    readonly property var visibleRange: CalendarLayout.range(mode, _anchor, weekStart, workDays)
    readonly property var days: visibleRange.days
    readonly property string rangeStart: visibleRange.start
    readonly property string rangeEnd: visibleRange.end   // exclusive
    readonly property string title: _title(visibleRange, _anchor, mode)
    readonly property var hiddenCalendarIds: (calendars || []).filter(function(c) { return c && c.visible === false })
                                                              .map(function(c) { return String(c.id) })
    readonly property var entries: CalendarLayout.items(events, hiddenCalendarIds)
    readonly property var nowPoint: CalendarLayout.parseLocal(now)
    readonly property string todayDate: nowPoint ? CalendarLayout.isoFromDayNumber(nowPoint.day) : ""
    // Keyboard traversal order: every entry touching a visible day.
    readonly property var orderedKeys: {
        const shown = ({})
        days.forEach(function(day) { shown[CalendarLayout.dayNumber(day)] = true })
        const keys = []
        entries.forEach(function(e) {
            const last = e.end > e.start ? Math.floor((e.end - 1) / 1440) : Math.floor(e.start / 1440)
            for (let d = Math.floor(e.start / 1440); d <= last; ++d)
                if (shown[d]) { if (keys.indexOf(e.key) < 0) keys.push(e.key); break }
        })
        return keys
    }
    readonly property string tabStopKey: orderedKeys.indexOf(selectedOccurrenceId) >= 0 ? selectedOccurrenceId
                                       : orderedKeys.length ? orderedKeys[0] : ""
    // {kind: move|resize|create, area: timed|allday|month, entry, start, end,
    //  delta, pending, requestId}; start/end are absolute civil minutes.
    property var dragState: null
    // Platform drag threshold; read through `var` because qmllint types
    // Qt.styleHints as QObject.
    readonly property var _styleHints: Qt.styleHints
    readonly property real dragDistance: _styleHints ? _styleHints.startDragDistance : Tk.Theme.space.md
    property bool _dragCancelled: false
    property var _eventItems: ({})
    property string _focusKey: ""
    property string _focusEventId: ""
    property string _lastRange: ""

    signal rangeChanged(string rangeStart, string rangeEnd)
    signal occurrenceSelected(string eventId, string occurrenceId)
    signal openRequested(string eventId, string occurrenceId)
    signal createRequested(string startLocal, string endLocal, bool allDay)
    signal eventChangeRequested(string requestId, string eventId, var proposedChange)
    signal deleteRequested(string eventId, string occurrenceId)
    signal moreRequested(string dateIso)

    implicitWidth: Tk.Theme.size.dialogWidth * 2
    implicitHeight: Tk.Theme.size.dialogWidth * 1.5
    padding: 0
    focusPolicy: Qt.ClickFocus
    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    Accessible.description: title + (zone ? " · " + zone : "")

    // Labels ------------------------------------------------------------------
    function _date(iso) { return { y: Number(iso.slice(0, 4)), m: Number(iso.slice(5, 7)), d: Number(iso.slice(8, 10)) } }
    function weekdayName(iso, format) { return control.locale.dayName(CalendarLayout.isoWeekday(iso) % 7, format) }
    function monthName(month, format) { return control.locale.monthName(month - 1, format) }
    function dayLabel(iso) { const p = _date(iso); return weekdayName(iso, Locale.LongFormat) + " " + p.d + " " + monthName(p.m, Locale.LongFormat) }
    function rangeText(startMinute, endMinute) { return CalendarLayout.formatRange(startMinute, endMinute, use24Hour, control.locale.amText, control.locale.pmText) }
    function timeText(minute, compact) { return CalendarLayout.formatMinute(minute, use24Hour, control.locale.amText, control.locale.pmText, !!compact) }
    function _title(range, anchor, currentMode) {
        if (!range.days.length) return ""
        const a = _date(anchor)
        if (currentMode === "month") return control.locale.standaloneMonthName(a.m - 1, Locale.LongFormat) + " " + a.y
        if (currentMode === "day") return dayLabel(anchor) + " " + a.y
        const f = _date(range.days[0]), l = _date(range.days[range.days.length - 1])
        const dash = " – "
        if (f.y !== l.y) return f.d + " " + monthName(f.m, Locale.ShortFormat) + " " + f.y + dash + l.d + " " + monthName(l.m, Locale.ShortFormat) + " " + l.y
        if (f.m !== l.m) return f.d + " " + monthName(f.m, Locale.ShortFormat) + dash + l.d + " " + monthName(l.m, Locale.ShortFormat) + " " + l.y
        return f.d + dash + l.d + " " + monthName(l.m, Locale.ShortFormat) + " " + l.y
    }
    function spanText(entry) {
        const startDay = Math.floor(entry.start / 1440)
        if (entry.allDay) {
            const lastDay = Math.floor((entry.end - 1) / 1440)
            return lastDay > startDay ? qsTr("%1 to %2, all day").arg(dayLabel(CalendarLayout.isoFromDayNumber(startDay))).arg(dayLabel(CalendarLayout.isoFromDayNumber(lastDay)))
                                      : qsTr("%1, all day").arg(dayLabel(CalendarLayout.isoFromDayNumber(startDay)))
        }
        const endDay = Math.floor(entry.end / 1440)
        const from = timeText(entry.start % 1440), to = timeText(entry.end % 1440)
        if (endDay === startDay || (endDay === startDay + 1 && entry.end % 1440 === 0))
            return qsTr("%1, %2 to %3").arg(dayLabel(CalendarLayout.isoFromDayNumber(startDay))).arg(from).arg(to)
        return qsTr("%1 %2 to %3 %4").arg(dayLabel(CalendarLayout.isoFromDayNumber(startDay))).arg(from)
                                     .arg(dayLabel(CalendarLayout.isoFromDayNumber(endDay))).arg(to)
    }
    function accessibleName(entry) { return String(entry.row.title || qsTr("Untitled event")) + ", " + spanText(entry) }
    function accessibleDescription(entry) {
        const row = entry.row, parts = []
        const calendar = calendarFor(row)
        if (calendar && calendar.name) parts.push(String(calendar.name))
        if (row.location) parts.push(String(row.location))
        if (entry.recurring) parts.push(qsTr("repeats"))
        if (row.overridden) parts.push(qsTr("moved"))
        if (!canEdit(row)) parts.push(qsTr("read-only"))
        return parts.join(" · ")
    }

    // Colours: a calendar's colour is user data and is used as given; without
    // one, calendars take the theme's accent and status roles in order.
    function calendarFor(row) {
        const id = String(row.calendarId || "")
        return (calendars || []).find(function(c) { return c && String(c.id) === id }) || null
    }
    function colorFor(row) {
        const calendar = calendarFor(row)
        if (calendar && calendar.color) return calendar.color
        if (row.color) return row.color
        const id = String(row.calendarId || "")
        if (!id) return Tk.Theme.color.accent
        const palette = [Tk.Theme.color.accent, Tk.Theme.color.success, Tk.Theme.color.warning,
                         Tk.Theme.color.danger, Tk.Theme.color.info]
        let index = (calendars || []).findIndex(function(c) { return c && String(c.id) === id })
        if (index < 0) { index = 0; for (let i = 0; i < id.length; ++i) index = (index * 31 + id.charCodeAt(i)) % 9973 }
        return palette[index % palette.length]
    }

    // Navigation --------------------------------------------------------------
    function setMode(newMode) {
        if (["day", "workweek", "week", "month"].indexOf(newMode) < 0) return false
        mode = newMode
        return true
    }
    function navigate(direction) {
        if (!_anchor) return false
        anchorDate = CalendarLayout.step(mode, _anchor, direction < 0 ? -1 : 1)
        return true
    }
    function goToday(todayIso) {
        const target = CalendarLayout.validDate(String(todayIso || "")) ? String(todayIso) : todayDate
        if (!target) return false
        anchorDate = target
        return true
    }
    function scrollToHour(hour) { return timeLoader.item ? timeLoader.item.scrollToHour(hour) : false }
    // Reads visibleRange itself: rangeStart/rangeEnd may not have re-evaluated
    // yet when onVisibleRangeChanged runs.
    function _emitRange() {
        const range = visibleRange
        const key = range.start + "|" + range.end
        if (!range.start || key === _lastRange) return
        _lastRange = key
        rangeChanged(range.start, range.end)
    }
    onVisibleRangeChanged: _emitRange()
    Component.onCompleted: _emitRange()

    // Requests ----------------------------------------------------------------
    function rowFor(key) { const e = entries.find(function(entry) { return entry.key === key }); return e ? e.row : null }
    function canEdit(row) { return !readOnly && !offline && !!row && !row.readOnly }
    function select(row) {
        if (!row) return false
        selectedOccurrenceId = CalendarLayout.rowKey(row)
        occurrenceSelected(String(row.eventId || ""), selectedOccurrenceId)
        return true
    }
    function open(row) { if (row) openRequested(String(row.eventId || ""), CalendarLayout.rowKey(row)) }
    function requestDelete(row) {
        if (!canEdit(row)) return false
        deleteRequested(String(row.eventId || ""), CalendarLayout.rowKey(row))
        return true
    }
    function proposeChange(eventId, proposed) {
        if (!eventId || offline || readOnly) return ""
        const id = "calendar-grid-edit-" + Date.now() + "-" + (++_requestSerial)
        lastRequestId = id; requestState = "pending"; requestMessage = ""
        eventChangeRequested(id, eventId, proposed)
        return id
    }
    function _proposal(kind, row, minutes) {
        const recurring = CalendarLayout.isRecurring(row)
        return { kind: kind, minutes: minutes, zone: String(row.zone || zone),
                 scope: recurring ? moveScope : "series", occurrenceId: recurring ? String(row.occurrenceId || "") : "" }
    }
    // Returns the request ID, or "" when the move is refused locally.
    function proposeMove(row, minutes) {
        const entry = row ? CalendarLayout.item(row, 0) : null
        if (!entry || !canEdit(row) || !minutes || (entry.allDay && minutes % 1440 !== 0)) return ""
        return proposeChange(String(row.eventId || ""), _proposal("shift-local", row, minutes))
    }
    // Moves the end by `minutes`; the event keeps at least `snapMinutes`.
    function proposeResize(row, minutes) {
        const entry = row ? CalendarLayout.item(row, 0) : null
        if (!entry || entry.lane || !canEdit(row) || !minutes || entry.end - entry.start + minutes < snapMinutes) return ""
        return proposeChange(String(row.eventId || ""), _proposal("resize-local", row, minutes))
    }
    function acknowledge(requestId, state, message) {
        if (dragState && dragState.pending && dragState.requestId === requestId) dragState = null
        if (requestId !== lastRequestId || requestState !== "pending") return false
        requestState = state; requestMessage = message || ""
        return true
    }
    function createAt(dayIndex, minute) {
        if (readOnly || offline || dayIndex < 0 || dayIndex >= days.length) return false
        const base = CalendarLayout.dayNumber(days[dayIndex]) * 1440
        const start = base + Math.min(CalendarLayout.snapFloor(minute, snapMinutes), 1440 - snapMinutes)
        const end = Math.min(start + Math.max(snapMinutes, createMinutes), base + 1440)
        createRequested(CalendarLayout.localText(start, false), CalendarLayout.localText(end, false), false)
        return true
    }
    function createDay(dayIndex) {
        if (readOnly || offline || dayIndex < 0 || dayIndex >= days.length) return false
        createRequested(days[dayIndex], CalendarLayout.addDays(days[dayIndex], 1), true)
        return true
    }

    // Pointer drags (called by the parts with {dayIndex, minute} slots) --------
    function beginDrag(kind, area, entry, slot) {
        if (offline || readOnly || (entry && !canEdit(entry.row)) || !days.length) return false
        _dragCancelled = false
        dragState = { kind: kind, area: area, entry: entry, originDay: CalendarLayout.dayNumber(days[slot.dayIndex]),
                      originMinute: slot.minute, start: entry ? entry.start : 0, end: entry ? entry.end : 0,
                      delta: 0, pending: false, requestId: "" }
        updateDrag(slot)
        return true
    }
    function updateDrag(slot) {
        const d = dragState
        if (!d || d.pending || _dragCancelled) return
        const day = CalendarLayout.dayNumber(days[Math.max(0, Math.min(days.length - 1, slot.dayIndex))])
        const snap = snapMinutes, timed = d.area === "timed"
        let start = d.start, end = d.end, delta = 0
        if (d.kind === "move") {
            delta = (day - d.originDay) * 1440 + (timed ? CalendarLayout.snapRound(slot.minute - d.originMinute, snap) : 0)
            start = d.entry.start + delta; end = d.entry.end + delta
        } else if (d.kind === "resize") {
            delta = (day - d.originDay) * 1440 + CalendarLayout.snapRound(slot.minute - d.originMinute, snap)
            delta = Math.max(delta, snap - (d.entry.end - d.entry.start))
            start = d.entry.start; end = d.entry.end + delta
        } else if (timed) {
            const base = d.originDay * 1440
            const lo = Math.min(d.originMinute, slot.minute), hi = Math.max(d.originMinute, slot.minute)
            start = base + Math.min(CalendarLayout.snapFloor(lo, snap), 1440 - snap)
            end = Math.min(base + 1440, Math.max(base + CalendarLayout.snapCeil(hi, snap), start + snap))
        } else {
            start = Math.min(day, d.originDay) * 1440
            end = (Math.max(day, d.originDay) + 1) * 1440
        }
        dragState = Object.assign({}, d, { start: start, end: end, delta: delta })
    }
    function endDrag() {
        const d = dragState
        if (!d || d.pending) return ""
        if (d.kind === "create") {
            dragState = null
            const allDay = d.area !== "timed"
            createRequested(CalendarLayout.localText(d.start, allDay), CalendarLayout.localText(d.end, allDay), allDay)
            return "create"
        }
        const id = !d.delta ? "" : d.kind === "move" ? proposeMove(d.entry.row, d.delta) : proposeResize(d.entry.row, d.delta)
        dragState = id ? Object.assign({}, d, { pending: true, requestId: id }) : null
        return id
    }
    function cancelDrag() {
        if (!dragState || dragState.pending) return false
        dragState = null
        _dragCancelled = true
        return true
    }

    // Keyboard ------------------------------------------------------------------
    function focusEvent(key) {
        const item = _eventItems[key]
        if (!item) return false
        item.forceActiveFocus(Qt.TabFocusReason)
        return true
    }
    function focusAdjacent(key, direction) {
        for (let i = orderedKeys.indexOf(key) + direction; i >= 0 && i < orderedKeys.length; i += direction) {
            if (focusEvent(orderedKeys[i])) { select(rowFor(orderedKeys[i])); return true }
        }
        return false
    }
    function handleEventKey(entry, event) {
        const row = entry.row
        const moveModifier = (event.modifiers & (Qt.AltModifier | Qt.ControlModifier)) !== 0
        const vertical = event.key === Qt.Key_Up ? -1 : event.key === Qt.Key_Down ? 1 : 0
        const horizontal = event.key === Qt.Key_Left ? -1 : event.key === Qt.Key_Right ? 1 : 0
        if (event.key === Qt.Key_Escape) return cancelDrag()
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { open(row); return true }
        if (event.key === Qt.Key_Space) return select(row)
        if (event.key === Qt.Key_Delete) { requestDelete(row); return true }
        if (event.key === Qt.Key_Tab) return focusAdjacent(entry.key, 1)
        if (event.key === Qt.Key_Backtab) return focusAdjacent(entry.key, -1)
        if (vertical && moveModifier && (event.modifiers & Qt.ShiftModifier)) { proposeResize(row, vertical * snapMinutes); return true }
        if (vertical && moveModifier) { proposeMove(row, vertical * (entry.allDay ? 1440 : snapMinutes)); return true }
        if (horizontal && moveModifier) { proposeMove(row, horizontal * 1440); return true }
        if (vertical || horizontal) { focusAdjacent(entry.key, vertical || horizontal); return true }
        return false
    }
    function _registerEvent(key, item) { _eventItems[key] = item; if (key === _focusKey) Qt.callLater(_restoreFocus) }
    function _unregisterEvent(key, item) { if (_eventItems[key] === item) delete _eventItems[key] }
    function _inside(item) { for (let p = item; p; p = p.parent) if (p === control) return true; return false }
    function _noteFocus(entry) { _focusKey = entry.key; _focusEventId = entry.eventId }
    // A re-projection recreates the event items; keyboard focus follows the
    // occurrence (or, when its ID changed with the move, its event).
    function _restoreFocus() {
        const w = control.Window.window
        const current = w ? w.activeFocusItem : null
        if (!_focusKey || (current && current.calendarGridEvent === true)) return
        // A destroyed focus item leaves focus on a scope inside the grid
        // (the Loader) or nowhere; anything outside means the user moved on.
        if (current && current !== w.contentItem && !_inside(current)) { _focusKey = ""; return }
        let item = _eventItems[_focusKey]
        if (!item) {
            for (const key in _eventItems)
                if (_eventItems[key].entry.eventId === _focusEventId) { item = _eventItems[key]; break }
        }
        if (item) { item.forceActiveFocus(Qt.OtherFocusReason); selectedOccurrenceId = item.entry.key }
    }
    onEventsChanged: {
        if (dragState && dragState.pending) dragState = null
        if (_focusKey) Qt.callLater(_restoreFocus)
    }
    Keys.onEscapePressed: function(event) { event.accepted = control.cancelDrag() }
    onActiveFocusChanged: if (activeFocus) _focusKey = "" // a click on an empty slot

    background: Rectangle { color: Tk.Theme.color.panel }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        Tk.Notice {
            objectName: "calendarGridNotice"
            visible: control.offline || control.errorText.length > 0
                     || ["rejected", "conflict", "offline"].indexOf(control.requestState) >= 0
            variant: control.offline || control.requestState === "offline" ? "warning" : "danger"
            title: control.offline ? qsTr("Offline calendar") : control.errorText.length > 0 ? qsTr("Calendar error")
                 : control.requestState === "conflict" ? qsTr("Calendar changed elsewhere") : qsTr("Change not applied")
            text: control.requestMessage || control.errorText || qsTr("Showing the host's cached calendar; changes are paused.")
        }
        Loader {
            id: timeLoader
            visible: active
            active: control.mode !== "month"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            sourceComponent: Tk.CalendarGridTime { grid: control }
        }
        Loader {
            visible: active
            active: control.mode === "month"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            sourceComponent: Tk.CalendarGridMonth { grid: control }
        }
    }
}
