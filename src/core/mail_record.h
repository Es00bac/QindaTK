// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "mail_mime.h"

#include <QVariantMap>

// The LocalProductivityStore `messages` record shape shared by the mail
// providers that download messages (ImapMailProvider, Pop3MailProvider).
// AGENT-CONTRACT (MailView.qml, docs/productivity-providers.md): both
// providers write these fields with the same meaning, so MailView and the
// host need nothing provider-specific; each provider adds its own `remote*`
// identity fields on top.
namespace QindaTK::MailRecord {

// Conversation key: the first References ID, else In-Reply-To, else the
// message's own Message-ID, else `fallback` (the record ID).
QString threadRoot(const Mime::Parsed &parsed, const QString &fallback);

struct Options {
    // D-347: keep the original HTML part as `bodyHtml` (untrusted: a host
    // renders it only through its own sanitizer) up to MaximumHtml characters.
    bool keepHtml = false;
};
constexpr qsizetype MaximumHtml = 2 * 1024 * 1024;
constexpr qsizetype MaximumCalendarText = 256 * 1024;

// id/messageId (both `id`), internetMessageId, threadId, sender,
// senderAddress, to, cc, replyTo, subject, UTC date, importance ("low",
// "normal", "high"), snippet, bodyPlain, bodyTrustMode, bodyTruncated and
// attachments [{id "<id>/<section>", name, mimeType, size, section, encoding,
// state "remote", contentId, disposition, inline}] where `inline` marks a
// part the HTML body shows through "cid:" (a picture inside the message,
// not one to list). A message with an iCalendar part adds calendarMethod,
// calendarAttachmentId and, up to MaximumCalendarText, calendarText.
// With options.keepHtml an HTML body adds bodyHtml and bodyTrustMode
// "untrusted-html" (bodyHtmlOmitted when it exceeds MaximumHtml).
QVariantMap fromParsed(const QString &id, const Mime::Parsed &parsed, bool truncated);
QVariantMap fromParsed(const QString &id, const Mime::Parsed &parsed, bool truncated, const Options &options);

// The Content-IDs a "cid:" URL of `html` names (RFC 2392), lower case.
QStringList referencedContentIds(const QString &html);

} // namespace QindaTK::MailRecord
