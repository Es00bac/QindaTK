// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_mail_jobs.h"

#include "imap_folders.h"
#include "imap_keywords.h"
#include "imap_messages.h"
#include "imap_mutf7.h"
#include "imap_session.h"
#include "mail_mime.h"
#include "mail_record.h"

#include <QRegularExpression>
#include <QHash>
#include <algorithm>

namespace QindaTK::ImapJobs {
namespace {
QByteArray number(qint64 value) { return QByteArray::number(value); }

// One sync's message bodies per FETCH command (D-353); 16 MiB keeps a batch
// well inside Imap::Session's 64 MiB response cap.
constexpr int BatchMessages = 25;
constexpr qint64 BatchBytes = 16LL * 1024 * 1024;

qint64 uidOf(const Imap::FetchItem &item)
{
    const Imap::Value *uid = item.find("UID");
    return uid ? uid->text.toLongLong() : 0;
}

QStringList sorted(QStringList list)
{
    list.sort(Qt::CaseInsensitive);
    return list;
}

// True when the server's flags say something the stored record does not.
bool changed(const Job &job, const KnownMessage &local, const QVariantMap &fields)
{
    return local.unread != fields.value(QStringLiteral("unread")).toBool()
        || local.flagged != fields.value(QStringLiteral("flagged")).toBool()
        || local.answered != fields.value(QStringLiteral("answered")).toBool()
        || sorted(local.keywords) != fields.value(QStringLiteral("keywords")).toStringList()
        || (job.keywordCategories
            && sorted(local.categories) != sorted(fields.value(QStringLiteral("categories")).toStringList()));
}

void reportMailbox(const Imap::SelectInfo &info, QVariantMap *out)
{
    out->insert(QStringLiteral("mailboxFlags"), info.flags);
    out->insert(QStringLiteral("permanentFlags"), info.permanentFlags);
    out->insert(QStringLiteral("permanentFlagsSent"), info.permanentFlagsSent);
    // RFC 3501 §7.1: "\*" means new keywords can be created and kept.
    out->insert(QStringLiteral("allowsKeywords"),
                !info.permanentFlagsSent || info.permanentFlags.contains(QStringLiteral("\\*")));
    out->insert(QStringLiteral("readOnly"), info.readOnly);
}

bool syncMailbox(Imap::Session &session, const Job &job, const Imap::SelectInfo &info, QVariantMap *out)
{
    const qint64 validity = info.uidValidity, exists = info.exists;
    const bool reset = job.knownUidValidity != 0 && job.knownUidValidity != validity;
    qint64 lastUid = 0;
    if (!reset)
        for (auto it = job.known.cbegin(); it != job.known.cend(); ++it) lastUid = std::max(lastUid, it.key());
    QVariantList updated, removed, added;
    if (!reset && !job.known.isEmpty()) {
        // AGENT-NOTE: only the messages the store holds are asked about. A
        // 1:<last UID> range made the server report the flags of the whole
        // mailbox on every sync: about a minute and megabytes for a Gmail
        // inbox of 158 000 messages that keeps the newest 200 (D-353).
        QMap<qint64, QStringList> flags;
        if (exists > 0 && !ImapMessages::fetchFlags(session, job.known.keys(), &flags)) return false;
        for (auto it = flags.cbegin(); it != flags.cend(); ++it) {
            QVariantMap fields = flagFields(job, it.value());
            if (!changed(job, job.known.value(it.key()), fields)) continue;
            fields.insert(QStringLiteral("uid"), it.key());
            updated.append(fields);
        }
        for (auto it = job.known.cbegin(); it != job.known.cend(); ++it)
            if (!flags.contains(it.key())) removed.append(it.key());
    }
    if (exists > 0) {
        // AGENT-NOTE: a first sync (or one after a UIDVALIDITY reset) of a
        // mailbox larger than the fetch limit lists only its newest
        // `fetchLimit` messages, by sequence number, instead of the size of
        // every message in it (D-353).
        const bool window = lastUid == 0 && job.fetchLimit > 0 && exists > job.fetchLimit;
        const QByteArray listing = window ? "FETCH " + number(exists - job.fetchLimit + 1) + ":* (UID RFC822.SIZE)"
                                          : "UID FETCH " + number(lastUid + 1) + ":* (UID RFC822.SIZE)";
        QList<QByteArray> untagged;
        if (!session.command(listing, &untagged)) return false;
        QList<QPair<qint64, qint64>> fresh;
        for (const Imap::FetchItem &item : Imap::fetchItems(untagged)) {
            const qint64 uid = uidOf(item);
            const Imap::Value *size = item.find("RFC822.SIZE");
            if (uid > lastUid) fresh.append({uid, size ? size->text.toLongLong() : 0});
        }
        std::sort(fresh.begin(), fresh.end());
        fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());
        if (job.fetchLimit > 0 && fresh.size() > job.fetchLimit) fresh = fresh.mid(fresh.size() - job.fetchLimit);
        // Bodies in batches of up to BatchMessages / BatchBytes (a message
        // over the size cap brings only its header): one round trip per
        // message made a first sync of 200 take most of a minute (D-353).
        for (qsizetype i = 0; i < fresh.size();) {
            const bool truncated = fresh.at(i).second > job.maximumMessageBytes;
            QList<qint64> batch;
            qint64 bytes = 0;
            qsizetype j = i;
            for (; j < fresh.size() && batch.size() < BatchMessages; ++j) {
                const qint64 size = fresh.at(j).second;
                if ((size > job.maximumMessageBytes) != truncated) break;
                if (!truncated && !batch.isEmpty() && bytes + size > BatchBytes) break;
                batch.append(fresh.at(j).first);
                if (!truncated) bytes += size;
            }
            const QByteArray section = truncated ? "BODY.PEEK[HEADER]" : "BODY.PEEK[]";
            QList<QByteArray> messages;
            if (!session.command("UID FETCH " + ImapMessages::uidSet(batch) + " (UID FLAGS " + section + ")", &messages))
                return false;
            QHash<qint64, Imap::FetchItem> items;
            for (const Imap::FetchItem &item : Imap::fetchItems(messages)) items.insert(uidOf(item), item);
            for (; i < j; ++i) {
                const auto &[uid, size] = fresh.at(i);
                const auto item = items.constFind(uid);
                if (item == items.cend()) continue;               // expunged meanwhile
                const Imap::Value *body = item->find("BODY[");
                added.append(messageRecord(job, validity, uid, ImapMessages::flagsOf(item->find("FLAGS")), size,
                                           body ? body->text : QByteArray(), truncated));
            }
        }
    }
    out->insert(QStringLiteral("uidValidity"), QString::number(validity));
    out->insert(QStringLiteral("reset"), reset);
    out->insert(QStringLiteral("exists"), exists);
    out->insert(QStringLiteral("added"), added);
    out->insert(QStringLiteral("updated"), updated);
    out->insert(QStringLiteral("removedUids"), removed);
    reportMailbox(info, out);
    return true;
}

bool runAction(Imap::Session &session, const Job &job, Protocol::Channel &channel, QVariantMap *out)
{
    const QByteArray uid = number(job.uid);
    if (job.action == QLatin1String("read")) return session.command("UID STORE " + uid + " +FLAGS.SILENT (\\Seen)");
    if (job.action == QLatin1String("unread")) return session.command("UID STORE " + uid + " -FLAGS.SILENT (\\Seen)");
    if (job.action == QLatin1String("flag")) return session.command("UID STORE " + uid + " +FLAGS.SILENT (\\Flagged)");
    if (job.action == QLatin1String("unflag")) return session.command("UID STORE " + uid + " -FLAGS.SILENT (\\Flagged)");
    if (job.action == QLatin1String("delete")) return ImapMessages::expunge(session, {job.uid});
    if (job.action == QLatin1String("archive") || job.action == QLatin1String("move") || job.action == QLatin1String("copy")) {
        QMap<qint64, qint64> targets;
        QString validity;
        if (!ImapMessages::transfer(session, {job.uid}, wireName(job, job.targetMailbox), job.action != QLatin1String("copy"), &targets, &validity))
            return false;
        if (targets.contains(job.uid)) {
            out->insert(QStringLiteral("targetUidValidity"), validity);
            out->insert(QStringLiteral("targetUid"), targets.value(job.uid));
        }
        return true;
    }
    channel.fail(Protocol::Failure::Rejected, QStringLiteral("Unsupported mail action %1").arg(job.action));
    return false;
}

// STORE of flags or of a category set on one message, honouring
// PERMANENTFLAGS, then the flags as the server holds them afterwards.
bool runFlags(Imap::Session &session, const Job &job, const Imap::SelectInfo &info, Protocol::Channel &channel,
              QVariantMap *out)
{
    QMap<qint64, QStringList> before;
    if (!ImapMessages::fetchFlags(session, {job.uid}, &before)) return false;
    if (!before.contains(job.uid)) {
        channel.fail(Protocol::Failure::Rejected, QStringLiteral("The message is no longer in the mailbox; sync first"));
        return false;
    }
    QStringList add, remove;
    if (job.setCategories) {
        QStringList wanted;
        for (const QString &name : job.categories) {
            const QString keyword = ImapKeywords::keywordForCategory(name);
            if (!keyword.isEmpty() && !ImapKeywords::containsFlag(wanted, keyword)) wanted.append(keyword);
        }
        for (const QString &keyword : wanted)
            if (!ImapKeywords::containsFlag(before.value(job.uid), keyword)) add.append(keyword);
        for (const QString &flag : before.value(job.uid))
            if (ImapKeywords::isCategoryKeyword(flag) && !ImapKeywords::containsFlag(wanted, flag)) remove.append(flag);
    } else {
        for (const QString &flag : job.addFlags) add.append(ImapKeywords::canonicalFlag(flag));
        for (const QString &flag : job.removeFlags) remove.append(ImapKeywords::canonicalFlag(flag));
    }
    if (info.readOnly) {
        channel.fail(Protocol::Failure::Rejected, QStringLiteral("The selected mailbox is read-only"));
        return false;
    }
    QStringList notPermanent;
    for (const QString &flag : add + remove)
        if (!ImapKeywords::isPermanent(flag, info.permanentFlags, info.permanentFlagsSent)) notPermanent.append(flag);
    reportMailbox(info, out);
    if (!notPermanent.isEmpty()) {
        // AGENT-GUARD: a flag outside PERMANENTFLAGS would only last for this
        // session (RFC 3501 §7.1); storing it would report a change that is
        // gone at the next sync. Nothing is stored.
        out->insert(QStringLiteral("notPermanent"), notPermanent);
        channel.fail(Protocol::Failure::Rejected,
                     QStringLiteral("The server does not keep %1 permanently in this mailbox").arg(notPermanent.join(QStringLiteral(", "))));
        return false;
    }
    if ((!add.isEmpty() || !remove.isEmpty()) && !ImapMessages::storeFlags(session, {job.uid}, add, remove)) return false;
    QMap<qint64, QStringList> after;
    if (!ImapMessages::fetchFlags(session, {job.uid}, &after)) return false;
    const QStringList flags = after.value(job.uid, before.value(job.uid));
    out->insert(flagFields(job, flags));
    out->insert(QStringLiteral("flags"), flags);
    out->insert(QStringLiteral("addedFlags"), add);
    out->insert(QStringLiteral("removedFlags"), remove);
    return true;
}

QVariantMap uidKeyed(const QMap<qint64, QByteArray> &values)
{
    QVariantMap out;
    for (auto it = values.cbegin(); it != values.cend(); ++it) out.insert(QString::number(it.key()), it.value());
    return out;
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

bool runSelected(Kind kind, Imap::Session &session, const Job &job, const Imap::SelectInfo &info,
                 Protocol::Channel &channel, QVariantMap *out)
{
    switch (kind) {
    case Kind::Action: return runAction(session, job, channel, out);
    case Kind::Attachment: return download(session, job, channel, out);
    case Kind::Flags: return runFlags(session, job, info, channel, out);
    case Kind::Headers: {
        QMap<qint64, QByteArray> headers;
        if (!ImapMessages::fetchHeaders(session, job.uids, job.headerFields, &headers)) return false;
        out->insert(QStringLiteral("headers"), uidKeyed(headers));
        return true;
    }
    case Kind::Transfer: {
        QMap<qint64, qint64> targets;
        QString validity;
        if (!ImapMessages::transfer(session, job.uids, wireName(job, job.targetMailbox), job.move, &targets, &validity))
            return false;
        QVariantMap mapped;
        for (auto it = targets.cbegin(); it != targets.cend(); ++it) mapped.insert(QString::number(it.key()), it.value());
        out->insert(QStringLiteral("targetUids"), mapped);
        if (!validity.isEmpty()) out->insert(QStringLiteral("targetUidValidity"), validity);
        return true;
    }
    default: break;
    }
    return false;
}
} // namespace

QString recordId(const QString &accountId, const QString &mailbox, qint64 uidValidity, qint64 uid)
{
    return QStringLiteral("imap:%1:%2:%3:%4").arg(accountId, mailbox).arg(uidValidity).arg(uid);
}

QByteArray wireName(const Job &job, const QString &mailbox)
{
    const auto known = job.wireNames.constFind(mailbox);
    return known != job.wireNames.cend() ? *known : Imap::encodeMailboxName(mailbox);
}

QVariantMap flagFields(const Job &job, const QStringList &flags)
{
    QVariantMap fields{{QStringLiteral("unread"), !ImapKeywords::containsFlag(flags, QStringLiteral("\\Seen"))},
                       {QStringLiteral("flagged"), ImapKeywords::containsFlag(flags, QStringLiteral("\\Flagged"))},
                       {QStringLiteral("answered"), ImapKeywords::containsFlag(flags, QStringLiteral("\\Answered"))},
                       {QStringLiteral("keywords"), sorted(ImapKeywords::keywordsOf(flags))}};
    if (job.keywordCategories) fields.insert(QStringLiteral("categories"), ImapKeywords::categoriesOf(flags));
    return fields;
}

QVariantMap messageRecord(const Job &job, qint64 uidValidity, qint64 uid, const QStringList &flags,
                          qint64 size, const QByteArray &source, bool truncated)
{
    MailRecord::Options options;
    options.keepHtml = job.keepHtml;
    QVariantMap record = MailRecord::fromParsed(recordId(job.accountId, job.mailbox, uidValidity, uid),
                                                Mime::parse(source), truncated, options);
    record.insert(flagFields(job, flags));
    record.insert(QVariantMap{{QStringLiteral("remoteAccount"), job.accountId},
                              {QStringLiteral("remoteMailbox"), job.mailbox},
                              {QStringLiteral("remoteUidValidity"), QString::number(uidValidity)},
                              {QStringLiteral("remoteUid"), uid}, {QStringLiteral("remoteSize"), size}});
    return record;
}

QVariantMap messageRecord(const Job &job, qint64 uidValidity, qint64 uid, bool unread, bool flagged,
                          qint64 size, const QByteArray &source, bool truncated)
{
    QStringList flags;
    if (!unread) flags.append(QStringLiteral("\\Seen"));
    if (flagged) flags.append(QStringLiteral("\\Flagged"));
    return messageRecord(job, uidValidity, uid, flags, size, source, truncated);
}

QVariantMap run(Kind kind, const Job &job)
{
    Protocol::Channel channel;
    channel.addSecret(job.credentials.secret);
    Imap::Session session(&channel);
    QVariantMap result;
    QString conflict;
    bool ok = false;
    Imap::SelectInfo info;
    if (!session.open(job.endpoint, job.credentials)) {
        // failure recorded on the channel
    } else if (kind == Kind::Mailboxes) {
        ok = ImapFolders::listMailboxes(session, job.withCounts, &result);
    } else if (kind == Kind::Append) {
        ok = ImapFolders::append(session, job.targetMailbox, job.appendMessage, job.appendFlags, &result,
                                 job.wireNames.value(job.targetMailbox));
    } else if (session.select(job.mailbox, &info, job.wireNames.value(job.mailbox))) {
        if (kind == Kind::Sync) {
            ok = syncMailbox(session, job, info, &result);
        } else if (info.uidValidity != job.uidValidity) {
            conflict = QStringLiteral("The mailbox was rebuilt on the server (UIDVALIDITY changed); sync before retrying");
        } else {
            ok = runSelected(kind, session, job, info, channel, &result);
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
