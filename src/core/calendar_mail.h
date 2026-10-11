// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// RFC 6047 iMIP conversion only: no network, store mutation or implicit
// acceptance. The caller decides whether the sender is trusted before
// changing calendar data, and sends reviewed bytes through sendPrepared.
class CalendarMail : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;
    // Parses an RFC 5322 message into method, calendar, event, organizer,
    // attendees and sequence. Result.authenticated is always false: MIME
    // and iCalendar identities are assertions, not sender authentication.
    Q_INVOKABLE QVariantMap parse(const QByteArray &message) const;
    // method REQUEST/REPLY/CANCEL; draft from/to/cc/replyTo/subject/body/
    // bodyHtml are mail headers/content. Result.message is prepared MIME.
    Q_INVOKABLE QVariantMap compose(const QString &method, const QByteArray &calendar,
                                    const QVariantMap &draft) const;
    // Builds a REPLY calendar for one existing attendee, preserving UID,
    // recurrence identity and sequence. partstat: ACCEPTED/TENTATIVE/DECLINED.
    Q_INVOKABLE QVariantMap reply(const QByteArray &calendar, const QString &address,
                                  const QString &partstat) const;
};
}
