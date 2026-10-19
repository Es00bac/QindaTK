<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Mail, calendar and task providers

Five providers connect the local productivity store
([productivity-store.md](productivity-store.md)) to standard servers. The
views (`MailView`, `RichMailComposer`, `CalendarView`, `RecurrenceEditor`,
`TaskBoard`) only emit correlated requests; the host calls a provider and
passes the result's `state` to the view's `acknowledge(requestId, state,
message)` (see [Wiring the views](#wiring-the-views)).

| Type | Protocol | Local side |
| --- | --- | --- |
| `Tk.SmtpMailProvider` | SMTP submission (RFC 5321: EHLO, STARTTLS, AUTH PLAIN/LOGIN/XOAUTH2, MAIL/RCPT/DATA) | draft records + `LocalAttachmentStore` bytes |
| `Tk.ImapMailProvider` | IMAP4rev1 (RFC 3501; SASL-IR, UIDPLUS, MOVE, RFC 6154 SPECIAL-USE when offered; modified UTF-7 names; LIST/STATUS/APPEND) | `messages` records + attachment bytes on demand |
| `Tk.Pop3MailProvider` | POP3 (RFC 1939: USER/PASS, UIDL, LIST, RETR, TOP, DELE, RSET, QUIT; RFC 2449 CAPA; RFC 2595 STLS; RFC 5034 AUTH PLAIN/XOAUTH2) | `messages` records that *are* the mailbox (D-298) + attachment bytes |
| `Tk.CalDavProvider` | CalDAV over WebDAV (RFC 4791/4918: PROPFIND Depth 1, GET, PUT/DELETE with If-Match), discovery (RFC 6764/5397) and iCalendar (RFC 5545 subset) | `events` (VEVENT) and `tasks` (VTODO) |
| `Tk.IcsCalendarProvider` | iCalendar feeds (conditional HTTP GET of `https:`/`http:`/`webcal:`) and `.ics` files, over the CalDAV codec | read-only feed replicas and imported local `events` (D-300) |

Nothing here talks to a hosted service's private API. Every provider runs
one protocol exchange per request on a worker thread with a blocking socket
and delivers its result map on the GUI thread (decision D-186).

## Common result contract

Each operation takes a host `requestId` and later emits one signal with a
result map: `requestId`, `ok`, `state`, `error`, `retryable` and a
`transcript` (protocol lines with credentials replaced by `<redacted>`,
message bodies shown as byte counts).

| state | Meaning | Host action |
| --- | --- | --- |
| `accepted` | The server confirmed it and the local store was saved | acknowledge; for SMTP move the draft to sent if wanted |
| `offline` | The server could not be reached or the connection dropped (`retryable` true) | keep local data; retry later |
| `rejected` | Validation, authentication, TLS policy or a server refusal | show `error`; nothing was changed remotely |
| `conflict` | The remote copy changed since it was read, or the local store file has a newer writer | reload/resolve, then retry |

Invalid input is rejected before connecting and still arrives through the
signal on the next event-loop turn. Providers never modify a draft; a failed
send leaves the draft record and its attachment bytes untouched.

## Wiring the views

`examples/local-productivity/ProductivityHost.qml` is the reference host,
and `tst_productivity_seam` runs it against local SMTP, IMAP and CalDAV test
servers:

| View request | Host call | Acknowledged with |
| --- | --- | --- |
| `MailView.refreshRequested` | `ImapMailProvider.sync` | `syncFinished` state |
| `MailView.actionRequested` (`read`, `unread`, `flag`, `unflag`, `archive`, `delete`) | `applyAction` for account messages, a store edit for local ones | `actionFinished` state |
| `MailView.attachmentRequested` | `fetchAttachment` | `acknowledgeAttachment(requestId, state, error)` |
| `MailView.replyRequested` | a new draft with `inReplyTo`/`references` | none (host action) |
| `RichMailComposer.sendRequested` | save the draft, then `SmtpMailProvider.send` | `sendFinished` state |
| `RichMailComposer.attachmentAddRequested` / `attachmentRemoveRequested` | `LocalAttachmentStore.importFile` / `remove` | `acknowledgeAttachment(requestId, state, message, metadata)` |
| `TaskBoard.moveRequested`, `CalendarView.eventChangeRequested`, `RecurrenceEditor` requests | the store edit, then `CalDavProvider.sync` for account records | `syncFinished` state (`conflict` included) |
| `TaskBoard.conflictResolutionRequested` | `resolveConflict(requestId, "tasks", id, choice)` | `changeFinished` state |

Account records are written through before the view is acknowledged, so an
accepted move is on the server; a `conflict` result leaves the local edit in
place and lists the server version in `CalDavProvider.conflicts`, which the
board shows with "Keep mine" and "Use server". Local-only records are
acknowledged after the local save.

The folder pane, moves, the Sent copy and calendar set-up are host actions
over the same result contract:

| Host action | Call | Result |
| --- | --- | --- |
| Show or refresh the folder list | `ImapMailProvider.listMailboxes(requestId, withCounts)` | `mailboxesFinished`; open a folder by setting `mailbox` and calling `sync` |
| Drop a message on a folder | `moveMessage(requestId, messageId, targetMailbox)` | `actionFinished` (`action: "move"`) |
| Keep a copy of sent mail | after an accepted `sendFinished`, `append(requestId, sentMailbox, composeMessage(draft).message, ["\\Seen"])` | `appendFinished` |
| Add a calendar account | `CalDavProvider.discoverCalendars(requestId, url)`, then one `CalDavProvider` per chosen `url` | `discoveryFinished` |
| Subscribe to a calendar feed | `IcsCalendarProvider.sync` (on a timer) | `syncFinished` |
| Import or export an `.ics` file | `IcsCalendarProvider.importFile` / `exportFile` | `importFinished` / `exportFinished` |

A POP3 account takes the same three `MailView` requests to
`Pop3MailProvider.sync`, `applyAction` and `fetchAttachment`; its results
have the same shape, so the view needs nothing POP3-specific.

## Credentials

`setSecret(secret, mechanism)` keeps the password or token in memory only.
There is no property to read it back, it is never written to a result,
transcript, store record or log, and any echo in server text is redacted
(`tst_smtp_mail_provider`, `tst_imap_mail_provider`,
`tst_pop3_mail_provider` and `tst_caldav_provider` check results, captured log output and the saved store
file). Persisting a secret is the host's job, normally through the desktop
keyring. Credentials are sent only over TLS (`security: "tls"` or
`"starttls"`, `https:` for CalDAV), to a loopback endpoint (local bridges and
the tests), or when the host sets `allowInsecureAuth` explicitly. A requested
STARTTLS that the server does not offer fails; the providers never fall back
to plain text (D-187). Certificates are verified against the system trust
store (hosts add a private CA to `QSslConfiguration::defaultConfiguration()`).
`tst_provider_tls` runs implicit TLS and STARTTLS for SMTP and IMAP, implicit
TLS and STLS for POP3, and HTTPS
for CalDAV against local servers with a test CA (`tests/fixtures/tls`): with
the CA trusted the exchange succeeds and every credential line arrives inside
TLS; without it the handshake fails before anything is sent. It also covers
IMAP and POP3 `xoauth2` (`tst_pop3_mail_provider` covers POP3's expired-token
error challenge and the RFC 5034 continuation for tokens too long for the
AUTH line) and
CalDAV `bearer`.

Calendar discovery and private iCalendar feeds follow the same rules and
add one: credentials go only to the start URL's site (the same host, or a
host under the parent domain of a host with three or more labels, so
`caldav.icloud.com` may hand over to `p42-caldav.icloud.com`). Discovery
fails when a redirect or `href` leaves the site; a feed drops the
credentials for a redirect to another site or to plain HTTP (public feeds
are often served from a CDN). A feed URL with user info
(`https://user:pw@…`) is refused: credentials belong in `setSecret`.
`tst_provider_tls` runs discovery over HTTPS and a private `webcal:` feed
with the test CA; `tst_caldav_discovery` and `tst_ics_calendar_provider`
check results, captured logs and the saved store for the secret.

## SMTP delivery

Set `host`, `port` (587), `security` (`starttls`), `userName`,
`fromAddress`/`fromName` and `attachmentStore`. `send(requestId, draft)`
reads `to`, `cc`, `bcc`, `subject`, `body`, `richDocumentJson` (or
`bodyHtml`), `inReplyTo`, `references` and `attachments` (IDs in the
attachment store). The message is `multipart/mixed` with a
`multipart/alternative` text and HTML body when the draft has rich content;
the HTML comes from the native rich-text document. Bcc recipients are in the
envelope only. `composeMessage(draft)` returns the exact bytes for preview.

Delivery is all or nothing: if the server refuses any recipient the provider
sends `RSET` and reports `rejected` with `rejectedRecipients`, so a message is
never delivered to only part of its audience. A 4xx reply is `retryable`.
The result carries the `messageId` written into the message.

## IMAP sync and actions

Set `host`, `port` (993), `security` (`tls`), `userName`, `accountId`,
`mailbox` (`INBOX`), `archiveMailbox` (`Archive`), `store` and
`attachmentStore`.

- `sync(requestId)` fetches flags for known UIDs only (updates `unread`/`flagged`,
  removes messages expunged elsewhere) and then new UIDs (at most the
  newest `fetchLimit`; with nothing known yet, only the newest
  `fetchLimit` sequence numbers are listed, and bodies come in batches of
  up to 25 messages or 16 MiB, D-353). Each new message becomes a record with
  `id == messageId == "imap:<accountId>:<mailbox>:<UIDVALIDITY>:<UID>"`,
  `internetMessageId`, `threadId` (the first `References` ID, else
  `In-Reply-To`, else its own Message-ID), sender, recipients, UTC `date`,
  `snippet`, `bodyPlain` (HTML-only mail is reduced to text; the trust mode
  stays `plain`) and attachment metadata with the IMAP `section`. Messages
  larger than `maximumMessageBytes` store headers only (`bodyTruncated`).
  A changed UIDVALIDITY replaces every cached message of that mailbox.
- `applyAction(requestId, messageId, action)` performs `read`, `unread`,
  `flag`, `unflag` (UID STORE), `delete` (\Deleted + UID EXPUNGE, or EXPUNGE
  without UIDPLUS) or `archive` (UID MOVE, or COPY + delete) and then updates
  the store. `reply`/`forward` are host actions (they open a draft) and are
  rejected. A record from an older UIDVALIDITY returns `conflict`.
- `fetchAttachment(requestId, messageId, attachmentId)` downloads only that
  MIME section (`BODY.PEEK[<section>]`), imports the decoded bytes into the
  attachment store under the attachment ID and marks it `local`.

With `autoSave` (default) each accepted operation saves the store. If
another writer saved the file first, the result is `conflict`; reload the
store and sync again. Sync is idempotent because it derives what is new from
the stored UIDs.

### Folders, APPEND and moves (D-299)

`mailbox`, `archiveMailbox`, every append and move target and every listed
`name` are UTF-8 display names; the wire carries modified UTF-7 (RFC 3501
§5.1.3), so `~peter/mail/台北/日本語` is sent as
`~peter/mail/&U,BTFw-/&ZeVnLIqe-` and `R&D` as `R&-D`. Record IDs and
`remoteMailbox` keep the display name. `sync` works for any selectable
mailbox: set `mailbox` and sync; each mailbox's records are separate.

- `listMailboxes(requestId, withCounts = false)` → `mailboxesFinished`.
  It sends `LIST "" "*"`, with `RETURN (SPECIAL-USE)` when the server
  advertises RFC 6154, and needs no store. `result.mailboxes` (INBOX first,
  then server order, at most 10,000):

  | Field | Meaning |
  | --- | --- |
  | `name` | decoded display name (a malformed wire name is its bytes read as UTF-8) |
  | `rawName` | the wire (modified UTF-7) name |
  | `delimiter` | hierarchy delimiter, `""` for a flat name |
  | `flags` | the LIST attributes as sent (`\HasNoChildren`, `\Noselect`, `\Sent`…) |
  | `specialUse` | `inbox`, `sent`, `drafts`, `trash`, `junk`, `archive`, `all`, `flagged` or `""` |
  | `selectable` | false for `\Noselect` / `\NonExistent` |
  | `unread`, `total` | `STATUS (MESSAGES UNSEEN)` counts, `-1` when not asked or refused |

  Roles come from the attributes (Gmail sends them in a plain LIST, which
  is read the same way). A role no attribute claims is taken from the
  well-known name of the last path segment: `Sent Items`/`Sent Mail`/
  `Sent Messages` → sent, `Drafts` → drafts, `Deleted Items`/`Deleted
  Messages`/`Trash`/`Bin` → trash, `Junk`/`Spam`/`Junk E-mail`/`Bulk Mail` →
  junk, `Archive(s)` → archive, `All Mail` → all, `Starred` → flagged. With
  `withCounts` the first 200 selectable mailboxes get a STATUS
  (`countsTruncated` says when there were more); a mailbox whose STATUS is
  refused keeps `-1` and the listing still succeeds.
- `append(requestId, mailbox, rawMessage, flags = [])` → `appendFinished`
  stores RFC 5322 bytes (`SmtpMailProvider.composeMessage(draft).message`)
  with a synchronizing literal; bare LF is sent as CRLF and the transcript
  shows only `{n message bytes}`. `flags` are system flags or keyword atoms
  (`\Seen`, `\Draft`, `$Forwarded`; `\Recent` is refused); the message is
  1 byte to 64 MiB without NUL. The result carries `mailbox`, `flags`,
  `size` and, when the server has UIDPLUS, `appendUid` and
  `appendUidValidity`. A missing mailbox is `rejected` with the server's
  `[TRYCREATE]` text; nothing is created. The store is not touched: the copy
  arrives with the next sync of that mailbox. Needs no store.
- `moveMessage(requestId, messageId, targetMailbox)` → `actionFinished`
  with `action: "move"`: UID MOVE (RFC 6851), or UID COPY + `\Deleted` +
  UID EXPUNGE (EXPUNGE without UIDPLUS, as for `delete`). The local record is
  removed; the destination's next sync brings the message back under that
  mailbox's identity (`targetUid`/`targetUidValidity` report it when the
  server sends COPYUID). The target must differ from `mailbox`. `archive` is
  a move to `archiveMailbox`.

## POP3 download

Set `host`, `port` (995), `security` (`tls`; `starttls` means STLS on port
110; `none` is refused for credentials unless the host is loopback or
`allowInsecureAuth` is set), `userName`, `accountId`, `store` and
`attachmentStore`. The mechanism is `auto` (SASL PLAIN when CAPA offers it,
else USER/PASS), `user` (USER/PASS; `login` is an alias), `plain` or
`xoauth2`. POP3 has one mailbox and no server flags, so the local store *is*
the mailbox (D-298):

- `sync(requestId)` lists `UIDL` and `LIST` and downloads every message
  whose UIDL is not stored yet (at most the newest `fetchLimit`, newest
  first). Each becomes a record with
  `id == messageId == "pop3:<accountId>:<UIDL>"` (the UIDL percent-encoded,
  so `:`, `/` and `%` never appear raw), the same fields as an IMAP record
  (`internetMessageId`, `threadId` by the same rule, sender, recipients, UTC
  `date`, `snippet`, `bodyPlain`, attachment metadata with a `section`,
  `unread: true`) plus `remoteAccount`, `remoteMailbox: "INBOX"`,
  `remoteUidl`, `remoteSize`, `remoteFetchedAt` (UTC download time),
  `remoteOnServer` and `localMailbox: "INBOX"`. A message larger than
  `maximumMessageBytes` is read with `TOP n 0` (headers only,
  `bodyTruncated`). Multi-line responses are dot-unstuffed and bounded
  (RETR at twice `maximumMessageBytes`, listings at 32 MiB).
- Messages deleted on the server by another client are **not** removed
  locally; they get `remoteOnServer: false`. The result's `removedCount` is
  always 0 and `updatedCount` counts those marks.
- **Server deletion.** With `leaveOnServer: false` every downloaded message
  leaves the server; with `leaveOnServer: true` and `deleteAfterDays > 0`
  the ones downloaded more than that many days ago (by `remoteFetchedAt`)
  do. A server copy is deleted only when the local copy is complete and
  saved: the body was downloaded (not `bodyTruncated`), every attachment's
  bytes are in the attachment store (sync imports them first, re-reading a
  stored message once if needed), and this provider's own `autoSave` save
  succeeded. Only then does a second session `DELE` those UIDLs and `QUIT`
  (which commits them). With `autoSave: false` sync never deletes. The
  result adds `deletedCount`, `keptOnServerCount` (due but incomplete),
  `deleteState` and, when the second session failed, `deleteError`; the
  download itself stays `accepted` and the next sync retries the deletion.
- `applyAction(requestId, messageId, action)`: `read`, `unread`, `flag`,
  `unflag` and `archive` (`localMailbox: "Archive"`) are local store edits,
  accepted after the save without connecting. `delete` DELEs the server copy
  by UIDL when `remoteOnServer` is true (`removedFromServer` in the result)
  and then removes the local record; if the server is offline the local
  record stays. `reply`/`forward` are host actions and are rejected.
- `fetchAttachment(requestId, messageId, attachmentId)` re-reads the server
  copy (`RETR` by UIDL), imports the part's decoded bytes into the attachment
  store and marks it `local`. A message no longer on the server is rejected
  with that reason.

A `-ERR [IN-USE]` or `[SYS/TEMP]` reply (a maildrop locked by another
session) is `rejected` with `retryable: true`. The store stale-writer case is
`conflict`, as for IMAP, and no server copy is deleted after a conflict.

## CalDAV events and tasks

Set `collectionUrl` (the calendar collection), `userName`, `accountId`,
`calendarId`, `defaultZone` (for floating times) and `store`; the mechanism
is `basic` or `bearer`.

`sync(requestId)` lists the collection with ETags, fetches changed members,
removes members deleted remotely, then writes local edits back with
`If-Match` and creates new records with `If-None-Match: *`. A record takes
part when its `remoteAccount` equals `accountId`; a new local record needs
only that field. Synced records carry `remoteHref`, `remoteEtag`,
`remoteUid`, `remoteIcs` (the server's text, so unmapped properties such as
alarms, attendees and overridden instances survive a write), `syncDigest`
and `syncState`.

A local edit is detected by comparing a digest of the mapped fields with
`syncDigest`, so `moveTask`, `shiftEventLocal` and plain `upsert` edits need
no extra marking. When the same record changed on the server, or was deleted
there, the result is `conflict`, the local record gets
`syncState: "conflict"`, and `conflicts` lists the remote version. Nothing is
overwritten. `resolveConflict(requestId, collection, id, "local")` writes the
local version with `If-Match` on the remote ETag it last saw (a further
remote edit is a new conflict); `"remote"` adopts the server version.
`removeRemote(requestId, collection, id)` deletes with `If-Match`. Conflict
details live in memory; after a restart the next sync detects them again
from `syncState` and the ETags.

### Calendar discovery

`discoverCalendars(requestId, url)` → `discoveryFinished` finds an
account's calendars from a server root, a principal, a home set or a
collection URL (`collectionUrl` when `url` is empty), with the provider's
`userName`/secret (`basic` or `bearer`); it needs no store:

1. PROPFIND Depth 0 on the URL for `current-user-principal` (RFC 5397),
   `calendar-home-set` (RFC 4791), `resourcetype` and the calendar
   properties. A calendar collection answers for itself.
2. When that is not a DAV resource or names nothing, the same origin's
   `/.well-known/caldav` (RFC 6764), following its redirect.
3. The principal's `calendar-home-set`; a plain collection with neither is
   taken as the home set.
4. PROPFIND Depth 1 on the home set; every member whose `resourcetype`
   contains `calendar` is a calendar (at most 500).

The result has `principalUrl`, `homeSetUrl` and `calendars`:
`{url (absolute, ends with "/"), displayName (the last path segment when
unnamed), color ("#rrggbb" from Apple's calendar-color, or ""), description,
components (supported-calendar-component-set; VEVENT and VTODO when not
reported), readOnly (current-user-privilege-set without write,
write-content or all; false when not reported)}`. Redirects are followed up
to 5 times. Every request must be `https:` unless the host is loopback or
`allowInsecureAuth` is set, and credentials stay on the start URL's site
(see [Credentials](#credentials)). Google's CalDAV starts at
`https://apidata.googleusercontent.com/caldav/v2/<calendar-id>/user` with a
bearer token; iCloud at `https://caldav.icloud.com/`; Fastmail and
Nextcloud at their server root.

`CalDavProvider.sync` never pushes a record that carries `icsFeed`, so a
feed replica is safe even under the same `accountId`.

### iCalendar mapping

| iCalendar | Event field | Notes |
| --- | --- | --- |
| `SUMMARY`, `DESCRIPTION`, `LOCATION` | `title`, `description`, `location` | text escaping per RFC 5545 |
| `DTSTART;TZID=` / `Z` / floating / `VALUE=DATE` | `startLocal`, `zone`, `allDay` | Windows zone names map to IANA |
| `DTEND` (any zone) or `DURATION` | `endLocal` | converted to the start's zone; all-day end is exclusive |
| `RRULE` FREQ, INTERVAL, BYDAY (weekly), COUNT, UNTIL | `recurrenceRule` | UNTIL becomes the local end date |
| other `RRULE` parts | `recurrenceRaw` | `recurrenceRule` is null; the raw rule is written back unchanged |
| `EXDATE` | `exceptions` | local `yyyy-MM-ddTHH:mm:ss` starts (`yyyy-MM-dd` for all-day), as `RecurrenceService` expects |
| `VEVENT` with `RECURRENCE-ID` | `overrides` | `{recurrenceId, startLocal, endLocal, title, description, location}` in the master's zone; `STATUS:CANCELLED` becomes an exception |

An override the record no longer lists is removed from the server copy on
the next write, a new one is written as its own `RECURRENCE-ID` component,
and the other properties and nested components of a kept override (alarms,
attendees) survive. `LocalCalendarProjectionModel` places each override at
its moved time under the original occurrence ID.

Tasks map `STATUS` NEEDS-ACTION/IN-PROCESS/COMPLETED/CANCELLED to
`todo`/`doing`/`done`/`cancelled` (other board statuses travel in
`X-QINDATK-STATUS`), `DUE` to `dueDate`, `PRIORITY` 1–4/5/6–9 to
High/Normal/Low, and `X-QINDATK-ORDER`/`X-QINDATK-ASSIGNEE` to `order` and
`assignee`. Written events reference zones by IANA TZID without a VTIMEZONE
block (RFC 7809 style); a server that requires VTIMEZONE needs a host adapter.

## iCalendar feeds and files (D-300)

`Tk.IcsCalendarProvider` reads and writes `.ics` data with the CalDAV codec
(`ical_codec`): the same mapping table, overrides and recurrence rules.

| Property | Default | Meaning |
| --- | --- | --- |
| `store` | — | `LocalProductivityStore` for `events` |
| `accountId`, `calendarId` | `ics` | feed replicas' `remoteAccount` and calendar |
| `feedUrl` | — | `https:`, `http:`, `webcal:` or `webcals:` (both mean `https:`) |
| `userName`, `setSecret(secret, "basic" \| "bearer")` | — | optional credentials of a private feed |
| `defaultZone` | the system zone | zone of floating times |
| `timeoutMs`, `maximumBytes` | 15000, 16 MiB | per request; a larger feed or file is `rejected` |
| `allowInsecureAuth`, `autoSave` | false, true | as for the other providers |

**Feeds.** `sync(requestId)` → `syncFinished` sends a conditional GET
(`If-None-Match`/`If-Modified-Since` from the last 200 response, kept in
memory and on the records as `feedEtag`/`feedLastModified`, so they survive
a restart). A 304 is `accepted` with `unchanged: true` and changes nothing.
A 200 replaces the events of `calendarId` whose `icsFeed` is this feed:
each VEVENT UID becomes `ics:<accountId>:<calendarId>:<uid>` with the mapped
fields and `readOnly: true`, `remoteAccount`, `remoteUid`, `remoteIcs` (the
UID's components and the VTIMEZONEs they use), `icsFeed` (the normalized
URL), `feedEtag`, `feedLastModified`; events the feed no longer has are
removed; records that did not change are not rewritten. The result carries
`added`, `updated`, `removed`, `skipped`, `eventCount`, `calendarName`
(X-WR-CALNAME), `warnings`, `status`, `finalUrl` and `bytes`. Up to 5
redirects are followed; 401/403, a malformed or oversized body and other
failures are `rejected`, 429/502/503/504 and network failures `offline`
(`retryable`), and none of them changes the store. Tasks in a feed are
skipped.

**Import.** `importFile(requestId, path, calendarId)` → `importFinished`
reads a local path or `file:` URL on the worker and adds each VEVENT UID as
an ordinary editable event of `calendarId` (the provider's when empty): ID
`import:<calendarId>:<uid>`, `remoteUid` (the UID) and `remoteIcs` (its
source, the base later writes keep alarms and attendees from), no
`remoteAccount`. An event of that calendar with the same UID is updated
instead of duplicated, so a second import of a file changes nothing; a UID
that belongs to an account or feed record is skipped with a warning. The
result counts `imported`, `updated`, `unchanged` and `skipped` and lists
`eventIds` and `warnings`. Import never deletes.

**Export.** `exportFile(requestId, path, calendarId, eventIds)` →
`exportFinished` writes the calendar's events (or only `eventIds`) as one
VCALENDAR (VERSION 2.0, PRODID, CALSCALE, each VTIMEZONE once) through
`QSaveFile`. Each event is written as CalDAV writes it (`fromRecord` over
`remoteIcs`, UID `remoteUid` else the record ID): RRULE, EXDATE and
overrides as RECURRENCE-ID components. An empty selection is `rejected`
(RFC 5545 needs one component). The result has `exported`, `bytes` and
`path`.

**Without the store.** `parseText(text)` returns `{ok, error, calendarName,
events, tasks, warnings}` (records as import would map them, `id` = UID) and
`serializeEvents(events)` returns `{ok, error, text, count, warnings}`, for
hosts that preview an `.ics` attachment.

**Parsing.** Several VCALENDARs in one stream are read; VJOURNAL and other
components are skipped with a warning. A component without UID gets a
stable `qindatk-<sha1>` UID from its text (DTSTAMP excluded). A TZID that is
neither IANA nor Windows resolves through the longest IANA suffix
(`/mozilla.org/20050126_1/Europe/Berlin`) or the VTIMEZONE's
`X-LIC-LOCATION` and is rewritten to the IANA name; an event whose zone
cannot be resolved is skipped with a warning.

## Host adapters for hosted services

Hosted accounts differ in sign-in and endpoints, not in the protocols above.
A host adapter owns (D-189):

1. **Sign-in.** OAuth or app-password flows, token refresh and keyring
   storage. It passes the current token with `setSecret(token, "xoauth2")`
   (SMTP/IMAP/POP3) or `setSecret(token, "bearer")` (CalDAV) before each request
   and retries once after a `rejected` authentication result.
2. **Discovery.** Hostnames and ports (presets, autoconfig). Once the
   endpoint is known the toolkit finds the rest: calendars with
   `CalDavProvider.discoverCalendars`, special mailboxes with
   `ImapMailProvider.listMailboxes` (the adapter may still override a role,
   for example a provider's archive folder name).
3. **Anything outside the protocols.** A service whose mail or calendar is
   only available through its own REST API is served by an adapter object
   that exposes the same invokables and signals (`send`/`sendFinished`,
   `sync`/`syncFinished`, `applyAction`/`actionFinished`,
   `resolveConflict`/`changeFinished`) and writes the same record fields.
   The views and the store then need no change.

The toolkit ships no hosted-service adapter and no client IDs.

## Limits

- Mailbox names travel in modified UTF-7; UTF8=ACCEPT (RFC 6855) is not
  enabled, so a mailbox a non-conforming server names in raw 8-bit is listed
  (with `rawName`) but cannot be selected. Addresses must be ASCII (no
  SMTPUTF8).
- The Sent copy comes from `composeMessage`, which writes a new Message-ID;
  it differs from the delivered message's until `send` can reuse composed
  bytes. Services that file sent mail themselves (Gmail) need no copy.
- Calendar discovery has no public-suffix list: the parent domain of a
  three-label host counts as its site, so a three-label registrable domain
  (`a.co.uk`) would trust its siblings.
- Feed replicas are protected by convention (`readOnly`), not by the store:
  a local edit is replaced by the next sync. VTIMEZONE rules are not
  interpreted (zones resolve by name). A private feed whose secret is part
  of its URL keeps that URL in `icsFeed` and in the transcript. Each changed
  record is one store `upsert` (one `dataChanged`), as for CalDAV.
- POP3: no APOP (use TLS), no `UIDL`-less servers (sync is rejected), and a
  headers-only message (`bodyTruncated`) stays on the server; there is no
  "download the rest" action yet. Attachments of a message that leaves the
  server count against the attachment store's 8 MiB per-payload bound; one
  that does not fit keeps its message on the server.
- IMAP sync reads the whole message source of new mail (bounded by
  `maximumMessageBytes`) to compute bodies and attachment metadata; the
  attachment bytes are stored only when requested.
- Linux platform and backend qualification belongs to WP-088 (planned, Linux
  only). Hosted-account qualification (WP-089/WP-090), performance
  measurement (WP-091) and screen-reader runs (WP-092) were deferred by the
  user on 2026-09-24.
