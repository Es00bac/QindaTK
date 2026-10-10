// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QHash>
#include <QMap>
#include <QStringList>
#include <QVariantMap>

// Worker-thread IMAP operations behind ImapMailProvider. Each job receives
// copies of everything it needs and never touches QObjects.
namespace QindaTK::ImapJobs {

// Flags: STORE of flags/keywords (or a category set) on one message;
// Headers: header blocks of messages; Transfer: UID COPY / UID MOVE of a
// UID set (D-340).
enum class Kind { Sync, Action, Attachment, Mailboxes, Append, Flags, Headers, Transfer };

struct KnownMessage {
    bool unread = false;
    bool flagged = false;
    bool answered = false;        // \Answered
    QStringList keywords;         // the record's `keywords` (sorted)
    QStringList categories;       // compared only with Job::keywordCategories
};

struct Job {
    Protocol::Endpoint endpoint;
    Protocol::Credentials credentials;
    QString accountId, mailbox, archiveMailbox;
    // Display name -> the server's own wire name, for names that do not
    // re-encode to what LIST sent (D-341).
    QHash<QString, QByteArray> wireNames;
    int fetchLimit = 200;
    qint64 maximumMessageBytes = 8 * 1024 * 1024;
    bool keepHtml = false;            // records keep bodyHtml (D-347)
    bool keywordCategories = false;   // records carry `categories` from keywords (D-340)
    // Sync: local knowledge of this mailbox.
    qint64 knownUidValidity = 0;
    QMap<qint64, KnownMessage> known;
    // Action / attachment / flags / headers / transfer: the target messages.
    qint64 uid = 0, uidValidity = 0;
    QList<qint64> uids;           // Flags (one), Headers and Transfer
    QString action, section, encoding;
    QString targetMailbox;        // archive and move: display name of the destination
    // Flags: explicit changes, or (setCategories) the wanted category names.
    QStringList addFlags, removeFlags;
    bool setCategories = false;
    QStringList categories;
    QStringList headerFields;     // Headers: empty = the whole header
    bool move = false;            // Transfer: MOVE instead of COPY
    // Mailboxes: STATUS counts. Append: destination, bytes and flags.
    bool withCounts = false;
    QByteArray appendMessage;
    QStringList appendFlags;
};

QString recordId(const QString &accountId, const QString &mailbox, qint64 uidValidity, qint64 uid);
// The wire form of a display name: the server's own name when known, else
// modified UTF-7.
QByteArray wireName(const Job &job, const QString &mailbox);
// Builds the LocalProductivityStore message record from RFC 5322 source and
// the message's FLAGS (unread, flagged, answered, keywords; categories when
// the job maps them).
QVariantMap messageRecord(const Job &job, qint64 uidValidity, qint64 uid, const QStringList &flags,
                          qint64 size, const QByteArray &source, bool truncated);
// The same from the two flags ImapMailProvider knew before D-340.
QVariantMap messageRecord(const Job &job, qint64 uidValidity, qint64 uid, bool unread, bool flagged,
                          qint64 size, const QByteArray &source, bool truncated);
// unread, flagged, answered, keywords (sorted) and, with keywordCategories,
// categories of a message whose FLAGS are `flags`.
QVariantMap flagFields(const Job &job, const QStringList &flags);
QVariantMap run(Kind kind, const Job &job);

} // namespace QindaTK::ImapJobs
