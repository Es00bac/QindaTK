// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_mail_jobs.h"

#include "imap_session.h"
#include "mail_mime.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace QindaTK::ImapJobs {
namespace {
QByteArray number(qint64 value) { return QByteArray::number(value); }

bool hasFlag(const Imap::Value *flags, const char *name)
{
    if (!flags || flags->kind != Imap::Value::List) return false;
    for (const Imap::Value &flag : flags->items)
        if (flag.text.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

qint64 uidOf(const Imap::FetchItem &item)
{
    const Imap::Value *uid = item.find("UID");
    return uid ? uid->text.toLongLong() : 0;
}

QString threadRoot(const Mime::Parsed &parsed, const QString &fallback)
{
    static const QRegularExpression firstId(QStringLiteral("<([^>]+)>"));
    const QRegularExpressionMatch match = firstId.match(parsed.references);
    if (match.hasMatch()) return match.captured(1).trimmed();
    if (!parsed.inReplyTo.isEmpty()) return parsed.inReplyTo;
    return parsed.messageId.isEmpty() ? fallback : parsed.messageId;
}

bool syncMailbox(Imap::Session &session, const Job &job, qint64 validity, qint64 exists, QVariantMap *out)
{
    const bool reset = job.knownUidValidity != 0 && job.knownUidValidity != validity;
    qint64 lastUid = 0;
    if (!reset)
        for (auto it = job.known.cbegin(); it != job.known.cend(); ++it) lastUid = std::max(lastUid, it.key());
    QVariantList updated, removed, added;
    if (!reset && !job.known.isEmpty()) {
        QSet<qint64> seen;
        if (exists > 0 && lastUid > 0) {
            QList<QByteArray> untagged;
            if (!session.command("UID FETCH 1:" + number(lastUid) + " (UID FLAGS)", &untagged)) return false;
            for (const Imap::FetchItem &item : Imap::fetchItems(untagged)) {
                const qint64 uid = uidOf(item);
                if (!job.known.contains(uid)) continue;
                seen.insert(uid);
                const bool unread = !hasFlag(item.find("FLAGS"), "\\Seen");
                const bool flagged = hasFlag(item.find("FLAGS"), "\\Flagged");
                const KnownMessage local = job.known.value(uid);
                if (local.unread != unread || local.flagged != flagged)
                    updated.append(QVariantMap{{QStringLiteral("uid"), uid}, {QStringLiteral("unread"), unread},
                                               {QStringLiteral("flagged"), flagged}});
            }
        }
        for (auto it = job.known.cbegin(); it != job.known.cend(); ++it)
            if (!seen.contains(it.key())) removed.append(it.key());
    }
    if (exists > 0) {
        QList<QByteArray> untagged;
        if (!session.command("UID FETCH " + number(lastUid + 1) + ":* (UID RFC822.SIZE)", &untagged)) return false;
        QList<QPair<qint64, qint64>> fresh;
        for (const Imap::FetchItem &item : Imap::fetchItems(untagged)) {
            const qint64 uid = uidOf(item);
            const Imap::Value *size = item.find("RFC822.SIZE");
            if (uid > lastUid) fresh.append({uid, size ? size->text.toLongLong() : 0});
        }
        std::sort(fresh.begin(), fresh.end());
        fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());
        if (job.fetchLimit > 0 && fresh.size() > job.fetchLimit) fresh = fresh.mid(fresh.size() - job.fetchLimit);
        for (const auto &[uid, size] : std::as_const(fresh)) {
            const bool truncated = size > job.maximumMessageBytes;
            const QByteArray section = truncated ? "BODY.PEEK[HEADER]" : "BODY.PEEK[]";
            QList<QByteArray> message;
            if (!session.command("UID FETCH " + number(uid) + " (UID FLAGS " + section + ")", &message)) return false;
            for (const Imap::FetchItem &item : Imap::fetchItems(message)) {
                if (uidOf(item) != uid) continue;
                const Imap::Value *body = item.find("BODY[");
                const Imap::Value *flags = item.find("FLAGS");
                added.append(messageRecord(job, validity, uid, !hasFlag(flags, "\\Seen"), hasFlag(flags, "\\Flagged"),
                                           size, body ? body->text : QByteArray(), truncated));
                break;
            }
        }
    }
    out->insert(QStringLiteral("uidValidity"), QString::number(validity));
    out->insert(QStringLiteral("reset"), reset);
    out->insert(QStringLiteral("exists"), exists);
    out->insert(QStringLiteral("added"), added);
    out->insert(QStringLiteral("updated"), updated);
    out->insert(QStringLiteral("removedUids"), removed);
    return true;
}

bool expunge(Imap::Session &session, const QByteArray &uid)
{
    if (!session.command("UID STORE " + uid + " +FLAGS.SILENT (\\Deleted)")) return false;
    // AGENT-NOTE: without UIDPLUS a plain EXPUNGE also removes any other
    // message already marked \Deleted in this mailbox (RFC 3501 §6.4.3).
    return session.hasCapability("UIDPLUS") ? session.command("UID EXPUNGE " + uid) : session.command("EXPUNGE");
}

bool runAction(Imap::Session &session, const Job &job, Protocol::Channel &channel)
{
    const QByteArray uid = number(job.uid);
    if (job.action == QLatin1String("read")) return session.command("UID STORE " + uid + " +FLAGS.SILENT (\\Seen)");
    if (job.action == QLatin1String("unread")) return session.command("UID STORE " + uid + " -FLAGS.SILENT (\\Seen)");
    if (job.action == QLatin1String("flag")) return session.command("UID STORE " + uid + " +FLAGS.SILENT (\\Flagged)");
    if (job.action == QLatin1String("unflag")) return session.command("UID STORE " + uid + " -FLAGS.SILENT (\\Flagged)");
    if (job.action == QLatin1String("delete")) return expunge(session, uid);
    if (job.action == QLatin1String("archive")) {
        if (session.hasCapability("MOVE")) return session.command("UID MOVE " + uid + ' ' + Imap::quoted(job.archiveMailbox));
        return session.command("UID COPY " + uid + ' ' + Imap::quoted(job.archiveMailbox)) && expunge(session, uid);
    }
    channel.fail(Protocol::Failure::Rejected, QStringLiteral("Unsupported mail action %1").arg(job.action));
    return false;
}

bool download(Imap::Session &session, const Job &job, Protocol::Channel &channel, QVariantMap *out)
{
    static const QRegularExpression validSection(QStringLiteral("^\\d+(\\.\\d+)*$"));
    if (!validSection.match(job.section).hasMatch()) {
        channel.fail(Protocol::Failure::Rejected, QStringLiteral("Invalid attachment section"));
        return false;
    }
    QList<QByteArray> untagged;
    if (!session.command("UID FETCH " + number(job.uid) + " (UID BODY.PEEK[" + job.section.toLatin1() + "])", &untagged))
        return false;
    for (const Imap::FetchItem &item : Imap::fetchItems(untagged)) {
        if (uidOf(item) != job.uid) continue;
        const Imap::Value *body = item.find("BODY[");
        if (!body || body->kind == Imap::Value::Nil) break;
        out->insert(QStringLiteral("bytes"), Mime::decodeTransfer(body->text, job.encoding));
        return true;
    }
    channel.fail(Protocol::Failure::Rejected, QStringLiteral("The server returned no data for this attachment"));
    return false;
}
} // namespace

QString recordId(const QString &accountId, const QString &mailbox, qint64 uidValidity, qint64 uid)
{
    return QStringLiteral("imap:%1:%2:%3:%4").arg(accountId, mailbox).arg(uidValidity).arg(uid);
}

QVariantMap messageRecord(const Job &job, qint64 uidValidity, qint64 uid, bool unread, bool flagged,
                          qint64 size, const QByteArray &source, bool truncated)
{
    const Mime::Parsed parsed = Mime::parse(source);
    const QString id = recordId(job.accountId, job.mailbox, uidValidity, uid);
    QVariantList attachments;
    for (const Mime::Part &part : parsed.attachments)
        attachments.append(QVariantMap{
            {QStringLiteral("id"), id + QLatin1Char('/') + part.section},
            {QStringLiteral("name"), part.name}, {QStringLiteral("mimeType"), part.mimeType},
            {QStringLiteral("size"), part.size}, {QStringLiteral("section"), part.section},
            {QStringLiteral("encoding"), part.encoding}, {QStringLiteral("state"), QStringLiteral("remote")}});
    return {
        {QStringLiteral("id"), id}, {QStringLiteral("messageId"), id},
        {QStringLiteral("internetMessageId"), parsed.messageId},
        {QStringLiteral("threadId"), threadRoot(parsed, id)},
        {QStringLiteral("sender"), parsed.from.isEmpty() ? parsed.fromAddress : parsed.from},
        {QStringLiteral("senderAddress"), parsed.fromAddress},
        {QStringLiteral("to"), parsed.to}, {QStringLiteral("cc"), parsed.cc},
        {QStringLiteral("subject"), parsed.subject},
        {QStringLiteral("date"), parsed.date.isValid() ? parsed.date.toUTC().toString(Qt::ISODate) : QString()},
        {QStringLiteral("snippet"), parsed.plain.simplified().left(160)},
        {QStringLiteral("bodyPlain"), parsed.plain},
        {QStringLiteral("bodyTrustMode"), QStringLiteral("plain")},
        {QStringLiteral("bodyTruncated"), truncated},
        {QStringLiteral("unread"), unread}, {QStringLiteral("flagged"), flagged},
        {QStringLiteral("attachments"), attachments},
        {QStringLiteral("remoteAccount"), job.accountId}, {QStringLiteral("remoteMailbox"), job.mailbox},
        {QStringLiteral("remoteUidValidity"), QString::number(uidValidity)},
        {QStringLiteral("remoteUid"), uid}, {QStringLiteral("remoteSize"), size}};
}

QVariantMap run(Kind kind, const Job &job)
{
    Protocol::Channel channel;
    channel.addSecret(job.credentials.secret);
    Imap::Session session(&channel);
    QVariantMap result;
    QString conflict;
    bool ok = false;
    qint64 validity = 0, exists = 0;
    if (session.open(job.endpoint, job.credentials) && session.select(job.mailbox, &validity, &exists)) {
        if (kind == Kind::Sync) {
            ok = syncMailbox(session, job, validity, exists, &result);
        } else if (validity != job.uidValidity) {
            conflict = QStringLiteral("The mailbox was rebuilt on the server (UIDVALIDITY changed); sync before retrying");
        } else if (kind == Kind::Action) {
            ok = runAction(session, job, channel);
        } else {
            ok = download(session, job, channel, &result);
        }
    }
    const Protocol::Failure failure = channel.failure();
    const QString error = channel.error();
    if (failure == Protocol::Failure::None) session.logout();
    const QString state = ok ? QStringLiteral("accepted")
        : !conflict.isEmpty() ? QStringLiteral("conflict")
        : Protocol::failureState(failure == Protocol::Failure::None ? Protocol::Failure::Rejected : failure);
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("state"), state);
    result.insert(QStringLiteral("error"), ok ? QString() : conflict.isEmpty() ? error : conflict);
    result.insert(QStringLiteral("retryable"), failure == Protocol::Failure::Offline);
    result.insert(QStringLiteral("transcript"), channel.transcript());
    return result;
}

} // namespace QindaTK::ImapJobs
