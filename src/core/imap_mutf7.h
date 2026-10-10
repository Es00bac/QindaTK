// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

// IMAP mailbox-name encoding (RFC 3501 §5.1.3, "modified UTF-7").
// AGENT-CONTRACT (imap_session.cpp, imap_folders.cpp, docs/productivity-providers.md):
// ImapMailProvider's `mailbox`, `archiveMailbox`, append/move targets and
// every `name` in listMailboxes are decoded UTF-8 display names; only the
// wire (commands, `rawName`) carries the encoded form. Record IDs and
// `remoteMailbox` use the decoded name.
namespace QindaTK::Imap {

// UTF-8 display name -> wire form: printable ASCII except '&' stands for
// itself, '&' becomes "&-", every other run is "&" + base64(UTF-16BE) with
// ',' for '/' and no padding + "-".
QByteArray encodeMailboxName(const QString &name);

// Wire form -> display name. A malformed name (bad base64, odd UTF-16
// length, unpaired surrogate, an unterminated "&" run, a raw 8-bit byte from
// a server that ignores the encoding) is returned as its bytes read as UTF-8
// with *ok = false; listMailboxes then reports it with `rawName` intact.
// AGENT-NOTE (D-341): a run that encodes printable ASCII ("&AGEAYgBj-" for
// "abc") breaks RFC 3501's "shortest form" rule, but real servers send such
// names (and IMAP keywords use such runs, imap_keywords.h), so it decodes
// with *ok = true. Such a name is not canonical: encodeMailboxName() of the
// result differs from `wire`, and commands must send `wire` itself
// (ImapMailProvider.mailboxWireNames).
QString decodeMailboxName(const QByteArray &wire, bool *ok = nullptr);

// True when `wire` decodes and encodeMailboxName() gives it back unchanged,
// so the display name alone reaches the same mailbox.
bool isCanonicalMailboxName(const QByteArray &wire);

// One "&" + base64(UTF-16BE, ',' for '/', no padding) + "-" run holding
// every character of `text`, printable ASCII included (empty for empty
// text). Mailbox names only encode what cannot stand for itself; IMAP
// keywords (imap_keywords.cpp) also encode atom-specials this way.
QByteArray encodeRun(QStringView text);

} // namespace QindaTK::Imap
