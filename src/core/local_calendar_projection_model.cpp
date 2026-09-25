// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_calendar_projection_model.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include <QSet>
#include <algorithm>
#include <climits>

namespace QindaTK {
namespace {
QString localText(const QDateTime &value)
{
    return value.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
}

QString eventStableId(const QVariantMap &event)
{
    const QString explicitId = event.value(QStringLiteral("eventId")).toString().trimmed();
    return explicitId.isEmpty() ? event.value(QStringLiteral("id")).toString().trimmed() : explicitId;
}

// UTC bounds of a stored local span. All-day spans cover whole local days
// (end exclusive); a timed start must exist in the zone when `strict`.
bool zonedSpan(const QString &startText, const QString &endText, const QTimeZone &zone, bool strict,
               QDateTime *startUtc, QDateTime *endUtc, bool *allDay)
{
    QDate startDate, endDate;
    QTime startTime, endTime;
    bool endDateOnly = false;
    if (!RecurrenceService::parseLocal(startText, &startDate, &startTime, allDay)) return false;
    const bool hasEnd = !endText.trimmed().isEmpty();
    if (hasEnd && !RecurrenceService::parseLocal(endText, &endDate, &endTime, &endDateOnly)) return false;
    if (*allDay) {
        if (!hasEnd || endDate <= startDate) endDate = startDate.addDays(1);
        *startUtc = startDate.startOfDay(zone).toUTC();
        *endUtc = endDate.startOfDay(zone).toUTC();
        return startUtc->isValid() && endUtc->isValid();
    }
    const QDateTime start(startDate, startTime, zone, QDateTime::TransitionResolution::PreferBefore);
    const QDateTime end(endDate, endTime, zone, QDateTime::TransitionResolution::PreferBefore);
    if (!start.isValid() || !end.isValid() || end <= start || !hasEnd) return false;
    if (strict && (start.date() != startDate || start.time() != startTime
                   || end.date() != endDate || end.time() != endTime))
        return false;
    *startUtc = start.toUTC();
    *endUtc = end.toUTC();
    return true;
}

QVariantMap applyOverride(QVariantMap row, const QVariantMap &entry)
{
    for (const QString &field : {QStringLiteral("title"), QStringLiteral("description"), QStringLiteral("location"),
                                 QStringLiteral("startLocal"), QStringLiteral("endLocal")})
        if (entry.contains(field)) row.insert(field, entry.value(field));
    row.insert(QStringLiteral("overridden"), true);
    return row;
}
} // namespace

LocalCalendarProjectionModel::LocalCalendarProjectionModel(QObject *parent) : QObject(parent) {}

void LocalCalendarProjectionModel::setStore(LocalProductivityStore *store)
{
    if (m_store == store) return;
    if (m_store) disconnect(m_store, nullptr, this, nullptr);
    m_store = store;
    if (m_store) connect(m_store, &LocalProductivityStore::dataChanged, this,
                         [this] { if (!m_rangeStartUtc.isEmpty()) projectCurrent(); });
    emit inputsChanged();
    if (!m_rangeStartUtc.isEmpty()) projectCurrent();
}

void LocalCalendarProjectionModel::setRecurrenceService(RecurrenceService *service)
{
    if (m_recurrenceService == service) return;
    m_recurrenceService = service;
    emit inputsChanged();
    if (!m_rangeStartUtc.isEmpty()) projectCurrent();
}

void LocalCalendarProjectionModel::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

bool LocalCalendarProjectionModel::project(const QString &rangeStartUtc,
                                           const QString &rangeEndUtc, int limit)
{
    if (m_rangeStartUtc != rangeStartUtc || m_rangeEndUtc != rangeEndUtc) {
        m_rangeStartUtc = rangeStartUtc;
        m_rangeEndUtc = rangeEndUtc;
        emit rangeChanged();
    }
    return projectCurrent(limit);
}

bool LocalCalendarProjectionModel::projectCurrent(int limit)
{
    const QDateTime from = QDateTime::fromString(m_rangeStartUtc, Qt::ISODate).toUTC();
    const QDateTime to = QDateTime::fromString(m_rangeEndUtc, Qt::ISODate).toUTC();
    if (!m_store || !m_recurrenceService || m_rangeStartUtc.isEmpty()
        || m_rangeEndUtc.isEmpty() || !from.isValid() || !to.isValid() || from >= to
        || limit < 1 || limit > 5000) {
        setError(QStringLiteral("Store, recurrence service, UTC range or result limit is invalid."));
        return false;
    }

    QVariantList projected;
    const auto add = [&](const QVariantMap &row) {
        if (projected.size() >= limit) {
            setError(QStringLiteral("Calendar projection reached its result limit."));
            return false;
        }
        projected.append(row);
        return true;
    };
    const auto withUtc = [](QVariantMap row, const QDateTime &start, const QDateTime &end) {
        row.insert(QStringLiteral("startUtc"), start.toString(Qt::ISODateWithMs));
        row.insert(QStringLiteral("endUtc"), end.toString(Qt::ISODateWithMs));
        return row;
    };
    for (const QVariant &value : m_store->events()) {
        const QVariantMap event = value.toMap();
        const QString eventId = eventStableId(event);
        const QString zoneId = event.value(QStringLiteral("zone"), QStringLiteral("UTC")).toString();
        const QTimeZone zone(zoneId.toUtf8());
        const QString startText = event.value(QStringLiteral("startLocal")).toString();
        const QString endText = event.value(QStringLiteral("endLocal")).toString();
        QDateTime startUtc, endUtc;
        bool allDay = false;
        if (eventId.isEmpty() || !zone.isValid()
            || !zonedSpan(startText, endText, zone, true, &startUtc, &endUtc, &allDay)) {
            setError(QStringLiteral("Calendar event has an invalid stable ID, local time or time zone."));
            return false;
        }
        const qint64 duration = RecurrenceService::spanSeconds(startText, endText, zoneId);
        const QVariantMap rule = event.value(QStringLiteral("recurrenceRule")).toMap();
        if (rule.isEmpty()) {
            if (startUtc < to && endUtc > from) {
                QVariantMap row = event;
                row.insert(QStringLiteral("eventId"), eventId);
                row.insert(QStringLiteral("occurrenceId"), eventId + QLatin1Char('/')
                               + RecurrenceService::canonicalLocal(startText));
                if (!add(withUtc(row, startUtc, endUtc))) return false;
            }
            continue;
        }
        QVariantMap effectiveRule = rule;
        effectiveRule.insert(QStringLiteral("exceptions"), event.value(QStringLiteral("exceptions")).toList());
        // AGENT-CONTRACT (local_productivity_events.cpp, ical_overrides.cpp):
        // `overrides` entries replace fields of the occurrence whose original
        // local start is `recurrenceId`; the occurrence keeps its ID.
        QMap<QString, QVariantMap> overrides;
        for (const QVariant &entry : event.value(QStringLiteral("overrides")).toList())
            overrides.insert(RecurrenceService::canonicalLocal(entry.toMap().value(QStringLiteral("recurrenceId")).toString()),
                             entry.toMap());
        const QVariantList occurrences = m_recurrenceService->expand(
            eventId, startText, int(qBound<qint64>(0, duration, qint64(INT_MAX))), zoneId, effectiveRule,
            m_rangeStartUtc, m_rangeEndUtc, qMin(limit, 1000));
        if (!m_recurrenceService->error().isEmpty()) {
            setError(m_recurrenceService->error());
            return false;
        }
        QSet<QString> placed;
        const auto placeOverride = [&](QVariantMap row, const QString &original) {
            placed.insert(original);
            row = applyOverride(row, overrides.value(original));
            row.insert(QStringLiteral("recurrenceId"), original);
            QDateTime overrideStart, overrideEnd;
            bool overrideAllDay = false;
            if (!zonedSpan(row.value(QStringLiteral("startLocal")).toString(), row.value(QStringLiteral("endLocal")).toString(),
                           zone, false, &overrideStart, &overrideEnd, &overrideAllDay))
                return true;                    // an unreadable override hides nothing else
            if (overrideStart >= to || overrideEnd <= from) return true;
            return add(withUtc(row, overrideStart, overrideEnd));
        };
        for (const QVariant &occurrenceValue : occurrences) {
            const QVariantMap occurrence = occurrenceValue.toMap();
            const QString original = occurrence.value(QStringLiteral("localStart")).toString();
            const QDateTime occurrenceStart = QDateTime::fromString(
                occurrence.value(QStringLiteral("startUtc")).toString(), Qt::ISODate).toUTC();
            const QDateTime occurrenceEnd = QDateTime::fromString(
                occurrence.value(QStringLiteral("endUtc")).toString(), Qt::ISODate).toUTC();
            QVariantMap row = event;
            row.insert(QStringLiteral("eventId"), eventId);
            row.insert(QStringLiteral("occurrenceId"), occurrence.value(QStringLiteral("occurrenceId")));
            row.insert(QStringLiteral("startLocal"), original);
            row.insert(QStringLiteral("endLocal"), allDay ? occurrenceEnd.toTimeZone(zone).date().toString(Qt::ISODate)
                                                          : localText(occurrenceEnd.toTimeZone(zone)));
            row.insert(QStringLiteral("zone"), zoneId);
            if (overrides.contains(original)) {
                if (!placeOverride(row, original)) return false;
            } else if (!add(withUtc(row, occurrenceStart, occurrenceEnd))) {
                return false;
            }
        }
        // An instance moved into this range from outside it is found through
        // its override; it still has to be a live occurrence of the series.
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
            if (placed.contains(it.key()) || !it.value().contains(QStringLiteral("startLocal"))) continue;
            const QVariantMap occurrence = m_recurrenceService->occurrenceAt(
                eventId, startText, int(qBound<qint64>(0, duration, qint64(INT_MAX))), zoneId, effectiveRule, it.key());
            if (occurrence.isEmpty()) continue;
            QVariantMap row = event;
            row.insert(QStringLiteral("eventId"), eventId);
            row.insert(QStringLiteral("occurrenceId"), occurrence.value(QStringLiteral("occurrenceId")));
            row.insert(QStringLiteral("zone"), zoneId);
            if (!placeOverride(row, it.key())) return false;
        }
    }
    std::sort(projected.begin(), projected.end(), [](const QVariant &left, const QVariant &right) {
        const QVariantMap a = left.toMap(), b = right.toMap();
        const QString aStart = a.value(QStringLiteral("startUtc")).toString();
        const QString bStart = b.value(QStringLiteral("startUtc")).toString();
        if (aStart != bStart) return aStart < bStart;
        const QString aEvent = a.value(QStringLiteral("eventId")).toString();
        const QString bEvent = b.value(QStringLiteral("eventId")).toString();
        return aEvent != bEvent ? aEvent < bEvent
                                : a.value(QStringLiteral("occurrenceId")).toString()
                                      < b.value(QStringLiteral("occurrenceId")).toString();
    });
    if (m_occurrences != projected) {
        m_occurrences = projected;
        emit occurrencesChanged();
    }
    setError({});
    return true;
}

} // namespace QindaTK
