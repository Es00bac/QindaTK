// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mail_mime.h"

#include <QMap>
#include <QRegularExpression>
#include <algorithm>

namespace QindaTK::Mime {
namespace {
constexpr int MaximumDepth = 8;

struct Split {
    QByteArray head, body;
};

Split splitHeaderBody(const QByteArray &raw)
{
    const qsizetype crlf = raw.indexOf("\r\n\r\n");
    const qsizetype lf = raw.indexOf("\n\n");
    if (crlf < 0 && lf < 0) return {raw, {}};
    if (crlf >= 0 && (lf < 0 || crlf < lf)) return {raw.left(crlf), raw.mid(crlf + 4)};
    return {raw.left(lf), raw.mid(lf + 2)};
}

QMultiMap<QByteArray, QByteArray> parseHeaders(const QByteArray &head)
{
    QMultiMap<QByteArray, QByteArray> headers;
    QByteArray name, value;
    auto flush = [&] {
        if (!name.isEmpty()) headers.insert(name.trimmed().toLower(), value.trimmed());
        name.clear();
        value.clear();
    };
    for (QByteArray line : head.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty() && (line.at(0) == ' ' || line.at(0) == '\t')) {
            value += ' ' + line.trimmed();          // RFC 5322 unfolding
            continue;
        }
        flush();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) continue;
        name = line.left(colon);
        value = line.mid(colon + 1);
    }
    flush();
    return headers;
}

struct HeaderValue {
    QString main;
    QMap<QString, QString> params;
};

QString percentDecode(const QString &value, const QString &charset)
{
    return decodeText(QByteArray::fromPercentEncoding(value.toLatin1()), charset);
}

// Content-Type / Content-Disposition with quoted strings and RFC 2231
// extended (name*=charset''value) and continued (name*0*, name*1) parameters.
HeaderValue parseStructured(const QByteArray &raw)
{
    HeaderValue out;
    QList<QString> pieces;
    QString current;
    bool quoted = false, escaped = false;
    for (const QChar c : QString::fromUtf8(raw)) {
        if (escaped) { current += c; escaped = false; continue; }
        if (quoted && c == QLatin1Char('\\')) { escaped = true; continue; }
        if (c == QLatin1Char('"')) { quoted = !quoted; continue; }
        if (!quoted && c == QLatin1Char(';')) { pieces.append(current.trimmed()); current.clear(); continue; }
        current += c;
    }
    pieces.append(current.trimmed());
    out.main = pieces.isEmpty() ? QString() : pieces.takeFirst().toLower();
    struct Segment { int index; bool extended; QString value; };
    QMap<QString, QList<Segment>> continued;
    for (const QString &piece : std::as_const(pieces)) {
        const qsizetype eq = piece.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        QString key = piece.left(eq).trimmed().toLower();
        const QString value = piece.mid(eq + 1).trimmed();
        bool extended = key.endsWith(QLatin1Char('*'));
        if (extended) key.chop(1);
        int index = -1;
        const qsizetype star = key.indexOf(QLatin1Char('*'));
        if (star > 0) {
            index = key.mid(star + 1).toInt();
            key = key.left(star);
        }
        continued[key].append({index, extended, value});
    }
    for (auto it = continued.begin(); it != continued.end(); ++it) {
        QList<Segment> segments = it.value();
        std::sort(segments.begin(), segments.end(),
                  [](const Segment &a, const Segment &b) { return a.index < b.index; });
        QString charset, joined;
        bool anyExtended = false;
        for (int i = 0; i < segments.size(); ++i) {
            QString value = segments.at(i).value;
            if (segments.at(i).extended && i == 0) {
                const QStringList parts = value.split(QLatin1Char('\''));
                if (parts.size() >= 3) {
                    charset = parts.at(0);
                    value = parts.mid(2).join(QLatin1Char('\''));
                }
            }
            if (segments.at(i).extended) {
                anyExtended = true;
                joined += percentDecode(value, charset);
            } else {
                joined += value;
            }
        }
        out.params.insert(it.key(), anyExtended ? joined : decodeHeaderText(joined.toUtf8()));
    }
    return out;
}

QList<QByteArray> splitMultipart(const QByteArray &body, const QByteArray &boundary)
{
    QList<QByteArray> parts;
    const QByteArray delimiter = "--" + boundary;
    qsizetype start = body.startsWith(delimiter) ? 0 : body.indexOf("\n" + delimiter);
    if (start < 0) return parts;
    if (!body.startsWith(delimiter)) start += 1;       // skip the '\n' before it
    for (;;) {
        if (body.mid(start + delimiter.size(), 2) == "--") break;   // closing delimiter
        const qsizetype lineEnd = body.indexOf('\n', start);
        if (lineEnd < 0) break;
        const qsizetype contentStart = lineEnd + 1;
        const qsizetype next = body.indexOf("\n" + delimiter, lineEnd);
        if (next < 0) {
            parts.append(body.mid(contentStart));
            break;
        }
        qsizetype contentEnd = next;
        if (contentEnd > contentStart && body.at(contentEnd - 1) == '\r') --contentEnd;
        parts.append(body.mid(contentStart, std::max<qsizetype>(0, contentEnd - contentStart)));
        start = next + 1;
    }
    return parts;
}

QString normalizeNewlines(QString text)
{
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

void walk(const QMultiMap<QByteArray, QByteArray> &headers, const QByteArray &body,
          const QString &section, int depth, Parsed *parsed)
{
    const HeaderValue type = parseStructured(headers.value("content-type", "text/plain"));
    const QString mimeType = type.main.isEmpty() ? QStringLiteral("text/plain") : type.main;
    if (mimeType.startsWith(QLatin1String("multipart/")) && depth < MaximumDepth) {
        const QString mark = type.params.value(QStringLiteral("boundary"));
        if (!mark.isEmpty()) {
            const QList<QByteArray> parts = splitMultipart(body, mark.toUtf8());
            for (int i = 0; i < parts.size(); ++i) {
                const Split child = splitHeaderBody(parts.at(i));
                const QString childSection = section.isEmpty()
                    ? QString::number(i + 1) : section + QLatin1Char('.') + QString::number(i + 1);
                walk(parseHeaders(child.head), child.body, childSection, depth + 1, parsed);
            }
            return;
        }
    }
    const QString leafSection = section.isEmpty() ? QStringLiteral("1") : section;
    const QString encoding = QString::fromLatin1(headers.value("content-transfer-encoding")).trimmed().toLower();
    const HeaderValue disposition = parseStructured(headers.value("content-disposition"));
    QString name = disposition.params.value(QStringLiteral("filename"));
    if (name.isEmpty()) name = type.params.value(QStringLiteral("name"));
    const QString charset = type.params.value(QStringLiteral("charset"));
    const bool isPlain = mimeType == QLatin1String("text/plain");
    const bool isHtml = mimeType == QLatin1String("text/html");
    const bool attachment = disposition.main == QLatin1String("attachment") || !name.isEmpty()
        || !(isPlain || isHtml);
    const QByteArray decoded = decodeTransfer(body, encoding);
    if (!attachment && isPlain && parsed->plain.isEmpty()) {
        parsed->plain = normalizeNewlines(decodeText(decoded, charset));
        return;
    }
    if (!attachment && isHtml && parsed->html.isEmpty()) {
        parsed->html = decodeText(decoded, charset);
        return;
    }
    if (!attachment) return;          // a second alternative body adds nothing new
    Part part;
    part.section = leafSection;
    part.mimeType = mimeType;
    part.name = name.isEmpty() ? QStringLiteral("part-%1").arg(leafSection) : name;
    part.encoding = encoding;
    part.charset = charset;
    part.size = decoded.size();
    part.attachment = true;
    part.data = decoded;
    parsed->attachments.append(part);
}

void firstAddress(const QString &decoded, QString *name, QString *address)
{
    const qsizetype open = decoded.indexOf(QLatin1Char('<'));
    const qsizetype close = decoded.indexOf(QLatin1Char('>'), open + 1);
    if (open >= 0 && close > open) {
        *address = decoded.mid(open + 1, close - open - 1).trimmed();
        QString display = decoded.left(open).trimmed();
        if (display.size() >= 2 && display.startsWith(QLatin1Char('"')) && display.endsWith(QLatin1Char('"')))
            display = display.mid(1, display.size() - 2);
        *name = display.isEmpty() ? *address : display;
    } else {
        *address = decoded.section(QLatin1Char(','), 0, 0).trimmed();
        *name = *address;
    }
}

QString bareId(const QByteArray &value)
{
    QString id = QString::fromLatin1(value).trimmed();
    const qsizetype open = id.indexOf(QLatin1Char('<'));
    const qsizetype close = id.indexOf(QLatin1Char('>'), open + 1);
    if (open >= 0 && close > open) id = id.mid(open + 1, close - open - 1);
    return id.trimmed();
}
} // namespace

Parsed parse(const QByteArray &raw)
{
    Parsed parsed;
    const Split split = splitHeaderBody(raw);
    parsed.headers = parseHeaders(split.head);
    parsed.subject = decodeHeaderText(parsed.headers.value("subject"));
    parsed.to = decodeHeaderText(parsed.headers.value("to"));
    parsed.cc = decodeHeaderText(parsed.headers.value("cc"));
    firstAddress(decodeHeaderText(parsed.headers.value("from")), &parsed.from, &parsed.fromAddress);
    parsed.messageId = bareId(parsed.headers.value("message-id"));
    parsed.inReplyTo = bareId(parsed.headers.value("in-reply-to"));
    parsed.references = QString::fromLatin1(parsed.headers.value("references")).simplified();
    QString date = QString::fromLatin1(parsed.headers.value("date")).trimmed();
    date.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)\\s*$")));   // trailing "(UTC)" comment
    parsed.date = QDateTime::fromString(date, Qt::RFC2822Date);
    walk(parsed.headers, split.body, QString(), 0, &parsed);
    if (parsed.plain.isEmpty() && !parsed.html.isEmpty()) parsed.plain = htmlToPlain(parsed.html);
    return parsed;
}

QString htmlToPlain(const QString &html)
{
    QString text = html;
    static const QRegularExpression hidden(QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression breaks(QStringLiteral("<(br|/p|/div|/li|/tr|/h[1-6])\\b[^>]*>"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression numeric(QStringLiteral("&#(x?)([0-9A-Fa-f]+);"));
    static const QRegularExpression blank(QStringLiteral("\\n{3,}"));
    text.remove(hidden);
    text.replace(breaks, QStringLiteral("\n"));
    text.remove(tags);
    QString decoded;
    qsizetype last = 0;
    auto it = numeric.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        decoded += text.mid(last, match.capturedStart() - last);
        bool ok = false;
        const char32_t code = match.captured(2).toUInt(&ok, match.captured(1).isEmpty() ? 10 : 16);
        decoded += ok && code > 0 && code <= 0x10FFFF ? QString::fromUcs4(&code, 1) : match.captured(0);
        last = match.capturedEnd();
    }
    decoded += text.mid(last);
    decoded.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    decoded.replace(QLatin1String("&lt;"), QLatin1String("<"));
    decoded.replace(QLatin1String("&gt;"), QLatin1String(">"));
    decoded.replace(QLatin1String("&quot;"), QLatin1String("\""));
    decoded.replace(QLatin1String("&#39;"), QLatin1String("'"));
    decoded.replace(QLatin1String("&amp;"), QLatin1String("&"));
    decoded = normalizeNewlines(decoded);
    decoded.replace(blank, QStringLiteral("\n\n"));
    return decoded.trimmed();
}

} // namespace QindaTK::Mime
