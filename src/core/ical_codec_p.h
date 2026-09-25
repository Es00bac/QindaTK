// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Internal helpers shared by ical_codec.cpp and ical_overrides.cpp.
#include "ical_codec.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

namespace QindaTK::ICal::detail {

struct Stamp {
    bool ok = false, allDay = false;
    QDate date;
    QTime time;
    QString zone;
    QDateTime zoned() const
    {
        return QDateTime(date, allDay ? QTime(0, 0) : time, QTimeZone(zone.toUtf8()),
                         QDateTime::TransitionResolution::PreferBefore);
    }
};

struct Block {
    int begin = -1, end = -1, overrides = 0;
};

const QStringList &managed(const QString &component);
QString resolveZone(const QString &tzid);
Stamp parseStampValue(QString value, const Property &property, const QString &defaultZone);
QString localText(const QDate &date, const QTime &time, bool allDay);
bool parseLocal(const QString &text, QDate *date, QTime *time);
qint64 durationSeconds(const QString &value);
QString stampLine(const QString &name, const QDate &date, const QTime &time, const QString &zone, bool allDay);
Block findMaster(const QList<Property> &lines, const QString &component);

// Overridden instances (VEVENTs with RECURRENCE-ID) of the master `uid`:
// fills fields["overrides"] and appends cancelled instances to
// fields["exceptions"]. `fields` already holds the master's mapping.
void readOverrides(const QList<Property> &lines, const QString &uid, QVariantMap *fields);
// Serialises `component` for `record`: the master gets `masterLines` (other
// properties and components of `base` are kept) and every record override is
// written as a RECURRENCE-ID component; overrides the record no longer has
// are dropped.
QByteArray writeCalendar(const QString &component, const QString &uid, const QVariantMap &record,
                         const QByteArray &base, const QStringList &masterLines);

} // namespace QindaTK::ICal::detail
