// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Recipient parsing for RichMailComposer's token fields, using the same
// RFC 5322 rules SmtpMailProvider applies when it sends (Mime::parseAddressList),
// so a recipient the composer accepts is one the provider accepts.
class MailAddressValidator : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit MailAddressValidator(QObject *parent = nullptr);
    // One entry ("Name <a@b>" or "a@b"): {valid, name, address, display, error}.
    Q_INVOKABLE QVariantMap parseOne(const QString &text) const;
    // A comma/semicolon separated list: {ok, error, recipients: [parseOne...]}.
    // Every entry is reported, valid or not, so a view can mark each token.
    Q_INVOKABLE QVariantMap parseList(const QString &text) const;
};

} // namespace QindaTK
