<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Calendar recurrence

`RecurrenceService` is the local recurrence engine for `CalendarView` and
`RecurrenceEditor`. The host stores event records and passes a stable event ID,
an ISO local wall start such as `2026-03-07T09:00:00`, a duration in seconds,
an IANA zone, a rule, and a half-open UTC search range to `expand(...)`.
It returns rows with `eventId`, stable `occurrenceId`, `localStart`, `zone`,
`startUtc`, and `endUtc`. The host assigns these rows to the editor's `preview`
or projects them into its calendar store. `error` reports invalid input or a
reached search bound.

Rules use `frequency` (`daily`, `weekly`, `monthly`, `yearly`), `interval`,
optional `weekdays` (1=Monday through 7=Sunday for weekly rules), `endMode`
(`never`, `count`, `until`), `count`, an inclusive local `until` date, and
`exceptions`. An exception may be an occurrence ID or its local start text.
`count` counts generated valid occurrences before exceptions.

## Leap days and month ends

As RFC 5545 specifies for invalid dates, a rule never moves an occurrence to
a nearby date. A monthly rule on the 29th, 30th or 31st skips the months
without that day (a rule from 31 January 2026 gives 31 January, 31 March,
31 May, 31 July, 31 August, 31 October, 31 December), and a yearly rule on
29 February repeats only in leap years (2024, 2028, 2032). Skipped months and
years do not count toward `count`: four monthly occurrences from
29 January 2026 are 29 January, 29 March, 29 April and 29 May.
`tst_recurrence_service::leapDayMonthEndAndEndDateFixtures` holds these
fixtures, and `RecurrenceEditor` tells the user ("Months without day 31 are
skipped", "Repeats only in leap years") when `startLocal` is set.

## All-day series

A date-only start (`"2026-03-07"`) is an all-day series. Its occurrences have
date-only `localStart` and `occurrenceId` values (`event/2026-03-08`), span
`durationSeconds / 86400` whole local days (at least one), and start at the
zone's start of day, so a 23-hour DST day or a zone that skips local
midnight (Santiago on 6 September 2026 starts at 01:00) never shortens or
drops the occurrence. Exceptions match the date or the midnight
`yyyy-MM-ddT00:00:00` form. Timed starts accept a `T` or space separator.

`occurrenceAt(eventId, startLocal, durationSeconds, zone, rule, local)`
returns the row the rule produces at one original local start, or an empty
map; the store and the projection use it to validate occurrence IDs.
`durationSeconds(startLocal, endLocal, zone)` gives the duration to pass to
`expand` (whole days for all-day spans).

## Time zones

Recurrence stays at the event's local wall time as UTC offsets change.
Nonexistent spring-forward wall times are skipped. An ambiguous fall-back
time resolves to the earlier offset (`PreferBefore`). Duration is elapsed
seconds, so an event crossing a transition may end at a different wall time.
Expansion is bounded to 1,000 returned rows and 100 years of searched dates;
callers should request a visible calendar range. This is a focused local rule
set, not a complete RFC 5545 implementation. `CalDavProvider` maps RRULEs
in this rule set to and from iCalendar and keeps any other RRULE verbatim
(`recurrenceRaw`); see [productivity-providers.md](productivity-providers.md).
Stored edits (rule changes, exclusions, single-occurrence overrides and
scoped moves) are `LocalProductivityStore` operations; see
[productivity-store.md](productivity-store.md#calendar-edits).
