// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mail_mime.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTimeZone>
#include <QUuid>

namespace QindaTK::Mime {
namespace {
bool isAsciiPrintable(const QString &text)
{
    for (const QChar c : text)
        if (c.unicode() < 0x20 || c.unicode() > 0x7e) return false;
    return true;
}

QString singleLine(QString text)
{
    text.replace(QLatin1String("\r\n"), QLatin1String(" "));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed();
}

QByteArray wrapBase64(const QByteArray &bytes)
{
    const QByteArray encoded = bytes.toBase64();
    QByteArray out;
    out.reserve(encoded.size() + encoded.size() / 76 * 2 + 2);
    for (qsizetype i = 0; i < encoded.size(); i += 76) out += encoded.mid(i, 76) + "\r\n";
    return out;
}

// Normalizes a text body to CRLF lines and picks 7bit or base64.
QByteArray textLeaf(const QString &text, const QByteArray &subtype)
{
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QByteArray utf8 = normalized.toUtf8();
    bool sevenBit = true;
    for (const QByteArray &line : utf8.split('\n')) {
        if (line.size() > 998) { sevenBit = false; break; }
        for (const char c : line)
            if (static_cast<unsigned char>(c) >= 0x80 || c == '\0') { sevenBit = false; break; }
        if (!sevenBit) break;
    }
    QByteArray part = "Content-Type: text/" + subtype + "; charset=utf-8\r\n";
    if (sevenBit) {
        QByteArray body = utf8;
        body.replace("\n", "\r\n");
        if (!body.endsWith("\r\n")) body += "\r\n";
        return part + "Content-Transfer-Encoding: 7bit\r\n\r\n" + body;
    }
    return part + "Content-Transfer-Encoding: base64\r\n\r\n" + wrapBase64(utf8);
}

QByteArray boundary()
{
    return "=_qindatk_" + QUuid::createUuid().toString(QUuid::Id128).toLatin1();
}

QByteArray multipart(const QByteArray &subtype, const QList<QByteArray> &parts)
{
    const QByteArray mark = boundary();
    QByteArray out = "Content-Type: multipart/" + subtype + "; boundary=\"" + mark + "\"\r\n\r\n";
    for (const QByteArray &part : parts) out += "--" + mark + "\r\n" + part;
    return out + "--" + mark + "--\r\n";
}

QByteArray safeMimeType(const QString &type)
{
    static const QRegularExpression valid(
        QStringLiteral("^[A-Za-z0-9!#$&^_.+-]+/[A-Za-z0-9!#$&^_.+-]+$"));
    return valid.match(type).hasMatch() ? type.toLower().toLatin1()
                                        : QByteArrayLiteral("application/octet-stream");
}

QByteArray attachmentLeaf(const OutgoingAttachment &attachment)
{
    QString name = singleLine(attachment.name);
    name.remove(QLatin1Char('"')).remove(QLatin1Char('\\'));
    if (name.isEmpty()) name = QStringLiteral("attachment");
    QByteArray nameParam, filenameParam;
    if (isAsciiPrintable(name)) {
        nameParam = "; name=\"" + name.toLatin1() + '"';
        filenameParam = "; filename=\"" + name.toLatin1() + '"';
    } else {
        nameParam = "; name=\"" + encodeHeaderText(name) + '"';
        filenameParam = "; filename*=utf-8''" + name.toUtf8().toPercentEncoding();
    }
    return "Content-Type: " + safeMimeType(attachment.mimeType) + nameParam + "\r\n"
        + "Content-Disposition: attachment" + filenameParam + "\r\n"
        + "Content-Transfer-Encoding: base64\r\n\r\n" + wrapBase64(attachment.bytes);
}

QByteArray messageIdHeader(const QString &value)
{
    QString id = singleLine(value);
    id.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
    return id.isEmpty() ? QByteArray() : "<" + id.toLatin1() + ">";
}
} // namespace

bool validAddress(const QString &address)
{
    if (address.size() > 254 || address.count(QLatin1Char('@')) != 1) return false;
    const qsizetype at = address.indexOf(QLatin1Char('@'));
    const QString local = address.left(at), domain = address.mid(at + 1);
    if (local.isEmpty() || local.size() > 64 || domain.isEmpty() || domain.startsWith(QLatin1Char('.'))
        || domain.endsWith(QLatin1Char('.')) || domain.contains(QLatin1String("..")))
        return false;
    static const QString forbidden = QStringLiteral("<>()[]\\,;:\" ");
    for (const QChar c : address)
        if (c.unicode() <= 0x20 || c.unicode() >= 0x7f || forbidden.contains(c)) return false;
    return true;
}

QList<Address> parseAddressList(const QString &text, QString *error)
{
    QList<Address> out;
    QStringList entries;
    QString current;
    bool quoted = false, angle = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) quoted = !quoted;
        else if (!quoted && c == QLatin1Char('<')) angle = true;
        else if (!quoted && c == QLatin1Char('>')) angle = false;
        if (!quoted && !angle && (c == QLatin1Char(',') || c == QLatin1Char(';'))) {
            entries.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    entries.append(current);
    for (const QString &raw : entries) {
        const QString entry = raw.trimmed();
        if (entry.isEmpty()) continue;
        Address address;
        const qsizetype open = entry.lastIndexOf(QLatin1Char('<'));
        if (open >= 0) {
            const qsizetype close = entry.indexOf(QLatin1Char('>'), open);
            if (close < 0 || !entry.mid(close + 1).trimmed().isEmpty()) {
                if (error) *error = QStringLiteral("Malformed address \"%1\"").arg(entry);
                return {};
            }
            address.address = entry.mid(open + 1, close - open - 1).trimmed();
            address.name = entry.left(open).trimmed();
            if (address.name.size() >= 2 && address.name.startsWith(QLatin1Char('"'))
                && address.name.endsWith(QLatin1Char('"')))
                address.name = address.name.mid(1, address.name.size() - 2);
        } else {
            address.address = entry;
        }
        address.name = singleLine(address.name);
        if (!validAddress(address.address)) {
            if (error) *error = QStringLiteral("Invalid address \"%1\"").arg(entry);
            return {};
        }
        out.append(address);
    }
    return out;
}

QByteArray formatAddress(const Address &address)
{
    const QByteArray mailbox = "<" + address.address.toLatin1() + ">";
    const QString name = singleLine(address.name);
    if (name.isEmpty()) return address.address.toLatin1();
    if (!isAsciiPrintable(name)) return encodeHeaderText(name) + " " + mailbox;
    QString quoted = name;
    quoted.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    return "\"" + quoted.toLatin1() + "\" " + mailbox;
}

QByteArray encodeHeaderText(const QString &text)
{
    const QString line = singleLine(text);
    if (isAsciiPrintable(line)) return line.toLatin1();
    // Each encoded word carries at most 45 UTF-8 bytes (60 base64 chars,
    // 72 with the =?UTF-8?B?...?= wrapper), split on code point boundaries.
    QList<QByteArray> words;
    QByteArray chunk;
    for (qsizetype i = 0; i < line.size();) {
        const qsizetype width = line.at(i).isHighSurrogate() && i + 1 < line.size() ? 2 : 1;
        const QByteArray bytes = line.mid(i, width).toUtf8();
        if (chunk.size() + bytes.size() > 45) {
            words.append(chunk);
            chunk.clear();
        }
        chunk += bytes;
        i += width;
    }
    if (!chunk.isEmpty()) words.append(chunk);
    QByteArray out;
    for (const QByteArray &word : words) {
        if (!out.isEmpty()) out += "\r\n ";
        out += "=?UTF-8?B?" + word.toBase64() + "?=";
    }
    return out;
}

QString decodeHeaderText(const QByteArray &value)
{
    static const QRegularExpression word(QStringLiteral("=\\?([^?]+)\\?([BbQq])\\?([^?]*)\\?="));
    const QString latin = QString::fromLatin1(value);
    QString out;
    qsizetype last = 0;
    bool previousWasWord = false;
    auto it = word.globalMatch(latin);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QByteArray between = value.mid(last, match.capturedStart() - last);
        if (!(previousWasWord && between.trimmed().isEmpty())) out += QString::fromUtf8(between);
        QString charset = match.captured(1);
        charset = charset.left(charset.indexOf(QLatin1Char('*')) < 0 ? charset.size()
                                                                   : charset.indexOf(QLatin1Char('*')));
        const QByteArray payload = match.captured(3).toLatin1();
        QByteArray bytes;
        if (match.captured(2).compare(QLatin1String("B"), Qt::CaseInsensitive) == 0) {
            bytes = QByteArray::fromBase64(payload);
        } else {
            QByteArray q = payload;
            q.replace('_', ' ');
            bytes = decodeTransfer(q, QStringLiteral("quoted-printable"));
        }
        out += decodeText(bytes, charset);
        last = match.capturedEnd();
        previousWasWord = true;
    }
    out += QString::fromUtf8(value.mid(last));
    return out.trimmed();
}

QByteArray build(const Outgoing &message)
{
    const QDateTime date = (message.date.isValid() ? message.date : QDateTime::currentDateTimeUtc())
                               .toTimeZone(QTimeZone::UTC);
    QByteArray head = "Date: " + QLocale::c().toString(date, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss")).toLatin1()
        + " +0000\r\nFrom: " + formatAddress(message.from) + "\r\n";
    auto list = [](const QList<Address> &addresses) {
        QByteArray joined;
        for (const Address &address : addresses)
            joined += (joined.isEmpty() ? QByteArray() : QByteArrayLiteral(",\r\n ")) + formatAddress(address);
        return joined;
    };
    if (!message.to.isEmpty()) head += "To: " + list(message.to) + "\r\n";
    if (!message.cc.isEmpty()) head += "Cc: " + list(message.cc) + "\r\n";
    head += "Subject: " + encodeHeaderText(message.subject) + "\r\n";
    const QByteArray id = messageIdHeader(message.messageId);
    if (!id.isEmpty()) head += "Message-ID: " + id + "\r\n";
    const QByteArray reply = messageIdHeader(message.inReplyTo);
    if (!reply.isEmpty()) head += "In-Reply-To: " + reply + "\r\n";
    const QString references = singleLine(message.references);
    if (!references.isEmpty() && isAsciiPrintable(references))
        head += "References: " + references.toLatin1() + "\r\n";
    head += "MIME-Version: 1.0\r\n";

    QByteArray body = message.html.isEmpty()
        ? textLeaf(message.plain, "plain")
        : multipart("alternative", {textLeaf(message.plain, "plain"), textLeaf(message.html, "html")});
    if (!message.attachments.isEmpty()) {
        QList<QByteArray> parts{body};
        for (const OutgoingAttachment &attachment : message.attachments)
            parts.append(attachmentLeaf(attachment));
        body = multipart("mixed", parts);
    }
    return head + body;
}

QByteArray decodeTransfer(const QByteArray &body, const QString &encoding)
{
    const QString kind = encoding.trimmed().toLower();
    if (kind == QLatin1String("base64")) {
        QByteArray compact;
        compact.reserve(body.size());
        for (const char c : body)
            if (!QChar::isSpace(static_cast<unsigned char>(c))) compact += c;
        return QByteArray::fromBase64(compact);
    }
    if (kind != QLatin1String("quoted-printable")) return body;
    QByteArray out;
    out.reserve(body.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (qsizetype i = 0; i < body.size(); ++i) {
        const char c = body.at(i);
        if (c != '=') { out += c; continue; }
        if (i + 1 < body.size() && body.at(i + 1) == '\n') { i += 1; continue; }
        if (i + 2 < body.size() && body.at(i + 1) == '\r' && body.at(i + 2) == '\n') { i += 2; continue; }
        if (i + 2 < body.size() && hex(body.at(i + 1)) >= 0 && hex(body.at(i + 2)) >= 0) {
            out += static_cast<char>(hex(body.at(i + 1)) * 16 + hex(body.at(i + 2)));
            i += 2;
            continue;
        }
        out += c;
    }
    return out;
}

QString decodeText(const QByteArray &bytes, const QString &charset)
{
    const QString name = charset.trimmed().isEmpty() ? QStringLiteral("utf-8") : charset.trimmed();
    if (auto known = QStringConverter::encodingForName(name.toLatin1().constData())) {
        QStringDecoder decoder(*known);
        return decoder.decode(bytes);
    }
    QStringDecoder decoder(name.toLatin1().constData());
    if (decoder.isValid()) return decoder.decode(bytes);
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString text = utf8.decode(bytes);
    return utf8.hasError() ? QString::fromLatin1(bytes) : text;
}

} // namespace QindaTK::Mime
