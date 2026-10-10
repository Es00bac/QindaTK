// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMultiMap>
#include <QString>
#include <QStringList>

// RFC 5322 / MIME (RFC 2045-2047, 2231) message building and parsing for the
// mail providers. AGENT-CONTRACT (smtp_mail_provider.cpp, imap_mail_provider.cpp):
// build() output is CRLF-terminated and never contains a bare CR or LF;
// parse() section numbers follow IMAP BODY[section] numbering (RFC 3501 §6.4.5)
// so an attachment's `section` can be fetched later with UID FETCH.
namespace QindaTK::Mime {

struct Address {
    QString name;
    QString address;
};

bool validAddress(const QString &address);
// Accepts "Name <a@b>, c@d; ..."; fails on any invalid entry.
QList<Address> parseAddressList(const QString &text, QString *error);
QByteArray formatAddress(const Address &address);
QByteArray encodeHeaderText(const QString &text);    // RFC 2047 B-encoding when needed
QString decodeHeaderText(const QByteArray &value);   // RFC 2047 B and Q words

struct OutgoingAttachment {
    QString name;
    QString mimeType;
    QByteArray bytes;
};

struct Outgoing {
    Address from;
    QList<Address> to, cc;
    QList<Address> replyTo;       // Reply-To (D-342)
    QString subject;
    QString plain;
    QString html;                 // optional alternative part
    QList<OutgoingAttachment> attachments;
    QString messageId;            // without angle brackets
    QString inReplyTo, references;
    QDateTime date;
    // "high" or "low" writes Importance and X-Priority; "" or "normal" nothing.
    QString importance;
    // iMIP (RFC 6047, D-346): iCalendar text sent as the last alternative,
    // "text/calendar; charset=utf-8; method=<calendarMethod>". Both or neither.
    QByteArray calendar;
    QString calendarMethod;
};

QByteArray build(const Outgoing &message);

struct Part {
    QString section;              // IMAP section, "1" for a single-part message
    QString mimeType;
    QString name;
    QString encoding;             // Content-Transfer-Encoding, lower case
    QString charset;
    qint64 size = 0;              // decoded bytes
    bool attachment = false;
    QString contentId;            // Content-ID without angle brackets (cid: target)
    QString disposition;          // "inline", "attachment" or "" when none was given
    // Decoded payload (implicitly shared with the parse). POP3 has no
    // partial fetch, so Pop3MailProvider takes attachment bytes from here.
    QByteArray data;
};

struct Parsed {
    QMultiMap<QByteArray, QByteArray> headers;   // lower-case name -> unfolded raw value
    QString subject, from, fromAddress, to, cc, messageId, inReplyTo, references;
    QString replyTo;              // decoded Reply-To text ("" when absent)
    QString importance;           // "low", "normal" or "high" (importanceOf)
    QDateTime date;
    QString plain, html;
    QList<Part> attachments;
    // The first text/calendar part (else an application/ics or ".ics"
    // attachment): decoded text, its method (the Content-Type parameter or
    // the METHOD property, upper case) and its section. The part is also
    // listed in `attachments`.
    QString calendar, calendarMethod, calendarSection;
};

Parsed parse(const QByteArray &raw);
// "high", "low" or "normal" from Importance (RFC 2156), X-Priority (1-2
// high, 4-5 low), Priority (urgent / non-urgent) or X-MSMail-Priority.
QString importanceOf(const QMultiMap<QByteArray, QByteArray> &headers);
QByteArray decodeTransfer(const QByteArray &body, const QString &encoding);
QString decodeText(const QByteArray &bytes, const QString &charset);
QString htmlToPlain(const QString &html);

} // namespace QindaTK::Mime
