// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// In-process SMTP and IMAP servers for the provider tests. They listen on
// 127.0.0.1 with an ephemeral port and speak the protocol subset the
// providers use, over real TCP, driven by the test's event loop while the
// provider's blocking client runs on a worker thread. No external network.
// AGENT-CONTRACT (smtp_mail_provider.cpp, imap_session.cpp): replies follow
// RFC 5321 / RFC 3501 framing (multi-line "250-", tagged status, {n} literals).

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <algorithm>
#include <memory>

#include "tls_test_support.h"

class LineServer {
public:
    struct Connection {
        QTcpSocket *socket = nullptr;
        QByteArray buffer, data;
        int state = 0;
        bool authenticated = false;
    };

    LineServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) accept(socket);
        });
    }
    virtual ~LineServer() = default;
    quint16 port() const { return m_server.serverPort(); }
    QStringList log;       // every client line, as received
    QStringList encryptedLog;   // the lines that arrived over TLS
    // implicit: TLS from the first byte; otherwise offered through STARTTLS.
    void enableTls(bool implicit)
    {
        m_server.tls = true;
        m_server.implicit = implicit;
    }
    bool tlsEnabled() const { return m_server.tls; }

protected:
    virtual void greet(Connection &connection) = 0;
    virtual void line(Connection &connection, const QByteArray &line) = 0;
    static void send(Connection &connection, const QByteArray &bytes)
    {
        connection.socket->write(bytes);
        connection.socket->flush();
    }
    static bool encrypted(const Connection &connection)
    {
        const QSslSocket *socket = TestTls::ssl(connection.socket);
        return socket && socket->isEncrypted();
    }
    // Replies with `ready`, then runs the server side of the handshake.
    static void upgrade(Connection &connection, const QByteArray &ready)
    {
        send(connection, ready);
        connection.buffer.clear();
        if (QSslSocket *socket = TestTls::ssl(connection.socket)) socket->startServerEncryption();
    }

private:
    void accept(QTcpSocket *socket)
    {
        auto connection = std::make_shared<Connection>();
        connection->socket = socket;
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, connection] {
            connection->buffer += connection->socket->readAll();
            qsizetype end = 0;
            while ((end = connection->buffer.indexOf('\n')) >= 0) {
                QByteArray text = connection->buffer.left(end);
                connection->buffer.remove(0, end + 1);
                if (text.endsWith('\r')) text.chop(1);
                log.append(QString::fromUtf8(text));
                if (encrypted(*connection)) encryptedLog.append(QString::fromUtf8(text));
                line(*connection, text);
            }
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QSslSocket *secure = TestTls::ssl(socket);
        if (m_server.implicit && secure) {
            QObject::connect(secure, &QSslSocket::encrypted, secure, [this, connection] { greet(*connection); });
            secure->startServerEncryption();
            return;
        }
        greet(*connection);
    }
    TestTls::Server m_server;
};

class SmtpTestServer : public LineServer {
public:
    QString user, secret;
    QStringList mechanisms{QStringLiteral("PLAIN"), QStringLiteral("LOGIN")};
    bool offerStartTls = false;
    bool requireTlsForAuth = false;    // answer AUTH with 530 until TLS is active
    QStringList refuse;                // recipients answered with 550
    QList<QByteArray> messages;        // accepted DATA, dot-unstuffed, CRLF lines
    QString envelopeFrom;
    QStringList envelopeRecipients;

protected:
    enum { Command, Data, LoginUser, LoginPassword };
    void greet(Connection &c) override { send(c, "220 test.local ESMTP ready\r\n"); }
    void line(Connection &c, const QByteArray &text) override
    {
        if (c.state == Data) {
            if (text == ".") {
                messages.append(c.data);
                c.state = Command;
                send(c, "250 2.0.0 Ok: queued as TEST1\r\n");
            } else {
                c.data += (text.startsWith('.') ? text.mid(1) : text) + "\r\n";
            }
            return;
        }
        if (c.state == LoginUser) {
            c.data = QByteArray::fromBase64(text);
            c.state = LoginPassword;
            send(c, "334 UGFzc3dvcmQ6\r\n");
            return;
        }
        if (c.state == LoginPassword) {
            c.state = Command;
            authReply(c, c.data == user.toUtf8() && QByteArray::fromBase64(text) == secret.toUtf8());
            return;
        }
        const QByteArray verb = text.left(4).toUpper();
        if (verb == "EHLO") {
            QByteArray reply = "250-test.local greets you\r\n";
            if (offerStartTls && !encrypted(c)) reply += "250-STARTTLS\r\n";
            if (!mechanisms.isEmpty()) reply += "250-AUTH " + mechanisms.join(QLatin1Char(' ')).toLatin1() + "\r\n";
            send(c, reply + "250 8BITMIME\r\n");
        } else if (verb == "STAR" && text.toUpper() == "STARTTLS" && offerStartTls && tlsEnabled() && !encrypted(c)) {
            upgrade(c, "220 2.0.0 Ready to start TLS\r\n");
        } else if (verb == "AUTH" && requireTlsForAuth && !encrypted(c)) {
            send(c, "530 5.7.0 Must issue a STARTTLS command first\r\n");
        } else if (verb == "AUTH") {
            const QList<QByteArray> parts = text.split(' ');
            const QByteArray mechanism = parts.value(1).toUpper();
            if (mechanism == "PLAIN") {
                const QList<QByteArray> fields = QByteArray::fromBase64(parts.value(2)).split('\0');
                authReply(c, fields.size() == 3 && fields.at(1) == user.toUtf8() && fields.at(2) == secret.toUtf8());
            } else if (mechanism == "LOGIN") {
                c.state = LoginUser;
                send(c, "334 VXNlcm5hbWU6\r\n");
            } else if (mechanism == "XOAUTH2") {
                const QByteArray expected = "user=" + user.toUtf8() + "\x01" "auth=Bearer " + secret.toUtf8() + "\x01\x01";
                if (QByteArray::fromBase64(parts.value(2)) == expected) authReply(c, true);
                else send(c, "334 eyJzdGF0dXMiOiI0MDEifQ==\r\n");      // client answers with an empty line
            } else {
                send(c, "504 5.5.4 Unrecognized authentication type\r\n");
            }
        } else if (text.isEmpty()) {
            send(c, "535 5.7.8 Authentication failed\r\n");
        } else if (verb == "MAIL") {
            envelopeFrom = QString::fromLatin1(text.mid(text.indexOf('<') + 1).chopped(1));
            send(c, "250 2.1.0 Ok\r\n");
        } else if (verb == "RCPT") {
            const QString address = QString::fromLatin1(text.mid(text.indexOf('<') + 1).chopped(1));
            if (refuse.contains(address)) {
                send(c, "550 5.1.1 No such user\r\n");
            } else {
                envelopeRecipients.append(address);
                send(c, "250 2.1.5 Ok\r\n");
            }
        } else if (verb == "RSET") {
            envelopeRecipients.clear();
            send(c, "250 2.0.0 Ok\r\n");
        } else if (verb == "DATA") {
            c.state = Data;
            c.data.clear();
            send(c, "354 End data with <CR><LF>.<CR><LF>\r\n");
        } else if (verb == "QUIT") {
            send(c, "221 2.0.0 Bye\r\n");
            c.socket->disconnectFromHost();
        } else {
            send(c, "502 5.5.2 Command not recognized\r\n");
        }
    }

private:
    void authReply(Connection &c, bool ok)
    {
        c.authenticated = ok;
        send(c, ok ? "235 2.7.0 Authentication successful\r\n" : "535 5.7.8 Authentication failed\r\n");
    }
};

class ImapTestServer : public LineServer {
public:
    struct Message {
        qint64 uid = 0;
        QByteArray source;
        QStringList flags;
    };
    QString user, secret;
    qint64 uidValidity = 1000;
    bool authenticatedOverTls = false;
    QList<Message> inbox, archive;
    QStringList capabilities{QStringLiteral("IMAP4rev1"), QStringLiteral("AUTH=PLAIN"), QStringLiteral("SASL-IR"),
                             QStringLiteral("UIDPLUS"), QStringLiteral("MOVE")};

    void implicitTlsOnly() { enableTls(true); m_implicitOnly = true; }
    void add(qint64 uid, const QByteArray &source, const QStringList &flags = {})
    {
        inbox.append({uid, source, flags});
    }
    Message *find(qint64 uid)
    {
        for (Message &message : inbox) if (message.uid == uid) return &message;
        return nullptr;
    }

protected:
    void greet(Connection &c) override { send(c, "* OK [CAPABILITY IMAP4rev1] test IMAP ready\r\n"); }
    void line(Connection &c, const QByteArray &text) override
    {
        if (c.state == 1) {                 // the client's answer to an XOAUTH2 error challenge
            c.state = 0;
            send(c, c.data + " NO [AUTHENTICATIONFAILED] Invalid token\r\n");
            return;
        }
        const qsizetype space = text.indexOf(' ');
        const QByteArray tag = text.left(space);
        const QByteArray rest = space < 0 ? QByteArray() : text.mid(space + 1);
        const QByteArray upper = rest.toUpper();
        auto ok = [&](const QByteArray &what) { send(c, tag + " OK " + what + " completed\r\n"); };
        if (upper == "CAPABILITY") {
            QStringList offered = capabilities;
            if (tlsEnabled() && !encrypted(c) && !m_implicitOnly) offered.append(QStringLiteral("STARTTLS"));
            send(c, "* CAPABILITY " + offered.join(QLatin1Char(' ')).toLatin1() + "\r\n");
            ok("CAPABILITY");
        } else if (upper == "STARTTLS" && tlsEnabled() && !encrypted(c)) {
            upgrade(c, tag + " OK Begin TLS negotiation now\r\n");
        } else if (upper.startsWith("AUTHENTICATE XOAUTH2 ")) {
            const QByteArray expected = "user=" + user.toUtf8() + "\x01" "auth=Bearer " + secret.toUtf8() + "\x01\x01";
            c.authenticated = QByteArray::fromBase64(rest.mid(21)) == expected;
            authenticatedOverTls = c.authenticated && encrypted(c);
            if (c.authenticated) {
                send(c, tag + " OK AUTHENTICATE completed\r\n");
            } else {
                c.state = 1;
                c.data = tag;
                send(c, "+ eyJzdGF0dXMiOiI0MDEiLCJzY2hlbWVzIjoiQmVhcmVyIn0=\r\n");
            }
        } else if (upper.startsWith("LOGIN ")) {
            static const QRegularExpression quoted(QStringLiteral("^LOGIN \"((?:[^\"\\\\]|\\\\.)*)\" \"((?:[^\"\\\\]|\\\\.)*)\"$"),
                                                   QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = quoted.match(QString::fromUtf8(rest));
            auto unescape = [](QString s) { return s.replace(QStringLiteral("\\\""), QStringLiteral("\"")).replace(QStringLiteral("\\\\"), QStringLiteral("\\")); };
            c.authenticated = m.hasMatch() && unescape(m.captured(1)) == user && unescape(m.captured(2)) == secret;
            send(c, c.authenticated ? tag + " OK LOGIN completed\r\n" : tag + " NO [AUTHENTICATIONFAILED] Invalid credentials\r\n");
        } else if (upper.startsWith("AUTHENTICATE PLAIN ")) {
            const QList<QByteArray> fields = QByteArray::fromBase64(rest.mid(19)).split('\0');
            c.authenticated = fields.size() == 3 && fields.at(1) == user.toUtf8() && fields.at(2) == secret.toUtf8();
            send(c, c.authenticated ? tag + " OK AUTHENTICATE completed\r\n" : tag + " NO [AUTHENTICATIONFAILED] Invalid credentials\r\n");
        } else if (!c.authenticated && !upper.startsWith("LOGOUT")) {
            send(c, tag + " BAD Authenticate first\r\n");
        } else if (upper.startsWith("SELECT ")) {
            send(c, "* FLAGS (\\Seen \\Flagged \\Deleted)\r\n* " + QByteArray::number(inbox.size()) + " EXISTS\r\n"
                    "* OK [UIDVALIDITY " + QByteArray::number(uidValidity) + "] UIDs valid\r\n");
            send(c, tag + " OK [READ-WRITE] SELECT completed\r\n");
        } else if (upper.startsWith("UID FETCH ")) {
            fetch(c, rest.mid(10));
            ok("FETCH");
        } else if (upper.startsWith("FETCH ")) {    // by sequence number (D-353)
            fetch(c, rest.mid(6), true);
            ok("FETCH");
        } else if (upper.startsWith("UID STORE ")) {
            const QList<QByteArray> parts = rest.split(' ');
            Message *message = find(parts.value(2).toLongLong());
            const QString flag = QString::fromLatin1(rest.mid(rest.indexOf('(') + 1).chopped(1));
            if (message && parts.value(3).startsWith('+') && !message->flags.contains(flag)) message->flags.append(flag);
            if (message && parts.value(3).startsWith('-')) message->flags.removeAll(flag);
            ok("STORE");
        } else if (upper.startsWith("UID EXPUNGE ") || upper == "EXPUNGE") {
            expunge(c, upper == "EXPUNGE" ? -1 : rest.mid(12).toLongLong());
            ok("EXPUNGE");
        } else if (upper.startsWith("UID MOVE ") || upper.startsWith("UID COPY ")) {
            const qint64 uid = rest.split(' ').value(2).toLongLong();
            if (Message *message = find(uid)) {
                archive.append(*message);
                if (upper.startsWith("UID MOVE ")) {
                    message->flags.append(QStringLiteral("\\Deleted"));
                    expunge(c, uid);
                }
            }
            ok("MOVE");
        } else if (upper == "LOGOUT") {
            send(c, "* BYE logging out\r\n" + tag + " OK LOGOUT completed\r\n");
            c.socket->disconnectFromHost();
        } else {
            send(c, tag + " BAD Unknown command\r\n");
        }
    }

protected:                   // imap_folder_test_server.h extends the mailbox commands
    bool m_implicitOnly = false;
    static QByteArray literal(const QByteArray &bytes) { return "{" + QByteArray::number(bytes.size()) + "}\r\n" + bytes; }

    // Raw body of top-level part `index` (1-based) of a multipart fixture.
    static QByteArray part(const QByteArray &source, int index)
    {
        static const QRegularExpression boundary(QStringLiteral("boundary=\"([^\"]+)\""));
        const QRegularExpressionMatch m = boundary.match(QString::fromLatin1(source));
        if (!m.hasMatch()) return index == 1 ? source.mid(source.indexOf("\r\n\r\n") + 4) : QByteArray();
        const QList<QByteArray> pieces = source.split('\n');
        const QByteArray mark = "--" + m.captured(1).toLatin1();
        QList<QByteArray> parts;
        QByteArray current;
        bool inside = false;
        for (QByteArray lineText : pieces) {
            if (lineText.endsWith('\r')) lineText.chop(1);
            if (lineText.startsWith(mark)) {
                if (inside) parts.append(current);
                current.clear();
                inside = !lineText.startsWith(mark + "--");
                continue;
            }
            if (inside) current += lineText + "\r\n";
        }
        const QByteArray chosen = parts.value(index - 1);
        return chosen.mid(chosen.indexOf("\r\n\r\n") + 4);
    }

    // A UID set ("3:5,9", "13:*") or, with bySequence, a sequence set.
    void fetch(Connection &c, const QByteArray &arguments, bool bySequence = false)
    {
        const qsizetype space = arguments.indexOf(' ');
        const QByteArray set = arguments.left(space);
        const QByteArray items = arguments.mid(space + 1).toUpper();
        qint64 maxUid = 0;
        for (const Message &message : std::as_const(inbox)) maxUid = std::max(maxUid, message.uid);
        const qint64 last = bySequence ? inbox.size() : maxUid;
        QList<QPair<qint64, qint64>> ranges;
        for (const QByteArray &piece : set.split(',')) {
            const QList<QByteArray> range = piece.split(':');
            qint64 low = range.value(0).toLongLong();
            const qint64 high = range.size() < 2 ? low : range.value(1) == "*" ? last : range.value(1).toLongLong();
            if (range.value(1) == "*" && low > last) low = last;        // RFC 3501: n:* includes the last message
            ranges.append({std::min(low, high), std::max(low, high)});
        }
        for (qsizetype i = 0; i < inbox.size(); ++i) {
            const Message &message = inbox.at(i);
            const qint64 key = bySequence ? i + 1 : message.uid;
            if (std::none_of(ranges.cbegin(), ranges.cend(),
                             [key](const auto &r) { return key >= r.first && key <= r.second; }))
                continue;
            QByteArray response = "* " + QByteArray::number(i + 1) + " FETCH (UID " + QByteArray::number(message.uid);
            if (items.contains("FLAGS")) response += " FLAGS (" + message.flags.join(QLatin1Char(' ')).toLatin1() + ")";
            if (items.contains("RFC822.SIZE")) response += " RFC822.SIZE " + QByteArray::number(message.source.size());
            static const QRegularExpression section(QStringLiteral("BODY\\.PEEK\\[([^\\]]*)\\]"));
            const QRegularExpressionMatch s = section.match(QString::fromLatin1(items));
            if (s.hasMatch()) {
                const QString name = s.captured(1);
                const QByteArray body = name.isEmpty() ? message.source
                    : name == QLatin1String("HEADER") ? message.source.left(message.source.indexOf("\r\n\r\n") + 4)
                    : part(message.source, name.toInt());
                response += " BODY[" + name.toLatin1() + "] " + literal(body);
            }
            send(c, response + ")\r\n");
        }
    }

    void expunge(Connection &c, qint64 uid)
    {
        for (qsizetype i = inbox.size() - 1; i >= 0; --i) {
            if (!inbox.at(i).flags.contains(QStringLiteral("\\Deleted")) || (uid >= 0 && inbox.at(i).uid != uid)) continue;
            inbox.removeAt(i);
            send(c, "* " + QByteArray::number(i + 1) + " EXPUNGE\r\n");
        }
    }
};
