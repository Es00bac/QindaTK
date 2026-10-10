// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mail_record.h"

#include <QRegularExpression>
#include <QUrl>

namespace QindaTK::MailRecord {

QString threadRoot(const Mime::Parsed &parsed, const QString &fallback)
{
    static const QRegularExpression firstId(QStringLiteral("<([^>]+)>"));
    const QRegularExpressionMatch match = firstId.match(parsed.references);
    if (match.hasMatch()) return match.captured(1).trimmed();
    if (!parsed.inReplyTo.isEmpty()) return parsed.inReplyTo;
    return parsed.messageId.isEmpty() ? fallback : parsed.messageId;
}

QStringList referencedContentIds(const QString &html)
{
    static const QRegularExpression cid(QStringLiteral("cid:([^\"'\\s>)]+)"), QRegularExpression::CaseInsensitiveOption);
    QStringList ids;
    auto it = cid.globalMatch(html);
    while (it.hasNext() && ids.size() < 1000) {
        // RFC 2392: the URL form percent-encodes the Content-ID.
        const QString id = QUrl::fromPercentEncoding(it.next().captured(1).toUtf8()).toLower();
        if (!ids.contains(id)) ids.append(id);
    }
    return ids;
}

QVariantMap fromParsed(const QString &id, const Mime::Parsed &parsed, bool truncated)
{
    return fromParsed(id, parsed, truncated, Options());
}

QVariantMap fromParsed(const QString &id, const Mime::Parsed &parsed, bool truncated, const Options &options)
{
    const QStringList shown = referencedContentIds(parsed.html);
    QVariantList attachments;
    for (const Mime::Part &part : parsed.attachments)
        attachments.append(QVariantMap{
            {QStringLiteral("id"), id + QLatin1Char('/') + part.section},
            {QStringLiteral("name"), part.name}, {QStringLiteral("mimeType"), part.mimeType},
            {QStringLiteral("size"), part.size}, {QStringLiteral("section"), part.section},
            {QStringLiteral("encoding"), part.encoding}, {QStringLiteral("state"), QStringLiteral("remote")},
            {QStringLiteral("contentId"), part.contentId}, {QStringLiteral("disposition"), part.disposition},
            {QStringLiteral("inline"), !part.contentId.isEmpty() && shown.contains(part.contentId.toLower())}});
    QVariantMap record{
        {QStringLiteral("id"), id}, {QStringLiteral("messageId"), id},
        {QStringLiteral("internetMessageId"), parsed.messageId},
        {QStringLiteral("threadId"), threadRoot(parsed, id)},
        {QStringLiteral("sender"), parsed.from.isEmpty() ? parsed.fromAddress : parsed.from},
        {QStringLiteral("senderAddress"), parsed.fromAddress},
        {QStringLiteral("to"), parsed.to}, {QStringLiteral("cc"), parsed.cc},
        {QStringLiteral("replyTo"), parsed.replyTo},
        {QStringLiteral("subject"), parsed.subject},
        {QStringLiteral("date"), parsed.date.isValid() ? parsed.date.toUTC().toString(Qt::ISODate) : QString()},
        {QStringLiteral("importance"), parsed.importance.isEmpty() ? QStringLiteral("normal") : parsed.importance},
        {QStringLiteral("snippet"), parsed.plain.simplified().left(160)},
        {QStringLiteral("bodyPlain"), parsed.plain},
        {QStringLiteral("bodyTrustMode"), QStringLiteral("plain")},
        {QStringLiteral("bodyTruncated"), truncated},
        {QStringLiteral("attachments"), attachments}};
    if (!parsed.calendarSection.isEmpty()) {
        record.insert(QStringLiteral("calendarMethod"), parsed.calendarMethod);
        record.insert(QStringLiteral("calendarAttachmentId"), id + QLatin1Char('/') + parsed.calendarSection);
        if (parsed.calendar.size() <= MaximumCalendarText) record.insert(QStringLiteral("calendarText"), parsed.calendar);
    }
    if (options.keepHtml && !parsed.html.isEmpty()) {
        if (parsed.html.size() <= MaximumHtml) {
            record.insert(QStringLiteral("bodyHtml"), parsed.html);
            record.insert(QStringLiteral("bodyTrustMode"), QStringLiteral("untrusted-html"));
        } else {
            record.insert(QStringLiteral("bodyHtmlOmitted"), true);
        }
    }
    return record;
}

} // namespace QindaTK::MailRecord
