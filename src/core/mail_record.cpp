// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mail_record.h"

#include <QRegularExpression>

namespace QindaTK::MailRecord {

QString threadRoot(const Mime::Parsed &parsed, const QString &fallback)
{
    static const QRegularExpression firstId(QStringLiteral("<([^>]+)>"));
    const QRegularExpressionMatch match = firstId.match(parsed.references);
    if (match.hasMatch()) return match.captured(1).trimmed();
    if (!parsed.inReplyTo.isEmpty()) return parsed.inReplyTo;
    return parsed.messageId.isEmpty() ? fallback : parsed.messageId;
}

QVariantMap fromParsed(const QString &id, const Mime::Parsed &parsed, bool truncated)
{
    QVariantList attachments;
    for (const Mime::Part &part : parsed.attachments)
        attachments.append(QVariantMap{
            {QStringLiteral("id"), id + QLatin1Char('/') + part.section},
            {QStringLiteral("name"), part.name}, {QStringLiteral("mimeType"), part.mimeType},
            {QStringLiteral("size"), part.size}, {QStringLiteral("section"), part.section},
            {QStringLiteral("encoding"), part.encoding}, {QStringLiteral("state"), QStringLiteral("remote")}});
    return {
        {QStringLiteral("id"), id}, {QStringLiteral("messageId"), id},
        {QStringLiteral("internetMessageId"), parsed.messageId},
        {QStringLiteral("threadId"), threadRoot(parsed, id)},
        {QStringLiteral("sender"), parsed.from.isEmpty() ? parsed.fromAddress : parsed.from},
        {QStringLiteral("senderAddress"), parsed.fromAddress},
        {QStringLiteral("to"), parsed.to}, {QStringLiteral("cc"), parsed.cc},
        {QStringLiteral("subject"), parsed.subject},
        {QStringLiteral("date"), parsed.date.isValid() ? parsed.date.toUTC().toString(Qt::ISODate) : QString()},
        {QStringLiteral("snippet"), parsed.plain.simplified().left(160)},
        {QStringLiteral("bodyPlain"), parsed.plain},
        {QStringLiteral("bodyTrustMode"), QStringLiteral("plain")},
        {QStringLiteral("bodyTruncated"), truncated},
        {QStringLiteral("attachments"), attachments}};
}

} // namespace QindaTK::MailRecord
