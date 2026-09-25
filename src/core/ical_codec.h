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
// RecurrenceService shape, `exceptions` as "yyyy-MM-ddTHH:mm:ss" local
// starts ("yyyy-MM-dd" for all-day series) and `overrides` as
// {recurrenceId, startLocal, endLocal, title, description, location} for
// RECURRENCE-ID instances (a cancelled instance becomes an exception). An
// RRULE outside that shape is kept verbatim in `recurrenceRaw` with
// `recurrenceRule` null, so the series survives a round trip.
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

// Maps the first master VEVENT/VTODO (no RECURRENCE-ID) and its overrides.
Mapped toRecord(const QByteArray &ics, const QString &defaultZone);
// Writes the record's mapped properties into `base` (keeping every other
// property and component, such as VALARM), rewrites the overridden instances
// the record lists and drops those it no longer lists, or builds a new
// VCALENDAR when `base` has no master component of that type.
QByteArray fromRecord(const QString &component, const QString &uid, const QVariantMap &record,
                      const QByteArray &base = {});
// SHA-256 over the mapped fields only; a changed digest means a local edit.
QString digest(const QString &component, const QVariantMap &record);

// ---- Whole calendars (ical_calendar.cpp): .ics files and feeds ----------

// One UID of a calendar: its master component, its RECURRENCE-ID overrides
// and the VTIMEZONEs they reference, as a standalone VCALENDAR (`ics`, the
// `remoteIcs` base for fromRecord) plus its mapping. `mapped.ok` is false
// (with `mapped.error`) for a component the mapping cannot use.
struct CalendarItem {
    Mapped mapped;
    QByteArray ics;
};

struct ParsedCalendar {
    bool ok = false;
    QString error;                 // not an iCalendar stream at all
    QString name;                  // X-WR-CALNAME, if any
    QList<CalendarItem> items;     // VEVENT and VTODO groups in source order
    QStringList warnings;          // unsupported components, missing UIDs, bounds
    bool truncated = false;        // more than maximumItems UIDs
};

// Splits a VCALENDAR stream (several concatenated calendars allowed) into
// per-UID items. A component without UID gets a stable "qindatk-<sha1>" UID
// derived from its text. TZIDs that are not IANA or Windows names resolve
// through the VTIMEZONE's X-LIC-LOCATION or an IANA suffix
// ("/mozilla.org/.../Europe/Berlin") and are rewritten to the IANA name.
ParsedCalendar parseCalendar(const QByteArray &text, const QString &defaultZone, int maximumItems = 20000);
// Joins standalone VCALENDAR texts (fromRecord output) into one VCALENDAR:
// VERSION:2.0, PRODID, CALSCALE, optional X-WR-CALNAME, each VTIMEZONE once
// (by TZID), then every other component in order.
QByteArray mergeCalendars(const QList<QByteArray> &calendars, const QString &name = {});

} // namespace QindaTK::ICal
