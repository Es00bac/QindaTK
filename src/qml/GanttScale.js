// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// Ruler cells and link routes for GanttView (D-302).
// AGENT-NOTE: dates here are zone-less ISO civil dates; Date.UTC is only a
// proleptic Gregorian day counter (as in CalendarGridLayout.js). The view's
// GanttCalendar turns a civil date into its UTC instant (dayStartUtc), so a
// DST day is 23 or 25 hours wide on the calendar scale and nothing here
// reads the process time zone.

const MS_PER_DAY = 86400000
// Bound on the civil days a ruler or backdrop materializes per frame.
const MAX_DAYS = 800

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

// 1 Monday … 7 Sunday, like QDate::dayOfWeek.
function weekday(n) { const d = new Date(n * MS_PER_DAY).getUTCDay(); return d === 0 ? 7 : d }
function dateOf(n) { const d = new Date(n * MS_PER_DAY); return new Date(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()) }

// The civil days intersecting [firstIso, firstIso + count), each with its
// track x from the host mapping `xOfIso(iso)`.
function visibleDays(firstIso, count, xOfIso) {
    const first = dayNumber(firstIso)
    const out = []
    if (isNaN(first)) return out
    const n = Math.min(MAX_DAYS, Math.max(0, Math.ceil(count)) + 1)
    let x = xOfIso(isoFromDayNumber(first))
    for (let i = 0; i < n; ++i) {
        const nextX = xOfIso(isoFromDayNumber(first + i + 1))
        out.push({ day: first + i, iso: isoFromDayNumber(first + i), x: x, width: nextX - x })
        x = nextX
    }
    return out
}

// Groups consecutive days into cells that start where `startsCell(day)`.
function groupDays(days, startsCell, label) {
    const out = []
    for (const d of days) {
        if (!out.length || startsCell(d.day)) out.push({ x: d.x, width: d.width, text: label(d.day), iso: d.iso })
        else out[out.length - 1].width += d.width
    }
    return out
}

// Two ruler tiers for a pixels-per-day scale. `locale` is a Qt.locale();
// `weekStart` is 1..7. Returns { major: [...], minor: [...] }.
function rulerTiers(days, pixelsPerDay, locale, weekStart) {
    const isWeekStart = function(n) { return weekday(n) === weekStart }
    const isMonthStart = function(n) { return new Date(n * MS_PER_DAY).getUTCDate() === 1 }
    const isYearStart = function(n) { const d = new Date(n * MS_PER_DAY); return d.getUTCDate() === 1 && d.getUTCMonth() === 0 }
    const fmt = function(n, format) { return locale.toString(dateOf(n), format) }
    if (pixelsPerDay >= 16) {
        return {
            major: groupDays(days, isWeekStart, function(n) { return fmt(n, "d MMM yyyy") }),
            minor: groupDays(days, function() { return true }, function(n) {
                return pixelsPerDay >= 30 ? fmt(n, "ddd d") : fmt(n, "d") })
        }
    }
    if (pixelsPerDay >= 3) {
        return {
            major: groupDays(days, isMonthStart, function(n) { return fmt(n, "MMMM yyyy") }),
            minor: groupDays(days, isWeekStart, function(n) { return fmt(n, "d") })
        }
    }
    return {
        major: groupDays(days, isYearStart, function(n) { return fmt(n, "yyyy") }),
        minor: groupDays(days, isMonthStart, function(n) { return fmt(n, "MMM") })
    }
}

// A link route as polyline points plus the arrow direction at its end.
// from/to: { x, y } anchor points; exitRight: leaves the predecessor to the
// right (FS/FF) or left (SS/SF); enterLeft: enters the successor from its
// left (FS/SS) or right (FF/SF). gap: horizontal stub; rowHeight: detour.
function linkRoute(from, to, exitRight, enterLeft, gap, rowHeight) {
    const xa = from.x + (exitRight ? gap : -gap)
    const points = [{ x: from.x, y: from.y }, { x: xa, y: from.y }]
    const direct = enterLeft ? xa <= to.x - gap / 2 : xa >= to.x + gap / 2
    if (direct) {
        points.push({ x: xa, y: to.y })
    } else {
        const ymid = to.y + (to.y >= from.y ? -rowHeight / 2 : rowHeight / 2)
        const xb = to.x + (enterLeft ? -gap : gap)
        points.push({ x: xa, y: ymid }, { x: xb, y: ymid }, { x: xb, y: to.y })
    }
    points.push({ x: to.x, y: to.y })
    return { points: points, arrowRight: enterLeft }
}
