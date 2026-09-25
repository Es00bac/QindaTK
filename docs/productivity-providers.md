<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Mail, calendar and task providers

Three protocol providers connect the local productivity store
([productivity-store.md](productivity-store.md)) to standard servers. The
views (`MailView`, `RichMailComposer`, `CalendarView`, `TaskBoard`) still
only emit correlated requests; the host calls a provider and passes the
result's `state` to the view's `acknowledge(requestId, state, message)`.

| Type | Protocol | Local side |
| --- | --- | --- |
| `Tk.SmtpMailProvider` | SMTP submission (RFC 5321: EHLO, STARTTLS, AUTH PLAIN/LOGIN/XOAUTH2, MAIL/RCPT/DATA) | draft records + `LocalAttachmentStore` bytes |
| `Tk.ImapMailProvider` | IMAP4rev1 (RFC 3501; SASL-IR, UIDPLUS, MOVE when offered) | `messages` records + attachment bytes on demand |
| `Tk.CalDavProvider` | CalDAV over WebDAV (RFC 4791/4918: PROPFIND Depth 1, GET, PUT/DELETE with If-Match) and iCalendar (RFC 5545 subset) | `events` (VEVENT) and `tasks` (VTODO) |

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

## Credentials

`setSecret(secret, mechanism)` keeps the password or token in memory only.
There is no property to read it back, it is never written to a result,
transcript, store record or log, and any echo in server text is redacted
(`tst_smtp_mail_provider`, `tst_imap_mail_provider` and
`tst_caldav_provider` check results, captured log output and the saved store
file). Persisting a secret is the host's job, normally through the desktop
keyring. Credentials are sent only over TLS (`security: "tls"` or
`"starttls"`, `https:` for CalDAV), to a loopback endpoint (local bridges and
the tests), or when the host sets `allowInsecureAuth` explicitly. A requested
STARTTLS that the server does not offer fails; the providers never fall back
to plain text (D-187).

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

- `sync(requestId)` fetches flags for known UIDs (updates `unread`/`flagged`,
  removes messages expunged elsewhere) and then new UIDs (at most the
  newest `fetchLimit`). Each new message becomes a record with
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

### iCalendar mapping

| iCalendar | Event field | Notes |
| --- | --- | --- |
| `SUMMARY`, `DESCRIPTION`, `LOCATION` | `title`, `description`, `location` | text escaping per RFC 5545 |
| `DTSTART;TZID=` / `Z` / floating / `VALUE=DATE` | `startLocal`, `zone`, `allDay` | Windows zone names map to IANA |
| `DTEND` (any zone) or `DURATION` | `endLocal` | converted to the start's zone; all-day end is exclusive |
| `RRULE` FREQ, INTERVAL, BYDAY (weekly), COUNT, UNTIL | `recurrenceRule` | UNTIL becomes the local end date |
| other `RRULE` parts | `recurrenceRaw` | `recurrenceRule` is null; the raw rule is written back unchanged |
| `EXDATE` | `exceptions` | local `yyyy-MM-ddTHH:mm:ss` starts, as `RecurrenceService` expects |

Tasks map `STATUS` NEEDS-ACTION/IN-PROCESS/COMPLETED/CANCELLED to
`todo`/`doing`/`done`/`cancelled` (other board statuses travel in
`X-QINDATK-STATUS`), `DUE` to `dueDate`, `PRIORITY` 1–4/5/6–9 to
High/Normal/Low, and `X-QINDATK-ORDER`/`X-QINDATK-ASSIGNEE` to `order` and
`assignee`. Written events reference zones by IANA TZID without a VTIMEZONE
block (RFC 7809 style); a server that requires VTIMEZONE needs a host adapter.

## Host adapters for hosted services

Hosted accounts differ in sign-in and endpoints, not in the protocols above.
A host adapter owns (D-189):

1. **Sign-in.** OAuth or app-password flows, token refresh and keyring
   storage. It passes the current token with `setSecret(token, "xoauth2")`
   (SMTP/IMAP) or `setSecret(token, "bearer")` (CalDAV) before each request
   and retries once after a `rejected` authentication result.
2. **Discovery.** Hostnames, ports, the CalDAV collection URL and special
   mailboxes (for example a provider's archive folder name).
3. **Anything outside the protocols.** A service whose mail or calendar is
   only available through its own REST API is served by an adapter object
   that exposes the same invokables and signals (`send`/`sendFinished`,
   `sync`/`syncFinished`, `applyAction`/`actionFinished`,
   `resolveConflict`/`changeFinished`) and writes the same record fields.
   The views and the store then need no change.

The toolkit ships no hosted-service adapter and no client IDs.

## Limits

- Mailbox names must be printable ASCII (no modified UTF-7); addresses must be
  ASCII (no SMTPUTF8).
- IMAP sync reads the whole message source of new mail (bounded by
  `maximumMessageBytes`) to compute bodies and attachment metadata; the
  attachment bytes are stored only when requested.
- Overridden recurrence instances (`RECURRENCE-ID`) are preserved on the
  server and counted in `recurrenceOverrides`, but not projected locally.
- Performance measurement, other platforms, screen-reader runs and hosted
  account qualification belong to WP-088 to WP-094.
