// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QMap>
#include <QVariantMap>

// Worker-thread IMAP operations behind ImapMailProvider. Each job receives
// copies of everything it needs and never touches QObjects.
namespace QindaTK::ImapJobs {

enum class Kind { Sync, Action, Attachment };

struct KnownMessage {
    bool unread = false;
    bool flagged = false;
};

struct Job {
    Protocol::Endpoint endpoint;
    Protocol::Credentials credentials;
    QString accountId, mailbox, archiveMailbox;
    int fetchLimit = 200;
    qint64 maximumMessageBytes = 8 * 1024 * 1024;
    // Sync: local knowledge of this mailbox.
    qint64 knownUidValidity = 0;
    QMap<qint64, KnownMessage> known;
    // Action / attachment: the target message.
    qint64 uid = 0, uidValidity = 0;
    QString action, section, encoding;
};

QString recordId(const QString &accountId, const QString &mailbox, qint64 uidValidity, qint64 uid);
// Builds the LocalProductivityStore message record from RFC 5322 source.
QVariantMap messageRecord(const Job &job, qint64 uidValidity, qint64 uid, bool unread, bool flagged,
                          qint64 size, const QByteArray &source, bool truncated);
QVariantMap run(Kind kind, const Job &job);

} // namespace QindaTK::ImapJobs
