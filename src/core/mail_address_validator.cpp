// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mail_address_validator.h"

#include "mail_mime.h"

#include <QVariantList>

namespace QindaTK {

MailAddressValidator::MailAddressValidator(QObject *parent) : QObject(parent) {}

QVariantMap MailAddressValidator::parseOne(const QString &text) const
{
    const QString trimmed = text.trimmed();
    QString error;
    const QList<Mime::Address> parsed = Mime::parseAddressList(trimmed, &error);
    if (trimmed.isEmpty() || parsed.size() != 1 || !error.isEmpty()) {
        return {{QStringLiteral("valid"), false}, {QStringLiteral("display"), trimmed},
                {QStringLiteral("address"), QString()}, {QStringLiteral("name"), QString()},
                {QStringLiteral("error"), error.isEmpty() ? tr("Enter one address such as name@example.com") : error}};
    }
    const Mime::Address &address = parsed.first();
    const QString display = address.name.isEmpty()
        ? address.address : address.name + QStringLiteral(" <") + address.address + QLatin1Char('>');
    return {{QStringLiteral("valid"), true}, {QStringLiteral("display"), display},
            {QStringLiteral("address"), address.address}, {QStringLiteral("name"), address.name},
            {QStringLiteral("error"), QString()}};
}

QVariantMap MailAddressValidator::parseList(const QString &text) const
{
    QVariantList recipients;
    QStringList errors;
    QString current;
    bool quoted = false;
    int angle = 0;
    const auto flush = [&] {
        if (!current.trimmed().isEmpty()) {
            const QVariantMap entry = parseOne(current);
            if (!entry.value(QStringLiteral("valid")).toBool())
                errors.append(entry.value(QStringLiteral("display")).toString());
            recipients.append(entry);
        }
        current.clear();
    };
    // Split on separators outside quotes and angle brackets, as the RFC
    // 5322 display-name form allows "Lovelace, Ada" <ada@example.test>.
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) quoted = !quoted;
        else if (!quoted && c == QLatin1Char('<')) ++angle;
        else if (!quoted && c == QLatin1Char('>') && angle > 0) --angle;
        if (!quoted && angle == 0 && (c == QLatin1Char(',') || c == QLatin1Char(';'))) flush();
        else current += c;
    }
    flush();
    return {{QStringLiteral("ok"), errors.isEmpty()}, {QStringLiteral("recipients"), recipients},
            {QStringLiteral("error"), errors.isEmpty() ? QString()
                                                       : tr("Invalid recipient: %1").arg(errors.join(QStringLiteral(", ")))}};
}

} // namespace QindaTK
