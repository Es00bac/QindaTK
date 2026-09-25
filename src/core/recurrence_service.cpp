// SPDX-License-Identifier: LGPL-3.0-or-later
#include "recurrence_service.h"

#include <QDateTime>
#include <QSet>
#include <QTimeZone>

namespace QindaTK {
namespace {
constexpr int kMaxSearchDays = 36525; // bounded to a century of local dates

bool selectedDay(const QDate &date, const QDate &first, const QString &frequency,
                 int interval, const QSet<int> &weekdays)
{
    const qint64 days = first.daysTo(date);
    if (days < 0) return false;
    if (frequency == QLatin1String("daily")) return days % interval == 0;
    if (frequency == QLatin1String("weekly")) {
        const QDate firstMonday = first.addDays(1 - first.dayOfWeek());
        return firstMonday.daysTo(date) / 7 % interval == 0
            && (weekdays.isEmpty() ? date.dayOfWeek() == first.dayOfWeek()
                                   : weekdays.contains(date.dayOfWeek()));
    }
    // Monthly/yearly match the start's day number (and month); a month or
    // year without that date (the 31st, 29 February) is skipped, not clamped.
    const int months = (date.year() - first.year()) * 12 + date.month() - first.month();
    if (frequency == QLatin1String("monthly"))
        return months % interval == 0 && date.day() == first.day();
    if (frequency == QLatin1String("yearly"))
        return (date.year() - first.year()) % interval == 0
            && date.month() == first.month() && date.day() == first.day();
    return false;
}
} // namespace

RecurrenceService::RecurrenceService(QObject *parent) : QObject(parent) {}

void RecurrenceService::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

bool RecurrenceService::parseLocal(const QString &text, QDate *date, QTime *time, bool *dateOnly)
{
    const QString value = text.trimmed();
    *date = QDate::fromString(value.left(10), Qt::ISODate);
    *time = QTime(0, 0);
    *dateOnly = value.size() == 10;
    if (!*dateOnly) {
        if (value.size() < 16 || (value.at(10) != QLatin1Char('T') && value.at(10) != QLatin1Char(' ')))
            return false;
        const QString clock = value.mid(11, 8);
        *time = QTime::fromString(clock, clock.size() == 8 ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
    }
    return date->isValid() && time->isValid();
}

qint64 RecurrenceService::spanSeconds(const QString &startLocal, const QString &endLocal,
                                      const QString &zoneId)
{
    QDate startDate, endDate;
    QTime startTime, endTime;
    bool startDateOnly = false, endDateOnly = false;
    const QTimeZone zone(zoneId.toUtf8());
    if (!zone.isValid() || !parseLocal(startLocal, &startDate, &startTime, &startDateOnly))
        return -1;
    if (endLocal.trimmed().isEmpty()) return startDateOnly ? 86400 : 0;
    if (!parseLocal(endLocal, &endDate, &endTime, &endDateOnly)) return -1;
    if (startDateOnly) return qMax<qint64>(1, startDate.daysTo(endDate)) * 86400;
    const QDateTime start(startDate, startTime, zone, QDateTime::TransitionResolution::PreferBefore);
    const QDateTime end(endDate, endTime, zone, QDateTime::TransitionResolution::PreferBefore);
    const qint64 seconds = start.secsTo(end);
    return start.isValid() && end.isValid() && seconds > 0 ? seconds : -1;
}

QString RecurrenceService::canonicalLocal(const QString &text)
{
    QDate date;
    QTime time;
    bool dateOnly = false;
    if (!parseLocal(text, &date, &time, &dateOnly)) return {};
    return dateOnly ? date.toString(Qt::ISODate)
                    : date.toString(Qt::ISODate) + QLatin1Char('T') + time.toString(QStringLiteral("HH:mm:ss"));
}

QVariantList RecurrenceService::expand(const QString &eventId, const QString &startLocal,
                                       int durationSeconds, const QString &zoneId,
                                       const QVariantMap &rule, const QString &rangeStartUtc,
                                       const QString &rangeEndUtc, int limit)
{
    setError({});
    const QTimeZone zone(zoneId.toUtf8());
    QDate firstDate;
    QTime wallTime;
    bool allDay = false;
    const bool localOk = parseLocal(startLocal, &firstDate, &wallTime, &allDay);
    const QDateTime from = QDateTime::fromString(rangeStartUtc, Qt::ISODate).toUTC();
    const QDateTime to = QDateTime::fromString(rangeEndUtc, Qt::ISODate).toUTC();
    const QString frequency = rule.value(QStringLiteral("frequency"), QStringLiteral("daily")).toString().toLower();
    const int interval = rule.value(QStringLiteral("interval"), 1).toInt();
    const QString endMode = rule.value(QStringLiteral("endMode"), QStringLiteral("never")).toString();
    const int countLimit = rule.value(QStringLiteral("count")).toInt();
    const QDate until = QDate::fromString(rule.value(QStringLiteral("until")).toString(), Qt::ISODate);
    if (eventId.isEmpty() || !zone.isValid() || !localOk || !from.isValid() || !to.isValid()
        || rangeStartUtc.isEmpty() || rangeEndUtc.isEmpty() || from >= to
        || durationSeconds < 0 || interval < 1 || limit < 1 || limit > 1000
        || (frequency != QLatin1String("daily") && frequency != QLatin1String("weekly")
            && frequency != QLatin1String("monthly") && frequency != QLatin1String("yearly"))
        || (endMode != QLatin1String("never") && endMode != QLatin1String("count")
            && endMode != QLatin1String("until"))
        || (endMode == QLatin1String("count") && countLimit < 1)
        || (endMode == QLatin1String("until") && !until.isValid())) {
        setError(QStringLiteral("Invalid event, zone, recurrence rule or UTC range."));
        return {};
    }

    QSet<int> weekdays;
    for (const QVariant &day : rule.value(QStringLiteral("weekdays")).toList()) {
        const int number = day.toInt();
        if (number < 1 || number > 7) {
            setError(QStringLiteral("Weekdays must be numbered 1 through 7."));
            return {};
        }
        weekdays.insert(number);
    }
    QSet<QString> exceptions;
    for (const QVariant &entry : rule.value(QStringLiteral("exceptions")).toList()) {
        QString text = entry.toString();
        exceptions.insert(text);
        // "yyyy-MM-dd HH:mm:ss" and "...THH:mm:ss" name the same local start.
        if (text.size() >= 19 && text.at(text.size() - 9) == QLatin1Char(' '))
            exceptions.insert(text.replace(text.size() - 9, 1, QLatin1Char('T')));
    }
    const int allDayCount = qMax(1, durationSeconds / 86400);

    const QDate lastDate = to.toTimeZone(zone).date().addDays(1);
    QVariantList rows;
    int generated = 0;
    int searched = 0;
    QDate day = firstDate;
    for (; day.isValid() && day <= lastDate && searched < kMaxSearchDays;
         day = day.addDays(1), ++searched) {
        if (endMode == QLatin1String("until") && day > until) break;
        if (!selectedDay(day, firstDate, frequency, interval, weekdays)) continue;
        QDateTime start, end;
        QString localText;
        if (allDay) {
            // An all-day occurrence is the whole local date, however long it is.
            start = day.startOfDay(zone);
            end = day.addDays(allDayCount).startOfDay(zone);
            localText = day.toString(Qt::ISODate);
        } else {
            // AGENT-GUARD: preserve the requested local wall time across DST; skip
            // a spring-forward gap rather than silently moving an occurrence.
            start = QDateTime(day, wallTime, zone, QDateTime::TransitionResolution::PreferBefore);
            if (!start.isValid() || start.date() != day || start.time() != wallTime) continue;
            end = start.addSecs(durationSeconds);
            localText = day.toString(Qt::ISODate) + QLatin1Char('T')
                + wallTime.toString(QStringLiteral("HH:mm:ss"));
        }
        ++generated;
        if (endMode == QLatin1String("count") && generated > countLimit) break;
        const QString occurrenceId = eventId + QLatin1Char('/') + localText;
        if (exceptions.contains(occurrenceId) || exceptions.contains(localText)
            || (allDay && (exceptions.contains(localText + QStringLiteral("T00:00:00"))
                           || exceptions.contains(occurrenceId + QStringLiteral("T00:00:00")))))
            continue;
        const QDateTime utc = start.toUTC();
        if (utc >= to || end.toUTC() <= from) continue;
        rows.append(QVariantMap{{QStringLiteral("eventId"), eventId},
                                {QStringLiteral("occurrenceId"), occurrenceId},
                                {QStringLiteral("localStart"), localText},
                                {QStringLiteral("zone"), zoneId},
                                {QStringLiteral("allDay"), allDay},
                                {QStringLiteral("startUtc"), utc.toString(Qt::ISODateWithMs)},
                                {QStringLiteral("endUtc"), end.toUTC().toString(Qt::ISODateWithMs)}});
        if (rows.size() >= limit) break;
    }
    if (searched == kMaxSearchDays && day.isValid() && day <= lastDate)
        setError(QStringLiteral("Recurrence search reached its 100-year date bound."));
    return rows;
}

QVariantMap RecurrenceService::occurrenceAt(const QString &eventId, const QString &startLocal,
                                            int durationSeconds, const QString &zoneId,
                                            const QVariantMap &rule, const QString &occurrenceLocal)
{
    QDate date;
    QTime time;
    bool dateOnly = false;
    const QTimeZone zone(zoneId.toUtf8());
    if (!parseLocal(occurrenceLocal, &date, &time, &dateOnly) || !zone.isValid()) {
        setError(QStringLiteral("Invalid occurrence local start or zone."));
        return {};
    }
    // Search the surrounding local days only: the rule is still evaluated
    // from the series start, so count/until/interval apply as in expand().
    const QString from = date.addDays(-1).startOfDay(zone).toUTC().toString(Qt::ISODate);
    const QString to = date.addDays(2).startOfDay(zone).toUTC().toString(Qt::ISODate);
    QString wanted = occurrenceLocal.trimmed();
    wanted.replace(QLatin1Char(' '), QLatin1Char('T'));
    if (!dateOnly && wanted.size() == 16) wanted += QStringLiteral(":00");
    for (const QVariant &value : expand(eventId, startLocal, durationSeconds, zoneId, rule, from, to, 10)) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("localStart")).toString() == wanted) return row;
    }
    return {};
}

} // namespace QindaTK
