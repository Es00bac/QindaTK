// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// ImapTestServer with more than one mailbox, for the folder operations of
// ImapMailProvider: LIST (with RFC 6154 RETURN (SPECIAL-USE)), STATUS,
// APPEND with a synchronizing literal (and APPENDUID under UIDPLUS), SELECT
// of any mailbox and UID MOVE/COPY between mailboxes (COPYUID).
// Mailboxes are keyed by their wire (modified UTF-7) name. SELECT swaps the
// selected folder into the base class's `inbox` list, so FETCH/STORE/EXPUNGE
// of the base work on whatever is selected; read folders through contents().
// AGENT-CONTRACT (imap_folders.cpp): LIST replies are
// `* LIST (<attrs>) "/" <quoted-or-literal name>`, STATUS replies
// `* STATUS <name> (MESSAGES n UNSEEN m)`.

#include "mail_test_servers.h"

class ImapFolderTestServer : public ImapTestServer {
public:
    struct Folder {
        QByteArray wire;
        QStringList attributes;           // e.g. \HasNoChildren, \Noselect
        QString specialUse;               // e.g. \Sent; see specialUseOnlyWhenAsked
        QList<Message> messages;
        qint64 uidValidity = 5000;
        qint64 nextUid = 1;
        bool statusFails = false;         // STATUS answers NO
        bool literalName = false;         // LIST sends the name as a {n} literal
    };
    QList<Folder> folders;                // listed after INBOX, in this order
    // RFC 6154 servers mark roles only for LIST ... RETURN (SPECIAL-USE);
    // false models Gmail, which marks them in every LIST.
    bool specialUseOnlyWhenAsked = true;

    Folder &addFolder(const QByteArray &wire, const QString &specialUse = {}, const QStringList &attributes = {})
    {
        Folder folder;
        folder.wire = wire;
        folder.specialUse = specialUse;
        folder.attributes = attributes.isEmpty() ? QStringList{QStringLiteral("\\HasNoChildren")} : attributes;
        folders.append(folder);
        return folders.last();
    }
    Folder *folder(const QByteArray &wire)
    {
        for (Folder &f : folders) if (f.wire == wire) return &f;
        return nullptr;
    }
    // Messages of a mailbox, wherever the selection swap currently keeps them.
    QList<Message> &contents(const QByteArray &wire)
    {
        if (wire == m_selected) return inbox;
        if (wire == "INBOX") return m_inboxStash;
        return folder(wire)->messages;
    }

protected:
    void line(Connection &c, const QByteArray &text) override
    {
        if (c.state == 10) { literalLine(c, text); return; }
        const qsizetype space = text.indexOf(' ');
        const QByteArray tag = text.left(space);
        const QByteArray rest = space < 0 ? QByteArray() : text.mid(space + 1);
        const QByteArray upper = rest.toUpper();
        if (!c.authenticated || !(upper.startsWith("LIST ") || upper.startsWith("STATUS ") || upper.startsWith("APPEND ")
                                  || upper.startsWith("SELECT ") || upper.startsWith("UID MOVE ")
                                  || upper.startsWith("UID COPY "))) {
            ImapTestServer::line(c, text);
            return;
        }
        if (upper.startsWith("LIST ")) return list(c, tag, upper.contains("RETURN (SPECIAL-USE)"));
        const QByteArray name = argument(rest.mid(rest.indexOf(' ') + 1));
        if (upper.startsWith("SELECT ")) {
            if (name != "INBOX" && !folder(name)) return send(c, tag + " NO Mailbox does not exist\r\n");
            select(name);
            ImapTestServer::line(c, tag + " SELECT \"" + name + '"');
        } else if (upper.startsWith("STATUS ")) {
            Folder *f = folder(name);
            if ((name != "INBOX" && !f) || (f && f->statusFails)) return send(c, tag + " NO STATUS failed\r\n");
            const QList<Message> &messages = contents(name);
            int unseen = 0;
            for (const Message &m : messages) unseen += !m.flags.contains(QStringLiteral("\\Seen"));
            send(c, "* STATUS \"" + name + "\" (MESSAGES " + QByteArray::number(messages.size()) + " UNSEEN "
                    + QByteArray::number(unseen) + ")\r\n" + tag + " OK STATUS completed\r\n");
        } else if (upper.startsWith("APPEND ")) {
            startAppend(c, tag, name, rest);
        } else {
            moveOrCopy(c, tag, upper.startsWith("UID MOVE "), rest);
        }
    }

private:
    QByteArray m_selected = "INBOX";
    QList<Message> m_inboxStash;
    QByteArray m_appendTag, m_appendTarget;
    QStringList m_appendFlags;
    qint64 m_remaining = 0;

    // First argument: a quoted string (unescaped) or an atom.
    static QByteArray argument(const QByteArray &text)
    {
        if (!text.startsWith('"')) return text.left(text.indexOf(' ') < 0 ? text.size() : text.indexOf(' '));
        QByteArray out;
        for (qsizetype i = 1; i < text.size(); ++i) {
            if (text.at(i) == '\\' && i + 1 < text.size()) { out += text.at(++i); continue; }
            if (text.at(i) == '"') break;
            out += text.at(i);
        }
        return out.compare("INBOX", Qt::CaseInsensitive) == 0 ? QByteArray("INBOX") : out;
    }

    void select(const QByteArray &name)
    {
        if (name == m_selected) return;
        qint64 &storedValidity = m_selected == "INBOX" ? m_inboxValidity : folder(m_selected)->uidValidity;
        storedValidity = uidValidity;
        (m_selected == "INBOX" ? m_inboxStash : folder(m_selected)->messages) = inbox;
        inbox = name == "INBOX" ? m_inboxStash : folder(name)->messages;
        uidValidity = name == "INBOX" ? m_inboxValidity : folder(name)->uidValidity;
        m_selected = name;
    }
    qint64 m_inboxValidity = 1000;

    void list(Connection &c, const QByteArray &tag, bool returnSpecialUse)
    {
        QByteArray out = "* LIST (\\HasNoChildren) \"/\" INBOX\r\n";
        for (const Folder &f : std::as_const(folders)) {
            QStringList attributes = f.attributes;
            if (!f.specialUse.isEmpty() && (returnSpecialUse || !specialUseOnlyWhenAsked)) attributes.append(f.specialUse);
            out += "* LIST (" + attributes.join(QLatin1Char(' ')).toLatin1() + ") \"/\" "
                   + (f.literalName ? literal(f.wire) : "\"" + f.wire + "\"") + "\r\n";
        }
        send(c, out + tag + " OK LIST completed\r\n");
    }

    void startAppend(Connection &c, const QByteArray &tag, const QByteArray &name, const QByteArray &rest)
    {
        if (name != "INBOX" && !folder(name)) return send(c, tag + " NO [TRYCREATE] Mailbox does not exist\r\n");
        const qsizetype open = rest.indexOf('('), close = rest.indexOf(')');
        m_appendFlags = open >= 0 && close > open
            ? QString::fromLatin1(rest.mid(open + 1, close - open - 1)).split(QLatin1Char(' '), Qt::SkipEmptyParts) : QStringList();
        m_remaining = rest.mid(rest.lastIndexOf('{') + 1).chopped(1).toLongLong();
        m_appendTag = tag;
        m_appendTarget = name;
        c.state = 10;
        c.data.clear();
        send(c, "+ Ready for literal data\r\n");
    }

    // The literal arrives through the line splitter: re-add the CRLFs it
    // consumed; the CRLF after the literal ends the command.
    void literalLine(Connection &c, const QByteArray &text)
    {
        if (m_remaining > 0 && text.size() + 2 <= m_remaining) {
            c.data += text + "\r\n";
            m_remaining -= text.size() + 2;
            return;
        }
        if (m_remaining > 0) c.data += text;
        m_remaining = 0;
        c.state = 0;
        Folder *f = folder(m_appendTarget);
        const qint64 uid = f ? f->nextUid++ : m_inboxNext++;
        contents(m_appendTarget).append({uid, c.data, m_appendFlags});
        const qint64 validity = m_appendTarget == m_selected ? uidValidity : f ? f->uidValidity : m_inboxValidity;
        const QByteArray code = capabilities.contains(QStringLiteral("UIDPLUS"))
            ? "[APPENDUID " + QByteArray::number(validity) + ' ' + QByteArray::number(uid) + "] " : QByteArray();
        send(c, m_appendTag + " OK " + code + "APPEND completed\r\n");
    }
    qint64 m_inboxNext = 900;

    void moveOrCopy(Connection &c, const QByteArray &tag, bool move, const QByteArray &rest)
    {
        const QList<QByteArray> parts = rest.split(' ');
        const qint64 uid = parts.value(2).toLongLong();
        const QByteArray target = argument(rest.mid(rest.indexOf(parts.value(2)) + parts.value(2).size() + 1));
        if (target == "Archive" && !folder(target)) {       // the base server's archive list
            ImapTestServer::line(c, tag + ' ' + rest);
            return;
        }
        Folder *f = folder(target);
        if (!f) return send(c, tag + " NO [TRYCREATE] Mailbox does not exist\r\n");
        Message *message = find(uid);
        if (!message) return send(c, tag + " OK No messages\r\n");
        Message copy = *message;
        copy.uid = f->nextUid++;
        copy.flags.removeAll(QStringLiteral("\\Deleted"));
        f->messages.append(copy);
        const QByteArray code = "[COPYUID " + QByteArray::number(f->uidValidity) + ' ' + QByteArray::number(uid) + ' '
            + QByteArray::number(copy.uid) + ']';
        if (!move) return send(c, tag + " OK " + code + " COPY completed\r\n");
        send(c, "* OK " + code + "\r\n");
        message->flags.append(QStringLiteral("\\Deleted"));
        expunge(c, uid);
        send(c, tag + " OK MOVE completed\r\n");
    }
};
