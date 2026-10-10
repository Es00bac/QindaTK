// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_messages.h"

#include "imap_keywords.h"

#include <QRegularExpression>
#include <algorithm>

namespace QindaTK::ImapMessages {
namespace {
QList<QList<qint64>> chunks(const QList<qint64> &uids)
{
    QList<QList<qint64>> out;
    for (qsizetype i = 0; i < uids.size(); i += ChunkUids) out.append(uids.mid(i, ChunkUids));
    return out;
}

qint64 uidOf(const Imap::FetchItem &item)
{
    const Imap::Value *uid = item.find("UID");
    return uid ? uid->text.toLongLong() : 0;
}

QByteArray flagList(const QStringList &flags) { return '(' + flags.join(QLatin1Char(' ')).toLatin1() + ')'; }
} // namespace

QByteArray uidSet(const QList<qint64> &uids)
{
    QList<qint64> sorted = uids;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    QByteArray out;
    for (qsizetype i = 0; i < sorted.size();) {
        qsizetype j = i;
        while (j + 1 < sorted.size() && sorted.at(j + 1) == sorted.at(j) + 1) ++j;
        if (!out.isEmpty()) out += ',';
        out += QByteArray::number(sorted.at(i));
        if (j > i) out += ':' + QByteArray::number(sorted.at(j));
        i = j + 1;
    }
    return out;
}

QList<qint64> expandUidSet(const QString &set, int limit)
{
    QList<qint64> out;
    for (const QString &part : set.split(QLatin1Char(','))) {
        const QStringList ends = part.split(QLatin1Char(':'));
        if (ends.isEmpty() || ends.size() > 2) return {};
        bool okLow = false, okHigh = true;
        qint64 low = ends.first().toLongLong(&okLow);
        qint64 high = ends.size() == 2 ? ends.last().toLongLong(&okHigh) : low;
        if (!okLow || !okHigh || low <= 0 || high <= 0) return {};
        if (low > high) std::swap(low, high);
        if (high - low >= limit || out.size() + (high - low + 1) > limit) return {};
        for (qint64 uid = low; uid <= high; ++uid) out.append(uid);
    }
    return out;
}

QStringList flagsOf(const Imap::Value *flags)
{
    QStringList out;
    if (!flags || flags->kind != Imap::Value::List) return out;
    for (const Imap::Value &flag : flags->items) out.append(QString::fromUtf8(flag.text));
    return out;
}

bool fetchFlags(Imap::Session &session, const QList<qint64> &uids, QMap<qint64, QStringList> *flags)
{
    for (const QList<qint64> &chunk : chunks(uids)) {
        QList<QByteArray> untagged;
        if (!session.command("UID FETCH " + uidSet(chunk) + " (UID FLAGS)", &untagged)) return false;
        for (const Imap::FetchItem &item : Imap::fetchItems(untagged)) {
            const qint64 uid = uidOf(item);
            if (uid > 0 && chunk.contains(uid)) flags->insert(uid, flagsOf(item.find("FLAGS")));
        }
    }
    return true;
}

bool storeFlags(Imap::Session &session, const QList<qint64> &uids, const QStringList &add, const QStringList &remove)
{
    for (const QList<qint64> &chunk : chunks(uids)) {
        if (!add.isEmpty() && !session.command("UID STORE " + uidSet(chunk) + " +FLAGS.SILENT " + flagList(add)))
            return false;
        if (!remove.isEmpty() && !session.command("UID STORE " + uidSet(chunk) + " -FLAGS.SILENT " + flagList(remove)))
            return false;
    }
    return true;
}

bool validHeaderField(const QString &name)
{
    if (name.isEmpty() || name.size() > 128) return false;
    for (const QChar c : name)
        if (c == u':' || !ImapKeywords::isAtomChar(c)) return false;     // RFC 5322 ftext that is also an atom
    return true;
}

bool fetchHeaders(Imap::Session &session, const QList<qint64> &uids, const QStringList &fields,
                  QMap<qint64, QByteArray> *headers)
{
    const QByteArray section = fields.isEmpty() ? QByteArrayLiteral("HEADER")
        : "HEADER.FIELDS (" + fields.join(QLatin1Char(' ')).toLatin1().toUpper() + ')';
    for (const QList<qint64> &chunk : chunks(uids)) {
        QList<QByteArray> untagged;
        if (!session.command("UID FETCH " + uidSet(chunk) + " (UID BODY.PEEK[" + section + "])", &untagged)) return false;
        for (const Imap::FetchItem &item : Imap::fetchItems(untagged)) {
            const qint64 uid = uidOf(item);
            const Imap::Value *block = item.find("BODY[");
            if (uid > 0 && chunk.contains(uid) && block && block->kind != Imap::Value::Nil)
                headers->insert(uid, block->text);
        }
    }
    return true;
}

bool expunge(Imap::Session &session, const QList<qint64> &uids)
{
    for (const QList<qint64> &chunk : chunks(uids)) {
        const QByteArray set = uidSet(chunk);
        if (!session.command("UID STORE " + set + " +FLAGS.SILENT (\\Deleted)")) return false;
        // AGENT-NOTE: without UIDPLUS a plain EXPUNGE also removes any other
        // message already marked \Deleted in this mailbox (RFC 3501 §6.4.3).
        if (!(session.hasCapability("UIDPLUS") ? session.command("UID EXPUNGE " + set) : session.command("EXPUNGE")))
            return false;
    }
    return true;
}

bool transfer(Imap::Session &session, const QList<qint64> &uids, const QByteArray &target, bool move,
              QMap<qint64, qint64> *targets, QString *targetValidity)
{
    // AGENT-NOTE: MOVE (RFC 6851) is atomic; without it COPY + \Deleted +
    // (UID) EXPUNGE is the RFC 3501 equivalent.
    const bool atomicMove = move && session.hasCapability("MOVE");
    for (const QList<qint64> &chunk : chunks(uids)) {
        QList<QByteArray> untagged;
        if (!session.command((atomicMove ? "UID MOVE " : "UID COPY ") + uidSet(chunk) + ' ' + Imap::quoted(target), &untagged))
            return false;
        readCopyUids(untagged, session.lastStatus(), targets, targetValidity);
        if (move && !atomicMove && !expunge(session, chunk)) return false;
    }
    return true;
}

void readCopyUids(const QList<QByteArray> &untagged, const QString &status, QMap<qint64, qint64> *targets,
                  QString *targetValidity)
{
    static const QRegularExpression copyUid(QStringLiteral("\\[COPYUID (\\d+) ([0-9:,]+) ([0-9:,]+)\\]"),
                                            QRegularExpression::CaseInsensitiveOption);
    QStringList texts{status};
    for (const QByteArray &line : untagged) texts.append(QString::fromUtf8(line));
    for (const QString &text : std::as_const(texts)) {
        const QRegularExpressionMatch match = copyUid.match(text);
        if (!match.hasMatch()) continue;
        const QList<qint64> from = expandUidSet(match.captured(2), 100000);
        const QList<qint64> to = expandUidSet(match.captured(3), 100000);
        if (from.isEmpty() || from.size() != to.size()) continue;      // RFC 4315: the sets correspond
        for (qsizetype i = 0; i < from.size(); ++i) targets->insert(from.at(i), to.at(i));
        *targetValidity = match.captured(1);
    }
}

} // namespace QindaTK::ImapMessages
