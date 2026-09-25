// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// Civil-calendar arithmetic and event layout for CalendarGridView.
// AGENT-NOTE (D-301): every date here is a zone-less ISO civil date and every
// time is minutes after local midnight read straight from the host's
// `startLocal`/`endLocal` text. Date.UTC is used only as a proleptic
// Gregorian day counter, so neither DST nor the process time zone can move a
// result. The grid never converts zones or expands recurrence; the host
// projects rows (LocalCalendarProjectionModel) and applies proposals.
// "Absolute minutes" below are dayNumber * 1440 + minute of day.

const MINUTES_PER_DAY = 1440
const MS_PER_DAY = 86400000

function pad2(n) { return (n < 10 ? "0" : "") + n }

function dayNumber(iso) {
    const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(String(iso || ""))
    if (!m) return NaN
    return Math.round(Date.UTC(Number(m[1]), Number(m[2]) - 1, Number(m[3])) / MS_PER_DAY)
}

function isoFromDayNumber(n) {
    const d = new Date(n * MS_PER_DAY)
    return String(d.getUTCFullYear()).padStart(4, "0") + "-" + pad2(d.getUTCMonth() + 1) + "-" + pad2(d.getUTCDate())
}

function validDate(iso) {
    const n = dayNumber(iso)
    return !isNaN(n) && isoFromDayNumber(n) === String(iso).slice(0, 10)
}

function addDays(iso, n) { return isoFromDayNumber(dayNumber(iso) + n) }

// 1 = Monday … 7 = Sunday (RecurrenceService / ISO 8601 numbering).
function isoWeekday(iso) {
    const w = new Date(dayNumber(iso) * MS_PER_DAY).getUTCDay()
    return w === 0 ? 7 : w
}

function normalizeWeekStart(weekStart) {
    const w = Math.round(Number(weekStart))
    return w >= 1 && w <= 7 ? w : 1
}

function startOfWeek(iso, weekStart) {
    const back = (isoWeekday(iso) - normalizeWeekStart(weekStart) + 7) % 7
    return addDays(iso, -back)
}

function daysInMonth(year, month) { return new Date(Date.UTC(year, month, 0)).getUTCDate() }

// Calendar months; the day is clamped (31 Jan + 1 month = 28/29 Feb).
function addMonths(iso, n) {
    const y = Number(iso.slice(0, 4)), m = Number(iso.slice(5, 7)), d = Number(iso.slice(8, 10))
    const total = y * 12 + (m - 1) + n
    const ny = Math.floor(total / 12), nm = total - ny * 12 + 1
    return String(ny).padStart(4, "0") + "-" + pad2(nm) + "-" + pad2(Math.min(d, daysInMonth(ny, nm)))
}

// The visible days of a mode around `anchor`. `end` is exclusive (the day
// after the last visible day), matching the half-open projection ranges and
// the store's exclusive all-day `endLocal`.
function range(mode, anchor, weekStart, workDays) {
    if (!validDate(anchor)) return { start: "", end: "", days: [] }
    let days = []
    if (mode === "day") {
        days = [anchor]
    } else if (mode === "month") {
        const first = anchor.slice(0, 8) + "01"
        const start = startOfWeek(first, weekStart)
        for (let i = 0; i < 42; ++i) days.push(addDays(start, i))
    } else {
        const start = startOfWeek(anchor, weekStart)
        for (let i = 0; i < 7; ++i) days.push(addDays(start, i))
        if (mode === "workweek") {
            const wanted = (workDays || []).map(Number)
            const work = days.filter(function(day) { return wanted.indexOf(isoWeekday(day)) >= 0 })
            if (work.length) days = work
        }
    }
    return { start: days[0], end: addDays(days[days.length - 1], 1), days: days }
}

// The anchor one mode unit before (-1) or after (+1) `anchor`.
function step(mode, anchor, direction) {
    if (!validDate(anchor)) return anchor
    const dir = direction < 0 ? -1 : 1
    if (mode === "day") return addDays(anchor, dir)
    if (mode === "month") return addMonths(anchor, dir)
    return addDays(anchor, 7 * dir)
}

// "yyyy-MM-dd", "yyyy-MM-ddTHH:mm[:ss]" or with a space separator.
function parseLocal(text) {
    const m = /^(\d{4}-\d{2}-\d{2})(?:[T ](\d{2}):(\d{2})(?::\d{2}(?:\.\d+)?)?)?$/.exec(String(text || "").trim())
    if (!m || !validDate(m[1])) return null
    if (m[2] === undefined) return { day: dayNumber(m[1]), minute: 0, dateOnly: true }
    return { day: dayNumber(m[1]), minute: Number(m[2]) * 60 + Number(m[3]), dateOnly: false }
}

// Absolute minutes back to local text: a date for all-day values, else a
// wall time with zero seconds.
function localText(absolute, dateOnly) {
    const day = Math.floor(absolute / MINUTES_PER_DAY)
    const minute = absolute - day * MINUTES_PER_DAY
    if (dateOnly) return isoFromDayNumber(day)
    return isoFromDayNumber(day) + "T" + pad2(Math.floor(minute / 60)) + ":" + pad2(minute % 60) + ":00"
}

function isRecurring(row) { return !!(row && row.recurrenceRule && row.recurrenceRule.frequency) }
function rowKey(row) { return String(row.occurrenceId || row.eventId || row.id || "") }

// Normalised, sortable view of a host row; never mutates the row.
function item(row, index) {
    const s = parseLocal(row.startLocal)
    if (!s) return null
    const e = parseLocal(row.endLocal)
    const allDay = !!row.allDay || s.dateOnly
    const start = s.day * MINUTES_PER_DAY + (allDay ? 0 : s.minute)
    let end = e ? e.day * MINUTES_PER_DAY + (allDay ? 0 : e.minute) : start
    if (allDay) end = Math.max(Math.ceil(end / MINUTES_PER_DAY) * MINUTES_PER_DAY, start + MINUTES_PER_DAY)
    end = Math.max(end, start)
    return { row: row, index: index, key: rowKey(row), eventId: String(row.eventId || row.id || ""),
             start: start, end: end, allDay: allDay,
             // All-day rows and timed rows of a day or more sit in the all-day lane.
             lane: allDay || end - start >= MINUTES_PER_DAY,
             recurring: isRecurring(row), readOnly: !!row.readOnly }
}

function compareText(a, b) { return a < b ? -1 : a > b ? 1 : 0 }

// AGENT-GUARD (D-301, tst_calendar_grid::test_overlap_columns): the one
// deterministic order of the grid — start, then longer first, then eventId,
// then occurrenceId. Packing, lanes, month chips and keyboard traversal all
// use it, so the same projection always draws and tabs the same way.
function compareItems(a, b) {
    return (a.start - b.start) || ((b.end - b.start) - (a.end - a.start))
        || compareText(a.eventId, b.eventId) || compareText(a.key, b.key)
}

function items(rows, hiddenCalendars) {
    const hidden = hiddenCalendars || []
    const out = []
    const source = rows || []
    for (let i = 0; i < source.length; ++i) {
        const row = source[i]
        if (!row || hidden.indexOf(String(row.calendarId || "")) >= 0) continue
        const entry = item(row, i)
        if (entry) out.push(entry)
    }
    return out.sort(compareItems)
}

function dayIndexMap(days) {
    const map = ({})
    for (let i = 0; i < days.length; ++i) map[dayNumber(days[i])] = i
    return map
}

// Classic interval-graph column assignment for one day's segments: sorted
// by compareItems, each overlap cluster gets the fewest columns a greedy
// first-fit needs, and each segment then widens into free columns to its
// right. `vStart`/`vEnd` are the drawn extents (a short event is drawn at a
// minimum height and must not be hidden under its neighbour).
function packDay(segments) {
    const sorted = segments.slice().sort(function(a, b) {
        return (a.start - b.start) || ((b.end - b.start) - (a.end - a.start))
            || compareText(a.item.eventId, b.item.eventId) || compareText(a.item.key, b.item.key)
    })
    const overlaps = function(a, b) { return a.vStart < b.vEnd && b.vStart < a.vEnd }
    let cluster = [], columnEnds = [], clusterEnd = -Infinity
    const finish = function() {
        for (const s of cluster) {
            s.columns = columnEnds.length
            let span = 1
            for (let c = s.column + 1; c < columnEnds.length; ++c) {
                if (cluster.some(function(t) { return t.column === c && overlaps(s, t) })) break
                ++span
            }
            s.span = span
        }
    }
    for (const s of sorted) {
        if (s.vStart >= clusterEnd) { finish(); cluster = []; columnEnds = []; clusterEnd = -Infinity }
        let column = columnEnds.findIndex(function(end) { return end <= s.vStart })
        if (column < 0) { column = columnEnds.length; columnEnds.push(s.vEnd) } else columnEnds[column] = s.vEnd
        s.column = column
        cluster.push(s)
        clusterEnd = Math.max(clusterEnd, s.vEnd)
    }
    finish()
    return sorted
}

// Timed pieces per visible day: an event crossing midnight is cut into one
// piece per visible day. Returns a flat list ordered by day then column.
function timedLayout(entries, days, minVisualMinutes) {
    const index = dayIndexMap(days)
    const perDay = days.map(function() { return [] })
    const seen = ({})
    for (const entry of entries) {
        if (entry.lane) continue
        const firstDay = Math.floor(entry.start / MINUTES_PER_DAY)
        const lastDay = entry.end > entry.start ? Math.floor((entry.end - 1) / MINUTES_PER_DAY) : firstDay
        for (let day = firstDay; day <= lastDay; ++day) {
            const at = index[day]
            if (at === undefined) continue
            const base = day * MINUTES_PER_DAY
            const start = Math.max(entry.start, base) - base
            const end = Math.min(entry.end, base + MINUTES_PER_DAY) - base
            perDay[at].push({ item: entry, dayIndex: at, start: start, end: end,
                              vStart: start, vEnd: Math.max(end, start + minVisualMinutes),
                              continuesBefore: entry.start < base, continuesAfter: entry.end > base + MINUTES_PER_DAY,
                              first: !seen[entry.key], column: 0, columns: 1, span: 1 })
            seen[entry.key] = true
        }
    }
    let out = []
    for (const list of perDay) out = out.concat(packDay(list))
    return out
}

// All-day lane bars across the visible columns, first-fit into lanes.
function allDayLayout(entries, days) {
    const numbers = days.map(dayNumber)
    const bars = [], lanes = []
    for (const entry of entries) {
        if (!entry.lane) continue
        const firstDay = Math.floor(entry.start / MINUTES_PER_DAY)
        const lastDay = Math.floor((entry.end - 1) / MINUTES_PER_DAY)
        let col = -1, last = -1
        for (let i = 0; i < numbers.length; ++i) {
            if (numbers[i] < firstDay || numbers[i] > lastDay) continue
            if (col < 0) col = i
            last = i
        }
        if (col < 0) continue
        let lane = 0
        while (lane < lanes.length && lanes[lane].some(function(r) { return col <= r[1] && r[0] <= last })) ++lane
        if (lane === lanes.length) lanes.push([])
        lanes[lane].push([col, last])
        bars.push({ item: entry, col: col, span: last - col + 1, lane: lane, first: true,
                    continuesBefore: firstDay < numbers[col], continuesAfter: lastDay > numbers[last] })
    }
    return { bars: bars, laneCount: lanes.length }
}

// Per visible day, every entry touching it: lane entries first, then timed
// ones, each group in compareItems order.
function monthLayout(entries, days) {
    return days.map(function(day) {
        const base = dayNumber(day) * MINUTES_PER_DAY
        const touching = entries.filter(function(e) {
            return e.end > e.start ? e.start < base + MINUTES_PER_DAY && e.end > base : e.start >= base && e.start < base + MINUTES_PER_DAY
        })
        const lane = touching.filter(function(e) { return e.lane }), timed = touching.filter(function(e) { return !e.lane })
        return lane.concat(timed).map(function(e) {
            return { item: e, continuesBefore: e.start < base, continuesAfter: e.end > base + MINUTES_PER_DAY }
        })
    })
}

function snapRound(minutes, stepMinutes) { return Math.round(minutes / stepMinutes) * stepMinutes }
function snapFloor(minutes, stepMinutes) { return Math.floor(minutes / stepMinutes) * stepMinutes }
function snapCeil(minutes, stepMinutes) { return Math.ceil(minutes / stepMinutes) * stepMinutes }

// "09:05" or "9:05 AM"; `compact` drops ":00" in 12-hour labels ("9 AM"),
// `bare` drops the AM/PM marker.
function formatMinute(minute, use24Hour, amText, pmText, compact, bare) {
    const m = ((minute % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY
    const h = Math.floor(m / 60), mm = m % 60
    if (use24Hour) return pad2(h) + ":" + pad2(mm)
    const h12 = h % 12 === 0 ? 12 : h % 12
    return h12 + (compact && mm === 0 ? "" : ":" + pad2(mm)) + (bare ? "" : " " + (h < 12 ? amText : pmText))
}

// "10:00 – 11:30", or in 12-hour time "10 – 11:30 AM" / "11:30 AM – 12:30 PM"
// (the marker is written once when both ends share it).
function formatRange(startMinute, endMinute, use24Hour, amText, pmText) {
    const half = function(minute) { return Math.floor((((minute % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY) / 720) }
    const shared = !use24Hour && half(startMinute) === half(endMinute)
    return formatMinute(startMinute, use24Hour, amText, pmText, true, shared) + " \u2013 "
        + formatMinute(endMinute, use24Hour, amText, pmText, true, false)
}
