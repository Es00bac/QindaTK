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
QString decodeMailboxName(const QByteArray &wire, bool *ok = nullptr);

} // namespace QindaTK::Imap
