// SPDX-License-Identifier: LGPL-3.0-or-later
#include "pop3_session.h"

#include <QHash>

namespace QindaTK::Pop3 {
namespace {
constexpr qint64 MaximumListing = 32 * 1024 * 1024;   // UIDL/LIST of a very large maildrop
constexpr qint64 MaximumCapa = 64 * 1024;
constexpr qint64 MaximumHeaders = 4 * 1024 * 1024;
constexpr qsizetype MaximumCommand = 255;             // RFC 5034 §4 AUTH line limit

bool safeLine(const QByteArray &text)
{
    for (const char c : text)
        if (c == '\r' || c == '\n' || c == '\0') return false;
    return true;
}
} // namespace

bool validUidl(const QByteArray &uidl)
{
    if (uidl.isEmpty() || uidl.size() > 70) return false;
    for (const char c : uidl)
        if (static_cast<unsigned char>(c) < 0x21 || static_cast<unsigned char>(c) > 0x7e) return false;
    return true;
}

int Session::status(QByteArray *reply)
{
    QByteArray line;
    if (!m_channel->readLine(&line)) return -1;
    if (reply) *reply = line;
    if (line.startsWith("+OK")) return 1;
    if (line.startsWith("-ERR")) {
        const QByteArray upper = line.toUpper();
        // RFC 2449 IN-USE (maildrop locked by another session) and RFC 3206
        // SYS/TEMP are transient; AUTH and SYS/PERM are not.
        if (upper.startsWith("-ERR [IN-USE]") || upper.startsWith("-ERR [SYS/TEMP]")) m_transient = true;
        return 0;
    }
    m_channel->fail(Protocol::Failure::Rejected,
                    QStringLiteral("Unexpected POP3 reply: ") + QString::fromUtf8(line.left(200)));
    return -1;
}

bool Session::refuse(const QByteArray &verb, const QByteArray &reply)
{
    m_channel->fail(Protocol::Failure::Rejected,
                    QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(verb), QString::fromUtf8(reply)));
    return false;
}

bool Session::command(const QByteArray &command, QByteArray *reply, const QString &shown)
{
    if (!m_channel->write(command + "\r\n", shown)) return false;
    QByteArray line;
    const int result = status(&line);
    if (reply) *reply = line;
    if (result == 1) return true;
    if (result == 0) refuse(command.split(' ').first(), line);
    return false;
}

bool Session::body(QByteArray *out, qint64 limit)
{
    out->clear();
    QByteArray line;
    for (;;) {
        if (!m_channel->readLine(&line, false)) return false;
        if (line == ".") break;
        // RFC 1939 §3: a line that begins with the termination octet was
        // byte-stuffed by the server; remove exactly one leading ".".
        if (line.startsWith('.')) line.remove(0, 1);
        if (out->size() + line.size() + 2 > limit) {
            m_channel->fail(Protocol::Failure::Rejected,
                            QStringLiteral("POP3 response exceeds %1 bytes").arg(limit));
            return false;
        }
        out->append(line);
        out->append("\r\n");
    }
    m_channel->note(QStringLiteral("S: {%1 bytes}").arg(out->size()));
    return true;
}

bool Session::multiline(const QByteArray &command, QByteArray *out, qint64 limit, bool *refused)
{
    if (refused) *refused = false;
    if (!m_channel->write(command + "\r\n")) return false;
    QByteArray reply;
    const int result = status(&reply);
    if (result < 0) return false;
    if (result == 0) {
        if (refused) {
            *refused = true;
            return false;
        }
        return refuse(command.split(' ').first(), reply);
    }
    return body(out, limit);
}

bool Session::capa()
{
    m_capabilities.clear();
    QByteArray text;
    bool refused = false;
    if (!multiline("CAPA", &text, MaximumCapa, &refused)) return refused;   // RFC 1939-only server: no CAPA
    for (const QByteArray &line : text.split('\n')) {
        const QByteArrayList words = line.trimmed().toUpper().split(' ');
        if (!words.isEmpty() && !words.first().isEmpty()) m_capabilities.append(words);
    }
    return true;
}

bool Session::hasCapability(const char *name) const
{
    for (const QByteArrayList &words : m_capabilities)
        if (words.first() == name) return true;
    return false;
}

bool Session::offersSasl(const char *mechanism) const
{
    for (const QByteArrayList &words : m_capabilities)
        if (words.first() == "SASL" && words.mid(1).contains(QByteArray(mechanism))) return true;
    return false;
}

bool Session::open(const Protocol::Endpoint &endpoint, const Protocol::Credentials &credentials)
{
    if (!m_channel->open(endpoint)) return false;
    QByteArray greeting;
    const int result = status(&greeting);
    if (result < 0) return false;
    if (result == 0) {
        m_channel->fail(Protocol::Failure::Rejected,
                        QStringLiteral("Server refused the connection: ") + QString::fromUtf8(greeting));
        return false;
    }
    if (!capa()) return false;
    if (endpoint.security == QLatin1String("starttls")) {
        if (!hasCapability("STLS")) {
            // AGENT-GUARD: never fall back to plain text when TLS was
            // requested (D-187); the credentials must not leave the client.
            m_channel->fail(Protocol::Failure::Rejected,
                            QStringLiteral("Server does not offer STLS; credentials were not sent"));
            return false;
        }
        // RFC 2595 §4: capabilities learned before TLS are discarded.
        if (!command("STLS") || !m_channel->startTls() || !capa()) return false;
    }
    if (!credentials.isSet()) {
        m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("The POP3 account has no credentials"));
        return false;
    }
    if (!m_channel->authAllowed()) {
        m_channel->fail(Protocol::Failure::Rejected,
                        QStringLiteral("Refusing to send credentials over an unencrypted connection"));
        return false;
    }
    return authenticate(credentials);
}

bool Session::authenticate(const Protocol::Credentials &credentials)
{
    QString mechanism = credentials.mechanism.toLower();
    if (mechanism.isEmpty() || mechanism == QLatin1String("auto"))
        mechanism = offersSasl("PLAIN") ? QStringLiteral("plain") : QStringLiteral("user");
    if (mechanism == QLatin1String("login")) mechanism = QStringLiteral("user");   // IMAP-style setting
    const QByteArray user = credentials.user.toUtf8(), secret = credentials.secret.toUtf8();
    if (mechanism == QLatin1String("user")) {
        if (!safeLine(user) || !safeLine(secret) || user.isEmpty()) {
            m_channel->fail(Protocol::Failure::Rejected,
                            QStringLiteral("USER/PASS needs a user name and a password without line breaks"));
            return false;
        }
        return command("USER " + user) && command("PASS " + secret, nullptr, QStringLiteral("PASS <redacted>"));
    }
    QByteArray name, payload;
    if (mechanism == QLatin1String("plain")) {
        name = "PLAIN";
        payload = (QByteArray(1, '\0') + user + QByteArray(1, '\0') + secret).toBase64();
    } else if (mechanism == QLatin1String("xoauth2")) {
        name = "XOAUTH2";
        payload = ("user=" + user + "\x01" "auth=Bearer " + secret + "\x01\x01").toBase64();
    } else {
        m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("Unsupported POP3 mechanism %1").arg(mechanism));
        return false;
    }
    if (!offersSasl(name.constData())) {
        m_channel->fail(Protocol::Failure::Rejected,
                        QStringLiteral("Server does not offer SASL %1").arg(QString::fromLatin1(name)));
        return false;
    }
    return sasl(name, payload);
}

bool Session::sasl(const QByteArray &mechanism, const QByteArray &payload)
{
    m_channel->addSecret(QString::fromLatin1(payload));
    const QByteArray initial = "AUTH " + mechanism + ' ' + payload;
    // RFC 5034 §4: an initial response that would make the command longer
    // than 255 octets (an OAuth token) is sent after the empty "+ " instead.
    bool sent = initial.size() <= MaximumCommand;
    const bool written = sent
        ? m_channel->write(initial + "\r\n", QStringLiteral("AUTH %1 <redacted>").arg(QString::fromLatin1(mechanism)))
        : m_channel->write("AUTH " + mechanism + "\r\n");
    if (!written) return false;
    for (int round = 0; round < 3; ++round) {
        QByteArray line;
        if (!m_channel->readLine(&line)) return false;
        if (line.startsWith("+OK")) return true;
        if (line.startsWith("-ERR")) return refuse("AUTH", line);
        if (!line.startsWith('+')) break;
        // A second challenge is an error report (XOAUTH2 sends a JSON status
        // and expects an empty answer before its "-ERR").
        const QByteArray answer = sent ? QByteArray() : payload;
        sent = true;
        if (!m_channel->write(answer + "\r\n", answer.isEmpty() ? QString() : QStringLiteral("<redacted>")))
            return false;
    }
    m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("AUTH did not complete"));
    return false;
}

bool Session::listing(QList<Entry> *entries)
{
    entries->clear();
    QByteArray text;
    bool refused = false;
    if (!multiline("UIDL", &text, MaximumListing, &refused)) {
        if (refused)
            m_channel->fail(Protocol::Failure::Rejected,
                            QStringLiteral("Server does not support UIDL; POP3 sync needs unique message IDs"));
        return false;
    }
    QHash<qint64, qsizetype> byNumber;
    QHash<QByteArray, bool> seen;
    int skipped = 0;
    for (const QByteArray &line : text.split('\n')) {
        const QByteArrayList words = line.trimmed().split(' ');
        if (words.size() != 2) continue;
        bool ok = false;
        const qint64 number = words.at(0).toLongLong(&ok);
        if (!ok || number <= 0 || !validUidl(words.at(1)) || seen.contains(words.at(1))) {
            ++skipped;
            continue;
        }
        seen.insert(words.at(1), true);
        byNumber.insert(number, entries->size());
        entries->append({number, words.at(1), 0});
    }
    if (skipped > 0) m_channel->note(QStringLiteral("* skipped %1 invalid or duplicate UIDL lines").arg(skipped));
    if (!multiline("LIST", &text, MaximumListing)) return false;
    for (const QByteArray &line : text.split('\n')) {
        const QByteArrayList words = line.trimmed().split(' ');
        if (words.size() < 2) continue;
        const qsizetype index = byNumber.value(words.at(0).toLongLong(), -1);
        if (index >= 0) (*entries)[index].size = words.at(1).toLongLong();
    }
    return true;
}

bool Session::retrieve(qint64 number, QByteArray *source, qint64 limit)
{
    return multiline("RETR " + QByteArray::number(number), source, limit);
}

bool Session::headers(qint64 number, QByteArray *source)
{
    bool refused = false;
    return multiline("TOP " + QByteArray::number(number) + " 0", source, MaximumHeaders, &refused);
}

bool Session::remove(qint64 number)
{
    if (!m_channel->write("DELE " + QByteArray::number(number) + "\r\n")) return false;
    QByteArray reply;
    const int result = status(&reply);
    if (result == 1) return true;
    if (result == 0) {
        // Unmark this session's earlier DELEs before failing: a server that
        // (against RFC 1939) commits on a dropped connection deletes nothing.
        if (m_channel->write("RSET\r\n")) status(nullptr);
        refuse("DELE", reply);
    }
    return false;
}

bool Session::quit()
{
    return command("QUIT");
}

} // namespace QindaTK::Pop3
