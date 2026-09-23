<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Accessibility, locale and gesture services

`AccessibilityAdapter` is a `QQuickItem` host for virtual semantic children of
custom grids, canvases and timelines. Put it in the relevant item tree and
set `Accessible.role: Accessible.Pane` so Qt Quick exposes the host in the
native window accessibility tree. Supply semantic nodes through `setNodes`
with stable `id`, role, name, description, local bounds, value/range, selected,
focused, enabled and action-to-command mappings. Nodes describe model items,
including offscreen items, without requiring a visible delegate per item.
Replacing the node list preserves accessible identity for retained IDs and
retires removed children. The adapter rejects malformed or oversized node
snapshots atomically. `SelectionModel` supplies current/selected state when
attached, and accessible actions invoke the same `CommandRegistry` paths as
the application's pointer and keyboard controls. A command handler owns any
`EditTransaction`; the adapter never edits a document by itself. Use
`announce(message)` for bounded queued live announcements.

`LocaleService` is an instance-owned presentation policy that can be shared
per app, window or document. Its locale, time zone, numbering system, calendar
and chrome direction choices emit changes without rewriting canonical model
values. `formatNumber`/`parseNumber`, `formatDateTime`/`parseDateTime`, and
`formatUnit` operate at the UI boundary. Parse methods return `ok`, `value`
or `error`; invalid choices leave prior settings unchanged. `chromeDirection`
controls application chrome policy, while `contentDirection(text)` detects a
text run's first strong direction independently. Apps apply the chrome policy
to their QML layout mirroring and keep document bidi content separate. Only
locales, calendars and time zones available through the selected Qt runtime
are accepted; translation catalogs are application content.

Date pickers use `classifyLocalDateTime(text,format)` before committing a wall
time. It returns `valid` with one canonical ISO UTC instant, `ambiguous` with
two candidate instants, `nonexistent` for a daylight-saving gap, or `invalid`.
The picker must ask the user to choose a candidate in an overlap and cannot
silently normalize a gap. `formatDate`/`parseDate` carry all-day values as
Gregorian ISO `YYYY-MM-DD` strings without applying a time zone.
`monthGrid(year,month,minIso,maxIso)` takes year/month in the configured
calendar and returns 42 date cells with stable Gregorian ISO IDs, localized
labels, bounds and weekday order. `calendarFields(dateIso)` converts an ISO
date to that calendar's year/month/day; `shiftMonth` navigates its native
month sequence. A UI should use these methods rather than JavaScript's
Gregorian month math when a non-Gregorian calendar is active.

`GestureRouter` accepts one semantic pointer gesture per view while allowing
independent views to work concurrently. A caller passes stable view, pointer
and target IDs, device, intent, owner and optional configured but inactive
`EditTransaction`. The router starts an edit only after the gesture wins its
claim, commits it once on completion, and cancels it on Escape/lost grab,
owner destruction, target removal or view recycling. `activeGestures` exposes
the current tool, device, intent, modifiers and buttons. Synthesized duplicate
pointer input is rejected; temporary navigation turns an edit claim into a
navigation gesture without starting an edit. `wheelIntent` resolves temporary
navigation and Ctrl zoom versus ordinary scroll. Qt pointer handlers,
including `StylusHandler`, still receive raw events and own platform grabs;
they call the router at semantic gesture boundaries.

The in-process Qt accessibility interface and window-tree discoverability are
tested with a real `QQuickWindow`. An external Linux AT-SPI client also finds
grid cells, a canvas shape and timeline clips, including an offscreen clip;
it invokes a timeline action that commits through `EditTransaction` and records
undo. Reproduce that check with
[`tests/accessibility/README.md`](../tests/accessibility/README.md). The
[`platform-services.qml`](../examples/platform-services.qml) scene checks RTL
chrome, mixed bidi content and translated-length wrapping at compact and
comfortable density; its visible draft is presentation data, while the C++
locale test verifies a live pending edit. Existing command/focus tests cover
IME preedit. Human screen-reader passes with Orca, Windows NVDA and macOS
VoiceOver remain separate platform qualification work; the AT-SPI client is
not a claim of those end-user passes.
