// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "imap_session.h"

#include <QStringList>
#include <QVariantMap>

// Mailbox-level IMAP operations for ImapMailProvider's worker jobs: LIST with
// RFC 6154 special-use roles, bounded STATUS counts, APPEND with a
// synchronizing literal (RFC 3501 §6.3.11, UIDPLUS APPENDUID) and the
// COPYUID reply of MOVE/COPY. Internal to the library.
namespace QindaTK::ImapFolders {

constexpr int MaximumListed = 10000;      // LIST entries kept in one result
constexpr int MaximumCounted = 200;       // mailboxes asked for STATUS
constexpr qint64 MaximumAppendBytes = 64LL * 1024 * 1024;

// One LIST reply entry as reported in result.mailboxes:
// {name, rawName, canonical, delimiter, flags, specialUse, selectable,
// unread, total}. `canonical` is false when the display name alone would
// not reach the mailbox (D-341): commands then send `rawName`.
QVariantList parseList(const QList<QByteArray> &untagged);
// "\\Sent" -> "sent" … ; "" for attributes without a special-use role.
QString specialUseOf(const QStringList &flags);
// Fills in the roles no attribute claimed from well-known names such as
// "Sent Items", "Deleted Items" or "[Gmail]/Sent Mail" (last path segment).
void applyNameHeuristics(QVariantList *mailboxes);

// LIST (with RETURN (SPECIAL-USE) when advertised), then STATUS (MESSAGES
// UNSEEN) for up to MaximumCounted selectable mailboxes when `withCounts`.
bool listMailboxes(Imap::Session &session, bool withCounts, QVariantMap *out);

// RFC 3501 flag syntax (D-340): a system flag ("\\Seen"), a flag-extension
// or a keyword atom; "\\Recent" and anything else are refused before they
// reach the command line. Same as ImapKeywords::validFlag.
bool validFlag(const QString &flag);
// APPEND of `message` (bare LF normalised to CRLF) to the display-name
// `mailbox`; out gets appendUid/appendUidValidity when the server sends
// APPENDUID. The message bytes never enter the transcript.
// Input (flags, size) is validated by the provider before connecting.
// A non-empty `wire` is sent instead of the encoded display name (D-341).
bool append(Imap::Session &session, const QString &mailbox, const QByteArray &message,
            const QStringList &flags, QVariantMap *out, const QByteArray &wire = {});

// Reads "[COPYUID <validity> <source> <target>]" from a MOVE/COPY exchange
// into targetUid/targetUidValidity when the source UID is `uid`.
void readCopyUid(const QList<QByteArray> &untagged, const QString &status, qint64 uid, QVariantMap *out);

} // namespace QindaTK::ImapFolders
