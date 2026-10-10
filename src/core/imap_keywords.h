// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

// IMAP message flags (RFC 3501 §2.3.2, §9 "flag") and the category <->
// keyword convention (D-340). Pure functions; no I/O.
// AGENT-CONTRACT (imap_mail_jobs.cpp, imap_mail_provider.cpp,
// docs/mail-protocols.md#categories-as-keywords): keywordForCategory() is the
// keyword QindaMail already stores for an Outlook-style category, so a host
// that switches to this mapping finds every category its earlier releases
// wrote: spaces become "_", and "_", "&", the atom-specials, a leading "$" or
// "\" and every non-ASCII character go into "&...-" runs of RFC 3501 §5.1.3's
// modified UTF-7 (printable ASCII included). categoryForKeyword() reads every
// keyword written that way back exactly.
namespace QindaTK::ImapKeywords {

// RFC 3501 ATOM-CHAR: printable ASCII without "(){ %*\"\\]" and CTLs.
bool isAtomChar(QChar c);
// A flag-keyword: 1..1024 ATOM-CHARs.
bool validKeyword(const QString &keyword);
// "\Seen", "\Answered", "\Flagged", "\Deleted", "\Draft" (any case).
bool isSystemFlag(const QString &flag);
// The flag as the RFC spells it ("\\seen" -> "\\Seen"); keywords unchanged.
QString canonicalFlag(const QString &flag);
// A flag a client may STORE or APPEND: a system flag, an RFC 3501
// flag-extension ("\" + atom, reserved for future system flags) or a
// keyword. "\Recent" (session state) and "\*" (a PERMANENTFLAGS marker)
// are refused, as is anything that could break out of the command line.
bool validFlag(const QString &flag);
// The keywords among `flags` (every flag not starting with "\").
QStringList keywordsOf(const QStringList &flags);
// Case-insensitive membership (IMAP flags compare without case).
bool containsFlag(const QStringList &flags, const QString &flag);

// RFC 3501 §7.1 PERMANENTFLAGS: without the response code every flag is
// permanent; with it a flag must be listed, or be a keyword when "\*" is.
bool isPermanent(const QString &flag, const QStringList &permanentFlags, bool permanentFlagsSent);

// Category name -> keyword (empty for an empty name). See the contract above.
QString keywordForCategory(const QString &name);
// True for a keyword this convention could have written: no leading "$" or
// "\" (RFC 5788 registry keywords such as $Forwarded, $Junk, $label1 are
// never categories) and not one of the junk markers mail clients set
// without "$" (Junk, NonJunk, NotJunk).
bool isCategoryKeyword(const QString &keyword);
// Keyword -> category name, or "" when it is not a category keyword.
QString categoryForKeyword(const QString &keyword);
// The categories among `flags`, in flag order, without duplicates.
QStringList categoriesOf(const QStringList &flags);

} // namespace QindaTK::ImapKeywords
