// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_session.h"

#include <QRegularExpression>

namespace QindaTK::Imap {
namespace {
constexpr qsizetype MaximumResponse = 64 * 1024 * 1024;

bool parseValue(const QByteArray &s, qsizetype &pos, Value *out, int depth)
{
    while (pos < s.size() && s.at(pos) == ' ') ++pos;
    if (pos >= s.size()) return false;
    const char c = s.at(pos);
    if (c == '(') {
        if (depth > 32) return false;
        out->kind = Value::List;
        ++pos;
        for (;;) {
            while (pos < s.size() && s.at(pos) == ' ') ++pos;
            if (pos >= s.size()) return false;
            if (s.at(pos) == ')') { ++pos; return true; }
            Value child;
            if (!parseValue(s, pos, &child, depth + 1)) return false;
            out->items.append(child);
        }
    }
    if (c == '"') {
        out->kind = Value::String;
        ++pos;
        while (pos < s.size()) {
            const char d = s.at(pos++);
            if (d == '\\' && pos < s.size()) { out->text += s.at(pos++); continue; }
            if (d == '"') return true;
            out->text += d;
        }
        return false;
    }
    if (c == '{') {
        const qsizetype close = s.indexOf('}', pos);
        if (close < 0) return false;
        QByteArray digits = s.mid(pos + 1, close - pos - 1);
        if (digits.endsWith('+')) digits.chop(1);
        bool ok = false;
        const qint64 length = digits.toLongLong(&ok);
        if (!ok || length < 0) return false;
        pos = close + 1;
        if (s.mid(pos, 2) == "\r\n") pos += 2;
        else if (pos < s.size() && s.at(pos) == '\n') pos += 1;
        else return false;
        if (pos + length > s.size()) return false;
        out->kind = Value::String;
        out->text = s.mid(pos, length);
        pos += length;
        return true;
    }
    if (c == ')') return false;
    const qsizetype start = pos;
    int bracket = 0;
    while (pos < s.size()) {
        const char d = s.at(pos);
        if (d == '[') ++bracket;
        else if (d == ']') --bracket;
        else if (bracket <= 0 && (d == ' ' || d == '(' || d == ')' || d == '\r' || d == '\n')) break;
        ++pos;
    }
    out->text = s.mid(start, pos - start);
    out->kind = out->text.compare("NIL", Qt::CaseInsensitive) == 0 ? Value::Nil : Value::Atom;
    return !out->text.isEmpty();
}
} // namespace

QList<Value> parseResponse(const QByteArray &raw)
{
    QList<Value> values;
    qsizetype pos = 0;
    for (;;) {
        while (pos < raw.size() && (raw.at(pos) == ' ' || raw.at(pos) == '\r' || raw.at(pos) == '\n')) ++pos;
        if (pos >= raw.size()) break;
        Value value;
        if (!parseValue(raw, pos, &value, 0)) break;
        values.append(value);
    }
    return values;
}

QByteArray quoted(const QString &text)
{
    QByteArray out = "\"";
    for (const char c : text.toUtf8()) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + '"';
}

const Value *FetchItem::find(const char *name) const
{
    const QByteArray wanted = QByteArray(name).toUpper();
    const bool prefix = wanted.endsWith('[');
    for (qsizetype i = 0; i + 1 < attributes.size(); i += 2) {
        const Value &key = attributes.at(i);
        if (key.kind != Value::Atom) continue;
        const QByteArray upper = key.text.toUpper();
        if (prefix ? upper.startsWith(wanted) : upper == wanted) return &attributes.at(i + 1);
    }
    return nullptr;
}

QList<FetchItem> fetchItems(const QList<QByteArray> &untagged)
{
    QList<FetchItem> items;
    for (const QByteArray &raw : untagged) {
        const QList<Value> values = parseResponse(raw);
        if (values.size() < 3 || !values.at(1).isAtom("FETCH") || values.at(2).kind != Value::List) continue;
        FetchItem item;
        item.sequence = values.at(0).text.toLongLong();
        item.attributes = values.at(2).items;
        items.append(item);
    }
    return items;
}

bool Session::readResponse(QByteArray *raw)
{
    static const QRegularExpression literal(QStringLiteral("\\{(\\d+)\\+?\\}$"));
    QByteArray line;
    if (!m_channel->readLine(&line)) return false;
    *raw = line;
    for (;;) {
        const QRegularExpressionMatch match = literal.match(QString::fromLatin1(line));
        if (!match.hasMatch()) return true;
        const qint64 length = match.captured(1).toLongLong();
        if (raw->size() + length > MaximumResponse) {
            m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("IMAP response exceeds 64 MiB"));
            return false;
        }
        QByteArray bytes;
        if (!m_channel->readBytes(length, &bytes) || !m_channel->readLine(&line)) return false;
        *raw += "\r\n" + bytes + line;
    }
}

bool Session::command(const QByteArray &command, QList<QByteArray> *untagged, const QString &shown,
                      const QByteArray &continuation)
{
    const QByteArray tag = "a" + QByteArray::number(++m_tag);
    if (!m_channel->write(tag + ' ' + command + "\r\n",
                          shown.isEmpty() ? QString() : QString::fromLatin1(tag) + QLatin1Char(' ') + shown))
        return false;
    bool continued = false;
    for (;;) {
        QByteArray raw;
        if (!readResponse(&raw)) return false;
        if (raw.startsWith("* ")) {
            if (untagged) untagged->append(raw.mid(2));
            continue;
        }
        if (raw.startsWith('+')) {
            // A second request (e.g. an XOAUTH2 error challenge) gets an empty answer.
            const QByteArray answer = continued ? QByteArray() : continuation;
            continued = true;
            if (!m_channel->write(answer + "\r\n", answer.isEmpty() ? QString() : QStringLiteral("<redacted>")))
                return false;
            continue;
        }
        if (!raw.startsWith(tag + ' ')) continue;
        const QByteArray status = raw.mid(tag.size() + 1);
        m_lastStatus = QString::fromUtf8(status);
        if (status.startsWith("OK")) return true;
        const QByteArray verb = command.split(' ').first();
        m_channel->fail(Protocol::Failure::Rejected,
                        QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(verb), m_lastStatus));
        return false;
    }
}

bool Session::capability()
{
    QList<QByteArray> untagged;
    if (!command("CAPABILITY", &untagged)) return false;
    m_capabilities.clear();
    for (const QByteArray &line : std::as_const(untagged))
        if (line.toUpper().startsWith("CAPABILITY "))
            m_capabilities += QString::fromLatin1(line.mid(11)).toUpper().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return true;
}

bool Session::hasCapability(const char *name) const
{
    return m_capabilities.contains(QString::fromLatin1(name).toUpper());
}

bool Session::open(const Protocol::Endpoint &endpoint, const Protocol::Credentials &credentials)
{
    if (!m_channel->open(endpoint)) return false;
    QByteArray greeting;
    if (!readResponse(&greeting)) return false;
    const bool preauth = greeting.startsWith("* PREAUTH");
    if (!greeting.startsWith("* OK") && !preauth) {
        m_channel->fail(Protocol::Failure::Rejected,
                        QStringLiteral("Server refused the connection: ") + QString::fromUtf8(greeting));
        return false;
    }
    if (!capability()) return false;
    if (endpoint.security == QLatin1String("starttls") && !preauth) {
        if (!hasCapability("STARTTLS")) {
            // AGENT-GUARD: never fall back to plain text when TLS was requested.
            m_channel->fail(Protocol::Failure::Rejected,
                            QStringLiteral("Server does not offer STARTTLS; credentials were not sent"));
            return false;
        }
        if (!command("STARTTLS") || !m_channel->startTls() || !capability()) return false;
    }
    if (preauth) return true;
    if (!credentials.isSet()) {
        m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("The IMAP account has no credentials"));
        return false;
    }
    if (!m_channel->authAllowed()) {
        m_channel->fail(Protocol::Failure::Rejected,
                        QStringLiteral("Refusing to send credentials over an unencrypted connection"));
        return false;
    }
    return authenticate(credentials) && capability();
}

bool Session::authenticate(const Protocol::Credentials &credentials)
{
    QString mechanism = credentials.mechanism.toLower();
    if (mechanism.isEmpty() || mechanism == QLatin1String("auto"))
        mechanism = hasCapability("AUTH=PLAIN") ? QStringLiteral("plain") : QStringLiteral("login");
    const QByteArray user = credentials.user.toUtf8(), secret = credentials.secret.toUtf8();
    if (mechanism == QLatin1String("login")) {
        if (hasCapability("LOGINDISABLED")) {
            m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("Server disabled LOGIN on this connection"));
            return false;
        }
        for (const char c : user + secret) {
            if (c == '\r' || c == '\n' || static_cast<unsigned char>(c) >= 0x80) {
                m_channel->fail(Protocol::Failure::Rejected,
                                QStringLiteral("LOGIN needs ASCII credentials; use the plain mechanism"));
                return false;
            }
        }
        return command("LOGIN " + quoted(credentials.user) + ' ' + quoted(credentials.secret), nullptr,
                       QStringLiteral("LOGIN <redacted>"));
    }
    QByteArray name, payload;
    if (mechanism == QLatin1String("plain")) {
        name = "PLAIN";
        payload = (QByteArray(1, '\0') + user + QByteArray(1, '\0') + secret).toBase64();
    } else if (mechanism == QLatin1String("xoauth2")) {
        name = "XOAUTH2";
        payload = ("user=" + user + "\x01" "auth=Bearer " + secret + "\x01\x01").toBase64();
    } else {
        m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("Unsupported IMAP mechanism %1").arg(mechanism));
        return false;
    }
    if (!hasCapability(("AUTH=" + name).constData())) {
        m_channel->fail(Protocol::Failure::Rejected,
                        QStringLiteral("Server does not offer AUTH=%1").arg(QString::fromLatin1(name)));
        return false;
    }
    m_channel->addSecret(QString::fromLatin1(payload));
    const QString shown = QStringLiteral("AUTHENTICATE %1 <redacted>").arg(QString::fromLatin1(name));
    if (hasCapability("SASL-IR")) return command("AUTHENTICATE " + name + ' ' + payload, nullptr, shown);
    return command("AUTHENTICATE " + name, nullptr, shown, payload);
}

bool Session::select(const QString &mailbox, qint64 *uidValidity, qint64 *exists)
{
    for (const QChar c : mailbox) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e) {
            m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("Mailbox names must be printable ASCII"));
            return false;
        }
    }
    QList<QByteArray> untagged;
    if (!command("SELECT " + quoted(mailbox), &untagged)) return false;
    static const QRegularExpression validity(QStringLiteral("\\[UIDVALIDITY (\\d+)\\]"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression count(QStringLiteral("^(\\d+) EXISTS"), QRegularExpression::CaseInsensitiveOption);
    *uidValidity = 0;
    *exists = 0;
    for (const QByteArray &line : std::as_const(untagged)) {
        const QString text = QString::fromLatin1(line);
        const QRegularExpressionMatch v = validity.match(text);
        if (v.hasMatch()) *uidValidity = v.captured(1).toLongLong();
        const QRegularExpressionMatch e = count.match(text);
        if (e.hasMatch()) *exists = e.captured(1).toLongLong();
    }
    if (*uidValidity <= 0) {
        m_channel->fail(Protocol::Failure::Rejected, QStringLiteral("Server did not report UIDVALIDITY"));
        return false;
    }
    return true;
}

void Session::logout()
{
    // Best effort: the operation's outcome is already decided by the caller.
    command("LOGOUT");
}

} // namespace QindaTK::Imap
