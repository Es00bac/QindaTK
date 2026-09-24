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

QVariantList RecurrenceService::expand(const QString &eventId, const QString &startLocal,
                                       int durationSeconds, const QString &zoneId,
                                       const QVariantMap &rule, const QString &rangeStartUtc,
                                       const QString &rangeEndUtc, int limit)
{
    setError({});
    const QTimeZone zone(zoneId.toUtf8());
    const QDateTime local = QDateTime::fromString(startLocal, Qt::ISODate);
    const QDateTime from = QDateTime::fromString(rangeStartUtc, Qt::ISODate).toUTC();
    const QDateTime to = QDateTime::fromString(rangeEndUtc, Qt::ISODate).toUTC();
    const QString frequency = rule.value(QStringLiteral("frequency"), QStringLiteral("daily")).toString().toLower();
    const int interval = rule.value(QStringLiteral("interval"), 1).toInt();
    const QString endMode = rule.value(QStringLiteral("endMode"), QStringLiteral("never")).toString();
    const int countLimit = rule.value(QStringLiteral("count")).toInt();
    const QDate until = QDate::fromString(rule.value(QStringLiteral("until")).toString(), Qt::ISODate);
    if (eventId.isEmpty() || !zone.isValid() || !local.isValid() || !from.isValid() || !to.isValid()
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
    for (const QVariant &entry : rule.value(QStringLiteral("exceptions")).toList())
        exceptions.insert(entry.toString());

    const QDate firstDate = local.date();
    const QTime wallTime = local.time();
    const QDate lastDate = to.toTimeZone(zone).date().addDays(1);
    QVariantList rows;
    int generated = 0;
    int searched = 0;
    QDate day = firstDate;
    for (; day.isValid() && day <= lastDate && searched < kMaxSearchDays;
         day = day.addDays(1), ++searched) {
        if (endMode == QLatin1String("until") && day > until) break;
        if (!selectedDay(day, firstDate, frequency, interval, weekdays)) continue;
        // AGENT-GUARD: preserve the requested local wall time across DST; skip
        // a spring-forward gap rather than silently moving an occurrence.
        const QDateTime start(day, wallTime, zone, QDateTime::TransitionResolution::PreferBefore);
        if (!start.isValid() || start.date() != day || start.time() != wallTime) continue;
        ++generated;
        if (endMode == QLatin1String("count") && generated > countLimit) break;
        const QString localText = day.toString(Qt::ISODate) + QLatin1Char('T')
            + wallTime.toString(QStringLiteral("HH:mm:ss"));
        const QString occurrenceId = eventId + QLatin1Char('/') + localText;
        if (exceptions.contains(occurrenceId) || exceptions.contains(localText)) continue;
        const QDateTime utc = start.toUTC();
        if (utc >= to || utc.addSecs(durationSeconds) <= from) continue;
        rows.append(QVariantMap{{QStringLiteral("eventId"), eventId},
                                {QStringLiteral("occurrenceId"), occurrenceId},
                                {QStringLiteral("localStart"), localText},
                                {QStringLiteral("zone"), zoneId},
                                {QStringLiteral("startUtc"), utc.toString(Qt::ISODateWithMs)},
                                {QStringLiteral("endUtc"), utc.addSecs(durationSeconds).toString(Qt::ISODateWithMs)}});
        if (rows.size() >= limit) break;
    }
    if (searched == kMaxSearchDays && day.isValid() && day <= lastDate)
        setError(QStringLiteral("Recurrence search reached its 100-year date bound."));
    return rows;
}

} // namespace QindaTK
