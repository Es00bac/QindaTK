// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// In-process POP3 server for the Pop3MailProvider tests (and the POP3 cases
// of tst_provider_tls). It listens on 127.0.0.1 over real TCP, driven by the
// test's event loop, and speaks the subset the provider uses: CAPA, STLS,
// USER/PASS, AUTH PLAIN/XOAUTH2 (with and without an initial response),
// UIDL, LIST, RETR, TOP, DELE, RSET, NOOP and QUIT. DELE marks are per
// connection and are applied only on QUIT, as RFC 1939 §6 requires.
// AGENT-CONTRACT (pop3_session.cpp): replies follow RFC 1939 framing —
// "+OK"/"-ERR" status lines, multi-line bodies byte-stuffed and ended by ".".

#include "mail_test_servers.h"

#include <QHash>
#include <QSet>
#include <functional>

class Pop3TestServer : public LineServer {
public:
    struct Message {
        QByteArray uidl;
        QByteArray source;          // CRLF lines
    };
    QString user, secret;
    QList<Message> maildrop;
    QStringList sasl{QStringLiteral("PLAIN")};   // SASL mechanisms offered in CAPA
    bool offerCapa = true;
    bool offerTop = true;
    QByteArray passReply;           // overrides the PASS answer (e.g. "-ERR [IN-USE] ...")
    bool authenticatedOverTls = false;
    QStringList retrieved, topped;  // UIDLs sent by RETR / TOP
    int quitCount = 0;
    std::function<void(const QByteArray &uidl)> onDelete;   // called when a DELE is committed

    void implicitTlsOnly() { enableTls(true); m_implicitOnly = true; }
    void add(const QByteArray &uidl, const QByteArray &source) { maildrop.append({uidl, source}); }
    bool contains(const QByteArray &uidl) const
    {
        for (const Message &message : maildrop) if (message.uidl == uidl) return true;
        return false;
    }

protected:
    enum { Command, SaslPayload, SaslError };
    void greet(Connection &c) override
    {
        m_marks.remove(&c);               // a reused address must not inherit marks
        send(c, "+OK POP3 test server ready\r\n");
    }

    void line(Connection &c, const QByteArray &text) override
    {
        if (c.state == SaslError) {          // the client's answer to an error challenge
            c.state = Command;
            send(c, "-ERR [AUTH] Invalid token\r\n");
            return;
        }
        if (c.state == SaslPayload) {
            c.state = Command;
            finishSasl(c, c.data, text);
            return;
        }
        const QList<QByteArray> words = text.split(' ');
        const QByteArray verb = words.value(0).toUpper();
        if (verb == "CAPA") {
            if (!offerCapa) { send(c, "-ERR unknown command\r\n"); return; }
            QByteArray reply = "+OK capability list follows\r\nUIDL\r\nUSER\r\nRESP-CODES\r\n";
            if (offerTop) reply += "TOP\r\n";
            if (tlsEnabled() && !encrypted(c) && !m_implicitOnly) reply += "STLS\r\n";
            if (!sasl.isEmpty()) reply += "SASL " + sasl.join(QLatin1Char(' ')).toLatin1() + "\r\n";
            send(c, reply + ".\r\n");
        } else if (verb == "STLS" && tlsEnabled() && !encrypted(c) && !m_implicitOnly) {
            upgrade(c, "+OK Begin TLS negotiation\r\n");
        } else if (verb == "USER") {
            c.data = words.value(1);
            send(c, "+OK\r\n");
        } else if (verb == "PASS") {
            if (!passReply.isEmpty()) { send(c, passReply + "\r\n"); return; }
            authenticate(c, c.data == user.toUtf8() && text.mid(5) == secret.toUtf8());
        } else if (verb == "AUTH") {
            const QByteArray mechanism = words.value(1).toUpper();
            if (!sasl.contains(QString::fromLatin1(mechanism))) { send(c, "-ERR unsupported mechanism\r\n"); return; }
            if (words.size() < 3) {
                c.data = mechanism;
                c.state = SaslPayload;
                send(c, "+ \r\n");
            } else {
                finishSasl(c, mechanism, words.value(2));
            }
        } else if (!c.authenticated && verb != "QUIT") {
            send(c, "-ERR Authenticate first\r\n");
        } else if (verb == "UIDL" || verb == "LIST") {
            QByteArray reply = "+OK\r\n";
            for (qsizetype i = 0; i < maildrop.size(); ++i) {
                if (m_marks.value(&c).contains(i + 1)) continue;
                reply += QByteArray::number(i + 1) + ' '
                    + (verb == "UIDL" ? maildrop.at(i).uidl : QByteArray::number(maildrop.at(i).source.size())) + "\r\n";
            }
            send(c, reply + ".\r\n");
        } else if (verb == "RETR" || verb == "TOP") {
            const Message *message = at(c, words.value(1));
            if (!message || (verb == "TOP" && !offerTop)) { send(c, "-ERR no such message\r\n"); return; }
            QByteArray body = message->source;
            if (verb == "TOP") {
                body = body.left(body.indexOf("\r\n\r\n") + 4);
                topped.append(QString::fromLatin1(message->uidl));
            } else {
                retrieved.append(QString::fromLatin1(message->uidl));
            }
            send(c, "+OK " + QByteArray::number(body.size()) + " octets\r\n" + stuffed(body) + ".\r\n");
        } else if (verb == "DELE") {
            if (!at(c, words.value(1))) { send(c, "-ERR no such message\r\n"); return; }
            m_marks[&c].insert(words.value(1).toLongLong());
            send(c, "+OK marked\r\n");
        } else if (verb == "RSET") {
            m_marks.remove(&c);
            send(c, "+OK\r\n");
        } else if (verb == "NOOP") {
            send(c, "+OK\r\n");
        } else if (verb == "QUIT") {
            ++quitCount;
            if (c.authenticated) commit(c);
            m_marks.remove(&c);
            send(c, "+OK bye\r\n");
            c.socket->disconnectFromHost();
        } else {
            send(c, "-ERR unknown command\r\n");
        }
    }

private:
    bool m_implicitOnly = false;
    QHash<const Connection *, QSet<qint64>> m_marks;

    void authenticate(Connection &c, bool ok)
    {
        c.authenticated = ok;
        authenticatedOverTls = ok && encrypted(c);
        send(c, ok ? "+OK logged in\r\n" : "-ERR [AUTH] Invalid credentials\r\n");
    }

    void finishSasl(Connection &c, const QByteArray &mechanism, const QByteArray &payload)
    {
        const QByteArray decoded = QByteArray::fromBase64(payload);
        if (mechanism == "PLAIN") {
            const QList<QByteArray> fields = decoded.split('\0');
            authenticate(c, fields.size() == 3 && fields.at(1) == user.toUtf8() && fields.at(2) == secret.toUtf8());
            return;
        }
        const QByteArray expected = "user=" + user.toUtf8() + "\x01" "auth=Bearer " + secret.toUtf8() + "\x01\x01";
        if (decoded == expected) {
            authenticate(c, true);
        } else {
            c.state = SaslError;
            send(c, "+ eyJzdGF0dXMiOiI0MDEiLCJzY2hlbWVzIjoiQmVhcmVyIn0=\r\n");
        }
    }

    const Message *at(const Connection &c, const QByteArray &number) const
    {
        const qint64 n = number.toLongLong();
        if (n < 1 || n > maildrop.size() || m_marks.value(&c).contains(n)) return nullptr;
        return &maildrop.at(n - 1);
    }

    void commit(const Connection &c)
    {
        QList<qint64> marked = m_marks.value(&c).values();
        std::sort(marked.begin(), marked.end(), std::greater<>());
        for (const qint64 n : std::as_const(marked)) {
            if (onDelete) onDelete(maildrop.at(n - 1).uidl);
            maildrop.removeAt(n - 1);
        }
    }

    static QByteArray stuffed(const QByteArray &body)
    {
        QByteArray out;
        for (const QByteArray &lineText : body.split('\n')) {
            if (lineText.isEmpty()) continue;         // the split after the final LF
            out += (lineText.startsWith('.') ? "." : "") + lineText + "\n";
        }
        return out;
    }
};
