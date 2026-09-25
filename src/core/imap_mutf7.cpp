// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_mutf7.h"

#include <QList>

namespace QindaTK::Imap {
namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

int sextet(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == ',') return 63;
    return -1;
}

bool directlyEncoded(char16_t c) { return c >= 0x20 && c <= 0x7e && c != u'&'; }

void flush(QByteArray *out, const QList<char16_t> &run)
{
    if (run.isEmpty()) return;
    QByteArray bytes;
    bytes.reserve(run.size() * 2);
    for (const char16_t unit : run) {
        bytes += static_cast<char>(unit >> 8);
        bytes += static_cast<char>(unit & 0xff);
    }
    *out += '&';
    quint32 buffer = 0;
    int bits = 0;
    for (const char byte : std::as_const(bytes)) {
        buffer = (buffer << 8) | static_cast<unsigned char>(byte);
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            *out += kAlphabet[(buffer >> bits) & 0x3f];
        }
    }
    if (bits > 0) *out += kAlphabet[(buffer << (6 - bits)) & 0x3f];
    *out += '-';
}

// Decodes the base64 run between '&' and '-'; false when malformed.
bool decodeRun(const QByteArray &run, QString *out)
{
    quint32 buffer = 0;
    int bits = 0;
    QByteArray bytes;
    for (const char c : run) {
        const int value = sextet(c);
        if (value < 0) return false;
        buffer = (buffer << 6) | static_cast<quint32>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes += static_cast<char>((buffer >> bits) & 0xff);
        }
    }
    // RFC 3501: leftover bits must be zero padding shorter than one byte.
    if (bits >= 6 || (buffer & ((1u << bits) - 1)) != 0 || bytes.isEmpty() || bytes.size() % 2 != 0)
        return false;
    QString text;
    for (qsizetype i = 0; i < bytes.size(); i += 2)
        text += QChar(static_cast<char16_t>((static_cast<unsigned char>(bytes.at(i)) << 8)
                                            | static_cast<unsigned char>(bytes.at(i + 1))));
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        // A run must not encode what could be written directly, and every
        // surrogate must be paired.
        if (directlyEncoded(c.unicode())) return false;
        if (c.isHighSurrogate()) {
            if (i + 1 >= text.size() || !text.at(i + 1).isLowSurrogate()) return false;
            ++i;
        } else if (c.isLowSurrogate()) {
            return false;
        }
    }
    *out += text;
    return true;
}
} // namespace

QByteArray encodeMailboxName(const QString &name)
{
    QByteArray out;
    QList<char16_t> run;
    for (const QChar c : name) {
        const char16_t unit = c.unicode();
        if (directlyEncoded(unit) || unit == u'&') {
            flush(&out, run);
            run.clear();
            out += unit == u'&' ? QByteArrayLiteral("&-") : QByteArray(1, static_cast<char>(unit));
        } else {
            run.append(unit);
        }
    }
    flush(&out, run);
    return out;
}

QString decodeMailboxName(const QByteArray &wire, bool *ok)
{
    QString out;
    bool valid = true;
    qsizetype pos = 0;
    while (pos < wire.size() && valid) {
        const char c = wire.at(pos);
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) {
            valid = false;
        } else if (c != '&') {
            out += QLatin1Char(c);
            ++pos;
        } else {
            const qsizetype end = wire.indexOf('-', pos + 1);
            if (end < 0) {
                valid = false;
            } else if (end == pos + 1) {
                out += QLatin1Char('&');
                pos = end + 1;
            } else {
                valid = decodeRun(wire.mid(pos + 1, end - pos - 1), &out);
                pos = end + 1;
            }
        }
    }
    if (ok) *ok = valid;
    return valid ? out : QString::fromUtf8(wire);
}

} // namespace QindaTK::Imap
