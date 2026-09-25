// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_folders.h"

#include "imap_mutf7.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace QindaTK::ImapFolders {
namespace {
const QHash<QString, QString> &attributeRoles()
{
    static const QHash<QString, QString> roles{
        {QStringLiteral("\\sent"), QStringLiteral("sent")}, {QStringLiteral("\\drafts"), QStringLiteral("drafts")},
        {QStringLiteral("\\trash"), QStringLiteral("trash")}, {QStringLiteral("\\junk"), QStringLiteral("junk")},
        {QStringLiteral("\\archive"), QStringLiteral("archive")}, {QStringLiteral("\\all"), QStringLiteral("all")},
        {QStringLiteral("\\flagged"), QStringLiteral("flagged")}};
    return roles;
}

// AGENT-NOTE: servers without RFC 6154 (older Exchange, many hosted
// Dovecot setups) mark nothing; these names are what their clients create.
// A heuristic never overrides a role some mailbox claims by attribute.
const QHash<QString, QString> &nameRoles()
{
    static const QHash<QString, QString> roles{
        {QStringLiteral("sent"), QStringLiteral("sent")}, {QStringLiteral("sent items"), QStringLiteral("sent")},
        {QStringLiteral("sent mail"), QStringLiteral("sent")}, {QStringLiteral("sent messages"), QStringLiteral("sent")},
        {QStringLiteral("drafts"), QStringLiteral("drafts")}, {QStringLiteral("draft"), QStringLiteral("drafts")},
        {QStringLiteral("trash"), QStringLiteral("trash")}, {QStringLiteral("deleted items"), QStringLiteral("trash")},
        {QStringLiteral("deleted messages"), QStringLiteral("trash")}, {QStringLiteral("bin"), QStringLiteral("trash")},
        {QStringLiteral("junk"), QStringLiteral("junk")}, {QStringLiteral("spam"), QStringLiteral("junk")},
        {QStringLiteral("junk e-mail"), QStringLiteral("junk")}, {QStringLiteral("junk email"), QStringLiteral("junk")},
        {QStringLiteral("bulk mail"), QStringLiteral("junk")}, {QStringLiteral("archive"), QStringLiteral("archive")},
        {QStringLiteral("archives"), QStringLiteral("archive")}, {QStringLiteral("all mail"), QStringLiteral("all")},
        {QStringLiteral("starred"), QStringLiteral("flagged")}};
    return roles;
}

bool safeWireName(const QByteArray &raw)
{
    return !raw.isEmpty() && !raw.contains('\r') && !raw.contains('\n') && !raw.contains('\0');
}

qint64 statusNumber(const Imap::Value &list, const char *name)
{
    for (qsizetype i = 0; i + 1 < list.items.size(); i += 2)
        if (list.items.at(i).isAtom(name)) return list.items.at(i + 1).text.toLongLong();
    return -1;
}
} // namespace

QString specialUseOf(const QStringList &flags)
{
    for (const QString &flag : flags) {
        const QString role = attributeRoles().value(flag.toLower());
        if (!role.isEmpty()) return role;
    }
    return {};
}

QVariantList parseList(const QList<QByteArray> &untagged)
{
    QVariantList inbox, others;
    QSet<QByteArray> seen;
    for (const QByteArray &raw : untagged) {
        const QList<Imap::Value> values = Imap::parseResponse(raw);
        if (values.size() < 4 || !values.at(0).isAtom("LIST") || values.at(1).kind != Imap::Value::List) continue;
        QByteArray wire = values.at(3).text;
        const bool isInbox = wire.compare("INBOX", Qt::CaseInsensitive) == 0;
        if (isInbox) wire = "INBOX";                    // RFC 3501 §5.1: INBOX is case-insensitive
        if (values.at(3).kind == Imap::Value::Nil || wire.isEmpty() || seen.contains(wire)) continue;
        if (inbox.size() + others.size() >= MaximumListed) break;
        seen.insert(wire);
        QStringList flags;
        bool selectable = true;
        for (const Imap::Value &flag : values.at(1).items) {
            flags.append(QString::fromUtf8(flag.text));
            if (flag.isAtom("\\Noselect") || flag.isAtom("\\NonExistent")) selectable = false;
        }
        const QString delimiter = values.at(2).kind == Imap::Value::Nil ? QString() : QString::fromUtf8(values.at(2).text);
        QString role = specialUseOf(flags);
        if (isInbox) role = QStringLiteral("inbox");
        const QVariantMap entry{{QStringLiteral("name"), Imap::decodeMailboxName(wire)},
                                {QStringLiteral("rawName"), QString::fromUtf8(wire)},
                                {QStringLiteral("delimiter"), delimiter}, {QStringLiteral("flags"), flags},
                                {QStringLiteral("specialUse"), role},
                                {QStringLiteral("selectable"), selectable && safeWireName(wire)},
                                {QStringLiteral("unread"), -1}, {QStringLiteral("total"), -1}};
        (isInbox ? inbox : others).append(entry);
    }
    return inbox + others;
}

void applyNameHeuristics(QVariantList *mailboxes)
{
    QSet<QString> claimed;
    for (const QVariant &value : std::as_const(*mailboxes))
        claimed.insert(value.toMap().value(QStringLiteral("specialUse")).toString());
    for (QVariant &value : *mailboxes) {
        QVariantMap entry = value.toMap();
        if (!entry.value(QStringLiteral("specialUse")).toString().isEmpty()
            || !entry.value(QStringLiteral("selectable")).toBool())
            continue;
        const QString delimiter = entry.value(QStringLiteral("delimiter")).toString();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString leaf = delimiter.isEmpty() ? name : name.section(delimiter, -1);
        const QString role = nameRoles().value(leaf.trimmed().toLower());
        if (role.isEmpty() || claimed.contains(role)) continue;
        claimed.insert(role);
        entry.insert(QStringLiteral("specialUse"), role);
        value = entry;
    }
}

bool listMailboxes(Imap::Session &session, bool withCounts, QVariantMap *out)
{
    // AGENT-NOTE: RFC 6154 servers need RETURN (SPECIAL-USE) to mark roles;
    // Gmail and others mark them in a plain LIST, which the parser reads the
    // same way. The "(SPECIAL-USE)" selection option is not used: it would
    // return only the special mailboxes.
    const bool specialUse = session.hasCapability("SPECIAL-USE");
    QList<QByteArray> untagged;
    if (!session.command(specialUse ? QByteArrayLiteral("LIST \"\" \"*\" RETURN (SPECIAL-USE)")
                                    : QByteArrayLiteral("LIST \"\" \"*\""), &untagged))
        return false;
    QVariantList mailboxes = parseList(untagged);
    applyNameHeuristics(&mailboxes);
    int counted = 0;
    bool truncated = false;
    if (withCounts) {
        for (QVariant &value : mailboxes) {
            QVariantMap entry = value.toMap();
            if (!entry.value(QStringLiteral("selectable")).toBool()) continue;
            if (counted >= MaximumCounted) { truncated = true; break; }
            ++counted;
            const QByteArray wire = entry.value(QStringLiteral("rawName")).toString().toUtf8();
            QList<QByteArray> status;
            // A mailbox deleted since LIST answers NO; the rest still count.
            if (!session.optionalCommand("STATUS " + Imap::quoted(wire) + " (MESSAGES UNSEEN)", &status)) {
                if (session.failed()) return false;
                continue;
            }
            for (const QByteArray &line : std::as_const(status)) {
                const QList<Imap::Value> values = Imap::parseResponse(line);
                if (values.size() < 3 || !values.at(0).isAtom("STATUS") || values.at(2).kind != Imap::Value::List) continue;
                const QByteArray name = values.at(1).text.compare("INBOX", Qt::CaseInsensitive) == 0 ? QByteArray("INBOX")
                                                                                                    : values.at(1).text;
                if (name != wire) continue;
                entry.insert(QStringLiteral("total"), statusNumber(values.at(2), "MESSAGES"));
                entry.insert(QStringLiteral("unread"), statusNumber(values.at(2), "UNSEEN"));
            }
            value = entry;
        }
    }
    out->insert(QStringLiteral("mailboxes"), mailboxes);
    out->insert(QStringLiteral("count"), mailboxes.size());
    out->insert(QStringLiteral("countedMailboxes"), counted);
    out->insert(QStringLiteral("countsTruncated"), truncated);
    out->insert(QStringLiteral("listTruncated"), mailboxes.size() >= MaximumListed);
    return true;
}

bool validFlag(const QString &flag)
{
    static const QRegularExpression pattern(QStringLiteral("^\\\\?[A-Za-z0-9$_.\\-]{1,64}$"));
    return pattern.match(flag).hasMatch() && flag != QLatin1String("\\Recent");
}

bool append(Imap::Session &session, const QString &mailbox, const QByteArray &message,
            const QStringList &flags, QVariantMap *out)
{
    QByteArray normalized;
    normalized.reserve(message.size() + message.count('\n'));
    for (qsizetype i = 0; i < message.size(); ++i) {
        const char c = message.at(i);
        if (c == '\n' && (i == 0 || message.at(i - 1) != '\r')) normalized += '\r';
        normalized += c;
    }
    QByteArray command = "APPEND " + Imap::mailboxArgument(mailbox);
    if (!flags.isEmpty()) command += " (" + flags.join(QLatin1Char(' ')).toLatin1() + ')';
    command += " {" + QByteArray::number(normalized.size()) + '}';
    // AGENT-NOTE: a synchronizing literal: the server's "+" continuation is
    // answered with the bytes; a tagged NO (e.g. [TRYCREATE]) ends it first.
    QList<QByteArray> untagged;
    if (!session.command(command, &untagged, {}, normalized,
                         QStringLiteral("{%1 message bytes}").arg(normalized.size())))
        return false;
    static const QRegularExpression appendUid(QStringLiteral("\\[APPENDUID (\\d+) (\\d+)\\]"),
                                              QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = appendUid.match(session.lastStatus());
    if (match.hasMatch()) {
        out->insert(QStringLiteral("appendUidValidity"), match.captured(1));
        out->insert(QStringLiteral("appendUid"), match.captured(2).toLongLong());
    }
    out->insert(QStringLiteral("size"), normalized.size());
    return true;
}

void readCopyUid(const QList<QByteArray> &untagged, const QString &status, qint64 uid, QVariantMap *out)
{
    static const QRegularExpression copyUid(QStringLiteral("\\[COPYUID (\\d+) ([0-9:,]+) ([0-9:,]+)\\]"),
                                            QRegularExpression::CaseInsensitiveOption);
    QStringList texts{status};
    for (const QByteArray &line : untagged) texts.append(QString::fromUtf8(line));
    for (const QString &text : std::as_const(texts)) {
        const QRegularExpressionMatch match = copyUid.match(text);
        // Only a single-UID reply maps unambiguously to the moved message.
        if (!match.hasMatch() || match.captured(2) != QString::number(uid)) continue;
        bool ok = false;
        const qint64 target = match.captured(3).toLongLong(&ok);
        if (!ok) continue;
        out->insert(QStringLiteral("targetUidValidity"), match.captured(1));
        out->insert(QStringLiteral("targetUid"), target);
        return;
    }
}

} // namespace QindaTK::ImapFolders
