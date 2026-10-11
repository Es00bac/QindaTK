<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Mail protocol extensions (M98)

These APIs build on [productivity-providers.md](productivity-providers.md).
Provider operations remain asynchronous, correlated by requestId, with secrets
held only in memory and removed from errors/transcripts. TLS verification and
STARTTLS requirements are unchanged. Tests use local fake servers only.

## IMAP flags, categories, headers and copies

`ImapMailProvider.updateFlags(requestId, messageId, add, remove)` adds/removes
RFC 3501 flags/keywords and emits `flagsFinished`. `setCategories(requestId,
messageId, names)` uses the existing QindaMail keyword mapping. `keywordCategories`
(default false) adds categories to synchronized records; keywords and answered
are always recorded. PERMANENTFLAGS is checked before changing flags; read-only
mailboxes and temporary-only edits are rejected without STORE. Clearing a
non-permanent flag is likewise rejected.

`fetchHeaders(requestId, messageId, fields)` emits `headersFinished`, with raw
header byte blocks in `result.headers` keyed by UID. Empty fields requests the
whole header; named fields use BODY.PEEK[HEADER.FIELDS (...)], preserving unread
state. Names are validated as atoms before connecting. `copyMessage(requestId,
messageId, targetMailbox)` emits `actionFinished` with action copy and keeps the
source record. Moves still prefer UID MOVE; the fallback uses COPY, Deleted and
UID EXPUNGE when UIDPLUS is supported. The legacy fallback without UIDPLUS uses
EXPUNGE and can expunge other messages already marked Deleted.

### Categories as keywords

ImapKeywords is the public pure codec. Spaces map to underscores; literal
underscores, ampersands, special ASCII, leading dollars and non-ASCII characters
use modified UTF-7 runs. Registered dollar-prefixed keywords and common junk
flags are not inferred as categories. The server's raw LIST mailbox spelling is
cached so noncanonical modified-UTF-7 ASCII runs remain addressable.

## SMTP prepared bytes

`sendPrepared(requestId, bytes, envelopeFrom, recipients)` sends reviewed RFC
5322 bytes without rebuilding them. Require a valid sender and each recipient,
CRLF lines, a header/body separator, trailing CRLF, no NULs and at most 32 MiB.
Only SMTP dot-stuffing changes the transport; the server receives the original
bytes. The explicit envelope supports recipients absent from visible headers.
The caller owns visible headers, including whether Bcc was removed.

Ordinary drafts accept replyTo and importance (high/normal/low). These produce
Reply-To, Importance and X-Priority. Parsed records expose the same metadata.

## Calendar and iMIP

VEVENT/VTODO CATEGORIES are mapped as categories, including escaped commas and
multiple category lines. Existing recurrence overrides and EXDATE remain handled
by the shared ICal codec and LocalCalendarProjectionModel.

`Tk.CalendarMail` provides pure conversion:

- `compose(method, calendarBytes, draft)` returns message bytes and calendar with
  matching METHOD, for REQUEST, REPLY or CANCEL. Draft fields: from, to, cc,
  replyTo, subject, body, bodyHtml. Output is multipart/alternative, text/calendar
  last, following [RFC 6047](https://www.rfc-editor.org/rfc/rfc6047.html).
- `parse(messageBytes)` exposes method, uid, event, organizer, attendees, sequence
  and raw calendar. `authenticated` is false: parsing does not authenticate a
  claimed organizer or attendee. The host decides whether to accept a change.
- `reply(calendarBytes, address, partstat)` prepares the REPLY calendar for an
  existing attendee; status is ACCEPTED, TENTATIVE or DECLINED. It retains UID,
  sequence and timing and returns the organizer as to. Compose and send the
  result explicitly. One VEVENT per scheduling conversion, maximum 256 KiB.

## HTML bodies and embedded images

IMAP and POP3 keepHtml defaults false. Opting in stores up to 2 MiB characters
as bodyHtml with bodyTrustMode untrusted-html; consumers must sanitize before
rendering. Attachments have contentId, disposition and inline (referenced by a
cid URL). Both providers use MailRecord, so the record contract is identical.
Calendar MIME parts add calendarMethod, calendarText (bounded) and attachment ID.

## Compatibility and qualification

M98 adds C++ fields and exported APIs; it requires the coordinated r18 ABI
subslot/reverse-dependency rebuild. Existing QML defaults remain compatible.
QindaMail can replace its keyword/header/COPY wire workarounds with provider
methods and feed its prepared message bytes directly to SMTP. Safe rendering
and explicit send/accept decisions remain host responsibilities.

Focused gates: tst_mail_extensions, tst_imap_mail_provider, tst_imap_folders,
tst_smtp_mail_provider, tst_pop3_mail_provider, tst_provider_tls,
tst_caldav_provider, tst_ics_calendar_provider and tst_local_calendar_edits.
Full-suite results are recorded in the milestone qualification report.

## ManageSieve (D-343)

`Tk.ManageSieveClient` exposes host, port (4190), security (starttls), userName,
timeoutMs, allowInsecureAuth, busy and hasSecret; setSecret/clearSecret keep the
password write-only. `request(id, command, name, scriptBytes, newName)` accepts
capabilities, list, get, put, check, activate, delete and rename. Each request
connects, reads capabilities, requires STARTTLS when configured, refreshes
capabilities, authenticates with PLAIN, executes one command and closes.
An empty name for activate disables the active script. Upload never activates.

`finished(id, result)` provides ok, state, error, responseCode, capabilities and
redacted transcript; list returns scripts (`{name,active}`), get returns script
bytes. Script literals are bounded to 4 MiB; server responses to 16 MiB and 10,000
rows. Quoted script names equal to OK/NO/BYE remain names, not status responses.
The implementation follows [RFC 5804](https://www.rfc-editor.org/rfc/rfc5804.html).
`tst_manage_sieve` covers every command, quoted names, literal content, response
codes, no-auth-on-missing-TLS and a real STARTTLS handshake with a local fake server.

## Native desktop notifications (D-348)

`Tk.DesktopNotifications.available` tracks a session notification server.
`post(requestId, summary, body, icon, actions, hints, timeoutMs, replacesId)` emits
delivered(requestId,nativeId) or failed(requestId,error). Actions are alternating
IDs and labels. Native actionInvoked and notificationClosed are forwarded;
close(nativeId) dismisses a notification. There is no automatic notification or
service startup. No server or no QtDBus means available=false and post returns
false. Tests use a private bus, so qualification cannot notify the user's desktop.
