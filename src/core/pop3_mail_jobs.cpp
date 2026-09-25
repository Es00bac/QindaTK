// SPDX-License-Identifier: LGPL-3.0-or-later
#include "pop3_mail_jobs.h"

#include "mail_mime.h"
#include "mail_record.h"
#include "pop3_session.h"

#include <QSet>
#include <QUrl>
#include <algorithm>

namespace QindaTK::Pop3Jobs {
namespace {
// AGENT-NOTE: LIST reports octets as the server stores them; a maildrop kept
// with bare LF grows when sent with CRLF. RETR is bounded at twice the limit
// that selected it for a full download, which still bounds memory.
qint64 retrieveLimit(const Job &job) { return 2 * job.maximumMessageBytes + 64 * 1024; }

const Pop3::Entry *findEntry(const QList<Pop3::Entry> &entries, const QString &uidl)
{
    for (const Pop3::Entry &entry : entries)
        if (QString::fromLatin1(entry.uidl) == uidl) return &entry;
    return nullptr;
}

bool syncMaildrop(Pop3::Session &session, Protocol::Channel &channel, const Job &job, QVariantMap *out)
{
    QList<Pop3::Entry> entries;
    if (!session.listing(&entries)) return false;
    QSet<QString> listed;
    QList<Pop3::Entry> fresh;
    for (const Pop3::Entry &entry : std::as_const(entries)) {
        const QString uidl = QString::fromLatin1(entry.uidl);
        listed.insert(uidl);
        if (!job.known.contains(uidl)) fresh.append(entry);
    }
    // AGENT-NOTE: POP3 semantics — the local store is the mailbox. A message
    // that left the server stays local; it is only marked remoteOnServer false.
    QVariantList gone;
    for (auto it = job.known.cbegin(); it != job.known.cend(); ++it)
        if (it->onServer && !listed.contains(it.key())) gone.append(it.key());

    // Maildrop order is arrival order: the newest `fetchLimit` new messages.
    std::sort(fresh.begin(), fresh.end(),
              [](const Pop3::Entry &a, const Pop3::Entry &b) { return a.number > b.number; });
    if (job.fetchLimit > 0 && fresh.size() > job.fetchLimit) fresh.resize(job.fetchLimit);
    QVariantList added, candidates;
    QVariantMap data;
    for (const Pop3::Entry &entry : std::as_const(fresh)) {
        const QString uidl = QString::fromLatin1(entry.uidl);
        const bool truncated = entry.size > job.maximumMessageBytes;
        QByteArray source;
        if (truncated) {
            // Headers only; a server without TOP leaves an empty header set.
            if (!session.headers(entry.number, &source) && channel.failure() != Protocol::Failure::None)
                return false;
        } else if (!session.retrieve(entry.number, &source, retrieveLimit(job))) {
            return false;
        }
        added.append(messageRecord(job, uidl, entry.size, source, truncated));
        if (job.leaveOnServer || truncated) continue;
        candidates.append(uidl);
        if (job.canImportAttachments) {
            const QString id = recordId(job.accountId, uidl);
            const QVariantMap bytes = attachmentData(id, source);
            if (!bytes.isEmpty()) data.insert(id, bytes);
        }
    }

    // Stored messages whose server copy is due to go: every one when
    // leaveOnServer is off, else those older than deleteAfterDays.
    const QDateTime cutoff = job.deleteAfterDays > 0 ? job.now.addDays(-job.deleteAfterDays) : QDateTime();
    for (const Pop3::Entry &entry : std::as_const(entries)) {
        const QString uidl = QString::fromLatin1(entry.uidl);
        const auto known = job.known.constFind(uidl);
        if (known == job.known.cend() || known->truncated) continue;
        const bool expired = cutoff.isValid() && known->fetchedAt.isValid() && known->fetchedAt < cutoff;
        if (job.leaveOnServer && !expired) continue;
        if (known->remoteAttachments) {
            // The local copy lacks attachment bytes: read them once more
            // before the only other copy is deleted.
            if (!job.canImportAttachments || entry.size > job.maximumMessageBytes) continue;
            QByteArray source;
            if (!session.retrieve(entry.number, &source, retrieveLimit(job))) return false;
            const QString id = recordId(job.accountId, uidl);
            data.insert(id, attachmentData(id, source));
        }
        candidates.append(uidl);
    }
    out->insert(QStringLiteral("exists"), entries.size());
    out->insert(QStringLiteral("added"), added);
    out->insert(QStringLiteral("goneUidls"), gone);
    out->insert(QStringLiteral("attachmentData"), data);
    out->insert(QStringLiteral("deleteCandidates"), candidates);
    return true;
}

bool removeMessage(Pop3::Session &session, const Job &job, QVariantMap *out)
{
    QList<Pop3::Entry> entries;
    if (!session.listing(&entries)) return false;
    const Pop3::Entry *entry = findEntry(entries, job.uidl);
    if (entry && !session.remove(entry->number)) return false;
    if (!session.quit()) return false;          // QUIT commits the DELE
    out->insert(QStringLiteral("removedFromServer"), entry != nullptr);
    return true;
}

bool download(Pop3::Session &session, Protocol::Channel &channel, const Job &job, QVariantMap *out)
{
    QList<Pop3::Entry> entries;
    if (!session.listing(&entries)) return false;
    const Pop3::Entry *entry = findEntry(entries, job.uidl);
    if (!entry) {
        channel.fail(Protocol::Failure::Rejected,
                     QStringLiteral("The message is no longer on the POP3 server; the attachment cannot be downloaded"));
        return false;
    }
    QByteArray source;
    if (!session.retrieve(entry->number, &source, retrieveLimit(job))) return false;
    for (const Mime::Part &part : Mime::parse(source).attachments) {
        if (part.section != job.section) continue;
        out->insert(QStringLiteral("bytes"), part.data);
        return true;
    }
    channel.fail(Protocol::Failure::Rejected, QStringLiteral("The server copy does not contain this attachment"));
    return false;
}

bool cleanup(Pop3::Session &session, const Job &job, QVariantMap *out)
{
    QList<Pop3::Entry> entries;
    if (!session.listing(&entries)) return false;
    QVariantList deleted;
    for (const QString &uidl : job.remove) {
        const Pop3::Entry *entry = findEntry(entries, uidl);
        if (!entry) continue;                   // already gone: nothing to do
        if (!session.remove(entry->number)) return false;
        deleted.append(uidl);
    }
    if (!session.quit()) return false;
    out->insert(QStringLiteral("deletedUidls"), deleted);
    return true;
}
} // namespace

QString recordId(const QString &accountId, const QString &uidl)
{
    return QStringLiteral("pop3:%1:%2").arg(accountId, QString::fromLatin1(QUrl::toPercentEncoding(uidl)));
}

QVariantMap messageRecord(const Job &job, const QString &uidl, qint64 size, const QByteArray &source,
                          bool truncated)
{
    QVariantMap record = MailRecord::fromParsed(recordId(job.accountId, uidl), Mime::parse(source), truncated);
    record.insert(QVariantMap{{QStringLiteral("unread"), true}, {QStringLiteral("flagged"), false},
                              {QStringLiteral("localMailbox"), QStringLiteral("INBOX")},
                              {QStringLiteral("remoteAccount"), job.accountId},
                              {QStringLiteral("remoteMailbox"), QStringLiteral("INBOX")},
                              {QStringLiteral("remoteUidl"), uidl}, {QStringLiteral("remoteSize"), size},
                              {QStringLiteral("remoteFetchedAt"), job.now.toUTC().toString(Qt::ISODate)},
                              {QStringLiteral("remoteOnServer"), true}});
    return record;
}

QVariantMap attachmentData(const QString &recordId, const QByteArray &source)
{
    QVariantMap data;
    for (const Mime::Part &part : Mime::parse(source).attachments)
        data.insert(recordId + QLatin1Char('/') + part.section, part.data);
    return data;
}

QVariantMap run(Kind kind, const Job &job)
{
    Protocol::Channel channel;
    channel.addSecret(job.credentials.secret);
    Pop3::Session session(&channel);
    QVariantMap result;
    bool ok = false;
    if (session.open(job.endpoint, job.credentials)) {
        switch (kind) {
        case Kind::Sync: ok = syncMaildrop(session, channel, job, &result); break;
        case Kind::Remove: ok = removeMessage(session, job, &result); break;
        case Kind::Attachment: ok = download(session, channel, job, &result); break;
        case Kind::Cleanup: ok = cleanup(session, job, &result); break;
        }
    }
    // Sync and Attachment marked nothing; QUIT only releases the maildrop lock.
    if (ok && (kind == Kind::Sync || kind == Kind::Attachment)) session.quit();
    const Protocol::Failure failure = channel.failure();
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("state"), ok ? QStringLiteral("accepted")
                  : Protocol::failureState(failure == Protocol::Failure::None ? Protocol::Failure::Rejected : failure));
    result.insert(QStringLiteral("error"), ok ? QString() : channel.error());
    result.insert(QStringLiteral("retryable"), !ok && (failure == Protocol::Failure::Offline || session.transient()));
    result.insert(QStringLiteral("transcript"), channel.transcript());
    return result;
}

} // namespace QindaTK::Pop3Jobs
