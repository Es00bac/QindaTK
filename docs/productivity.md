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
Monthly rules skip nonexistent days rather than moving the 31st to a shorter
month. `count` counts generated valid occurrences before exceptions.

Recurrence stays at the event's local wall time as UTC offsets change.
Nonexistent spring-forward wall times are skipped. An ambiguous fall-back
time resolves to the earlier offset (`PreferBefore`). Duration is elapsed
seconds, so an event crossing a transition may end at a different wall time.
Expansion is bounded to 1,000 returned rows and 100 years of searched dates;
callers should request a visible calendar range. This is a focused local rule
set, not a complete RFC 5545 implementation. Remote calendar sync,
provider-specific recurrence semantics and stored event transactions remain
host or local-store responsibilities.
