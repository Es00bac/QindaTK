// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "protocol_client.h"

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

// Worker-thread POP3 operations behind Pop3MailProvider. Each job receives
// copies of everything it needs and never touches QObjects (D-186).
namespace QindaTK::Pop3Jobs {

// Sync downloads; Remove is the `delete` action; Attachment re-reads one
// message; Cleanup DELEs the UIDLs the provider saved locally (D-298).
enum class Kind { Sync, Remove, Attachment, Cleanup };

// What the store already holds for one UIDL of this account.
struct KnownMessage {
    bool onServer = true;         // remoteOnServer
    bool truncated = false;       // bodyTruncated: the local copy lacks the body
    bool remoteAttachments = false;   // an attachment is still state "remote"
    QDateTime fetchedAt;          // remoteFetchedAt (UTC)
};

struct Job {
    Protocol::Endpoint endpoint;
    Protocol::Credentials credentials;
    QString accountId;
    int fetchLimit = 200;
    qint64 maximumMessageBytes = 8 * 1024 * 1024;
    bool keepHtml = false;
    bool leaveOnServer = true;
    int deleteAfterDays = 0;
    bool canImportAttachments = false;   // an attachment store is configured
    QDateTime now;                // UTC; remoteFetchedAt and the retention cut-off
    // Sync: every UIDL of this account in the store.
    QHash<QString, KnownMessage> known;
    // Remove / Attachment: the target message and part.
    QString uidl, section;
    // Cleanup: UIDLs to DELE.
    QStringList remove;
};

// "pop3:<accountId>:<percent-encoded UIDL>" (':' '/' '%' are encoded, so the
// attachment suffix "/<section>" stays unambiguous).
QString recordId(const QString &accountId, const QString &uidl);
// The LocalProductivityStore message record for a downloaded message.
QVariantMap messageRecord(const Job &job, const QString &uidl, qint64 size, const QByteArray &source,
                          bool truncated);
// Decoded bytes of every attachment part, keyed by attachment ID.
QVariantMap attachmentData(const QString &recordId, const QByteArray &source);

// AGENT-CONTRACT (pop3_mail_provider.cpp): a Sync result carries `added`
// (records), `goneUidls` (known UIDLs no longer listed), `attachmentData`
// ({recordId: {attachmentId: bytes}} for messages about to leave the server)
// and `deleteCandidates` (UIDLs the provider may DELE once the store saved
// them completely). Remove returns `removedFromServer`; Attachment returns
// `bytes`; Cleanup returns `deletedUidls`. Jobs never DELE except Remove and
// Cleanup, and both commit only through a successful QUIT.
QVariantMap run(Kind kind, const Job &job);

} // namespace QindaTK::Pop3Jobs
