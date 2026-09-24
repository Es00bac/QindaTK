// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_calendar_projection_model.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include <algorithm>

namespace QindaTK {
namespace {
bool localDateTime(const QString &text, const QTimeZone &zone, QDateTime *value)
{
    const QDate date = QDate::fromString(text.left(10), Qt::ISODate);
    QTime time(0, 0);
    if (text.size() > 10) {
        QString timeText = text.mid(11);
        if (timeText.size() >= 8) timeText = timeText.left(8);
        else timeText = timeText.left(5);
        time = QTime::fromString(timeText, timeText.size() == 8
            ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
    }
    if (!date.isValid() || !time.isValid()) return false;
    *value = QDateTime(date, time, zone, QDateTime::TransitionResolution::PreferBefore);
    return value->isValid() && value->date() == date && value->time() == time;
}

QString localText(const QDateTime &value)
{
    return value.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
}

QString eventStableId(const QVariantMap &event)
{
    const QString explicitId = event.value(QStringLiteral("eventId")).toString().trimmed();
    return explicitId.isEmpty() ? event.value(QStringLiteral("id")).toString().trimmed() : explicitId;
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
    for (const QVariant &value : m_store->events()) {
        const QVariantMap event = value.toMap();
        const QString eventId = eventStableId(event);
        const QString zoneId = event.value(QStringLiteral("zone"), QStringLiteral("UTC")).toString();
        const QTimeZone zone(zoneId.toUtf8());
        QDateTime localStart, localEnd;
        if (eventId.isEmpty() || !zone.isValid()
            || !localDateTime(event.value(QStringLiteral("startLocal")).toString(), zone, &localStart)
            || !localDateTime(event.value(QStringLiteral("endLocal")).toString(), zone, &localEnd)
            || localEnd <= localStart) {
            setError(QStringLiteral("Calendar event has an invalid stable ID, local time or time zone."));
            return false;
        }
        const int duration = static_cast<int>(localStart.secsTo(localEnd));
        const QVariantMap rule = event.value(QStringLiteral("recurrenceRule")).toMap();
        if (!rule.isEmpty()) {
            QVariantMap effectiveRule = rule;
            if (!event.value(QStringLiteral("exceptions")).toList().isEmpty())
                effectiveRule.insert(QStringLiteral("exceptions"), event.value(QStringLiteral("exceptions")));
            const QVariantList occurrences = m_recurrenceService->expand(
                eventId, event.value(QStringLiteral("startLocal")).toString(), duration,
                zoneId, effectiveRule, m_rangeStartUtc, m_rangeEndUtc,
                qMin(limit, 1000));
            if (!m_recurrenceService->error().isEmpty()) {
                setError(m_recurrenceService->error());
                return false;
            }
            for (const QVariant &occurrenceValue : occurrences) {
                if (projected.size() >= limit) {
                    setError(QStringLiteral("Calendar projection reached its result limit."));
                    return false;
                }
                const QVariantMap occurrence = occurrenceValue.toMap();
                QVariantMap row = event;
                const QString startLocal = occurrence.value(QStringLiteral("localStart")).toString();
                const QDateTime startUtc = QDateTime::fromString(
                    occurrence.value(QStringLiteral("startUtc")).toString(), Qt::ISODate).toUTC();
                const QDateTime endUtc = QDateTime::fromString(
                    occurrence.value(QStringLiteral("endUtc")).toString(), Qt::ISODate).toUTC();
                row.insert(QStringLiteral("eventId"), eventId);
                row.insert(QStringLiteral("occurrenceId"), occurrence.value(QStringLiteral("occurrenceId")));
                row.insert(QStringLiteral("startLocal"), startLocal);
                row.insert(QStringLiteral("endLocal"), localText(endUtc.toTimeZone(zone)));
                row.insert(QStringLiteral("zone"), zoneId);
                row.insert(QStringLiteral("startUtc"), startUtc.toString(Qt::ISODateWithMs));
                row.insert(QStringLiteral("endUtc"), endUtc.toString(Qt::ISODateWithMs));
                projected.append(row);
            }
        } else {
            const QDateTime startUtc = localStart.toUTC();
            const QDateTime endUtc = localEnd.toUTC();
            if (startUtc < to && endUtc > from) {
                if (projected.size() >= limit) {
                    setError(QStringLiteral("Calendar projection reached its result limit."));
                    return false;
                }
                QVariantMap row = event;
                row.insert(QStringLiteral("eventId"), eventId);
                row.insert(QStringLiteral("occurrenceId"), eventId + QLatin1Char('/') + localText(localStart));
                row.insert(QStringLiteral("startUtc"), startUtc.toString(Qt::ISODateWithMs));
                row.insert(QStringLiteral("endUtc"), endUtc.toString(Qt::ISODateWithMs));
                projected.append(row);
            }
        }
    }
    std::sort(projected.begin(), projected.end(), [](const QVariant &left, const QVariant &right) {
        const QVariantMap a = left.toMap(), b = right.toMap();
        const QString aStart = a.value(QStringLiteral("startUtc")).toString();
        const QString bStart = b.value(QStringLiteral("startUtc")).toString();
        return aStart == bStart ? a.value(QStringLiteral("eventId")).toString()
                                      < b.value(QStringLiteral("eventId")).toString()
                                : aStart < bStart;
    });
    if (m_occurrences != projected) {
        m_occurrences = projected;
        emit occurrencesChanged();
    }
    setError({});
    return true;
}

} // namespace QindaTK
