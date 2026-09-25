// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>

// iCalendar (RFC 5545) subset mapping VEVENT/VTODO to LocalProductivityStore
// event and task records for CalDavProvider.
// AGENT-CONTRACT (local_calendar_projection_model.cpp, recurrence_service.cpp):
// events use startLocal/endLocal wall times ("yyyy-MM-dd HH:mm[:ss]", or
// "yyyy-MM-dd" when allDay), an IANA `zone`, `recurrenceRule` in the
// RecurrenceService shape, and `exceptions` as "yyyy-MM-ddTHH:mm:ss" local
// starts. An RRULE outside that shape is kept verbatim in `recurrenceRaw`
// with `recurrenceRule` null, so the series survives a round trip.
namespace QindaTK::ICal {

struct Property {
    QString name;                              // upper case
    QList<QPair<QString, QString>> params;     // upper-case names
    QString value;                             // still escaped
    QString raw;                               // unfolded source line
    QString param(const QString &name) const;
};

QList<Property> parseLines(const QByteArray &text);
QByteArray foldLine(const QString &line);      // CRLF-terminated, folded at 75 octets
QString escapeText(const QString &text);
QString unescapeText(const QString &text);

struct Mapped {
    bool ok = false;
    QString error;
    QString component;                         // "VEVENT" or "VTODO"
    QString uid;
    QVariantMap fields;
};

// Maps the first master VEVENT/VTODO (no RECURRENCE-ID).
Mapped toRecord(const QByteArray &ics, const QString &defaultZone);
// Writes the record's mapped properties into `base` (keeping every other
// property and component, such as VALARM or overridden instances), or a new
// VCALENDAR when `base` has no master component of that type.
QByteArray fromRecord(const QString &component, const QString &uid, const QVariantMap &record,
                      const QByteArray &base = {});
// SHA-256 over the mapped fields only; a changed digest means a local edit.
QString digest(const QString &component, const QVariantMap &record);

} // namespace QindaTK::ICal
