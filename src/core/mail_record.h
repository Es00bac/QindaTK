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

// id/messageId (both `id`), internetMessageId, threadId, sender,
// senderAddress, to, cc, subject, UTC date, snippet, bodyPlain,
// bodyTrustMode, bodyTruncated and attachments
// [{id "<id>/<section>", name, mimeType, size, section, encoding, state "remote"}].
QVariantMap fromParsed(const QString &id, const Mime::Parsed &parsed, bool truncated);

} // namespace QindaTK::MailRecord
