// SPDX-License-Identifier: LGPL-3.0-or-later
// Calendar edits of LocalProductivityStore: scoped moves, recurrence rules,
// occurrence exclusions and single-occurrence overrides, all with undo.
#include "local_productivity_store.h"
#include "recurrence_service.h"

#include <QDateTime>
#include <QTimeZone>
#include <algorithm>
#include <climits>

namespace QindaTK {
namespace {
constexpr int kUndoLimit = 100;
constexpr int kMaxShiftMinutes = 366 * 1440;

QString key(const char *name) { return QString::fromLatin1(name); }

QString seriesId(const QVariantMap &row)
{
    const QString explicitId = row.value(key("eventId")).toString().trimmed();
    return explicitId.isEmpty() ? row.value(key("id")).toString().trimmed() : explicitId;
}

bool recurring(const QVariantMap &row)
{
    return !row.value(key("recurrenceRule")).toMap().value(key("frequency")).toString().isEmpty();
}

// Moves a stored local time by wall-clock minutes, keeping its text shape.
// Timed values must exist exactly once in `zone` unless `validate` is false
// (identity keys such as EXDATE and RECURRENCE-ID values are not re-checked).
QString shiftWall(const QString &text, int minutes, const QTimeZone &zone, bool validate)
{
    QDate date;
    QTime time;
    bool dateOnly = false;
    if (!RecurrenceService::parseLocal(text, &date, &time, &dateOnly)) return {};
    if (dateOnly) {
        if (minutes % 1440) return {};
        return date.addDays(minutes / 1440).toString(Qt::ISODate);
    }
    // AGENT-GUARD: do wall-clock arithmetic before validating in the event
    // zone; zoned addSecs would move elapsed time and can normalize a DST gap.
    const QDateTime wall = QDateTime(date, time, QTimeZone::UTC).addSecs(qint64(minutes) * 60);
    if (validate) {
        const QDateTime zoned(wall.date(), wall.time(), zone, QDateTime::TransitionResolution::Reject);
        if (!zoned.isValid()) return {};
    }
    const QString trimmed = text.trimmed();
    const QChar separator = trimmed.at(10);
    const bool seconds = trimmed.size() > 16;
    return wall.date().toString(Qt::ISODate) + separator
        + wall.time().toString(seconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
}

// The original local start named by an occurrence ID ("<event>/<local>").
QString occurrenceLocal(const QVariantMap &row, const QString &occurrenceId)
{
    for (const QString &prefix : {seriesId(row), row.value(key("id")).toString()}) {
        const QString marker = prefix + QLatin1Char('/');
        if (!prefix.isEmpty() && occurrenceId.startsWith(marker))
            return RecurrenceService::canonicalLocal(occurrenceId.mid(marker.size()));
    }
    return RecurrenceService::canonicalLocal(occurrenceId);
}

QVariantMap ruleWith(const QVariantMap &row, bool withExceptions)
{
    QVariantMap rule = row.value(key("recurrenceRule")).toMap();
    rule.remove(key("exceptions"));
    if (withExceptions) rule.insert(key("exceptions"), row.value(key("exceptions")).toList());
    return rule;
}

// The occurrence row the series produces at `local`, or empty.
QVariantMap occurrenceOf(const QVariantMap &row, const QString &local, bool withExceptions)
{
    const QString zone = row.value(key("zone"), key("UTC")).toString();
    const QString start = row.value(key("startLocal")).toString();
    const qint64 span = RecurrenceService::spanSeconds(start, row.value(key("endLocal")).toString(), zone);
    if (local.isEmpty() || span < 0 || span > INT_MAX) return {};
    RecurrenceService service;
    return service.occurrenceAt(seriesId(row), start, int(span), zone, ruleWith(row, withExceptions), local);
}

int overrideIndex(const QVariantList &overrides, const QString &local)
{
    for (int i = 0; i < overrides.size(); ++i)
        if (RecurrenceService::canonicalLocal(overrides.at(i).toMap().value(key("recurrenceId")).toString()) == local)
            return i;
    return -1;
}

// The local end of a series occurrence that starts at `local`.
QString occurrenceEnd(const QVariantMap &occurrence, const QTimeZone &zone, bool allDay)
{
    const QDateTime end = QDateTime::fromString(occurrence.value(key("endUtc")).toString(), Qt::ISODate).toTimeZone(zone);
    return allDay ? end.date().toString(Qt::ISODate) : end.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
}

QVariantList rotateWeekdays(const QVariantList &weekdays, int dayDelta)
{
    QList<int> days;
    for (const QVariant &value : weekdays) {
        const int day = ((value.toInt() - 1 + dayDelta) % 7 + 7) % 7 + 1;
        if (!days.contains(day)) days.append(day);
    }
    std::sort(days.begin(), days.end());
    QVariantList out;
    for (int day : days) out.append(day);
    return out;
}

// Moves rule anchors (weekdays, until) and identity keys along with a start
// that moved by `minutes`; `dayDelta` is the change of the start's date.
void shiftSeriesKeys(QVariantMap *row, int minutes, int dayDelta, const QTimeZone &zone)
{
    QVariantMap rule = row->value(key("recurrenceRule")).toMap();
    if (dayDelta) {
        if (!rule.value(key("weekdays")).toList().isEmpty())
            rule.insert(key("weekdays"), rotateWeekdays(rule.value(key("weekdays")).toList(), dayDelta));
        const QDate until = QDate::fromString(rule.value(key("until")).toString(), Qt::ISODate);
        if (until.isValid()) rule.insert(key("until"), until.addDays(dayDelta).toString(Qt::ISODate));
        row->insert(key("recurrenceRule"), rule);
    }
    QVariantList exceptions;
    for (const QVariant &value : row->value(key("exceptions")).toList()) {
        const QString shifted = shiftWall(RecurrenceService::canonicalLocal(value.toString()), minutes, zone, false);
        if (!shifted.isEmpty()) exceptions.append(shifted);
    }
    row->insert(key("exceptions"), exceptions);
    QVariantList overrides;
    for (const QVariant &value : row->value(key("overrides")).toList()) {
        QVariantMap entry = value.toMap();
        entry.insert(key("recurrenceId"), shiftWall(RecurrenceService::canonicalLocal(
                                              entry.value(key("recurrenceId")).toString()), minutes, zone, false));
        overrides.append(entry);
    }
    row->insert(key("overrides"), overrides);
}
} // namespace

int LocalProductivityStore::eventIndex(const QString &eventId) const
{
    for (int i = 0; i < m_events.size(); ++i) {
        const QVariantMap row = m_events.at(i).toMap();
        if (idOf(row) == eventId || seriesId(row) == eventId) return i;
    }
    return -1;
}

QVariantMap LocalProductivityStore::eventError(const QString &error)
{
    setStatus(QStringLiteral("error"), error);
    return result(false, error);
}

void LocalProductivityStore::commitEvents(const QVariantList &previous)
{
    m_eventUndo.append(previous);
    if (m_eventUndo.size() > kUndoLimit) m_eventUndo.removeFirst();
    ++m_revision;
    emit dataChanged();
    emit eventHistoryChanged();
    setStatus(QStringLiteral("modified"));
}

QVariantMap LocalProductivityStore::shiftEventLocal(const QString &eventId, int minutes,
                                                    const QString &scope, const QString &occurrenceId)
{
    if (minutes == 0 || minutes < -kMaxShiftMinutes || minutes > kMaxShiftMinutes)
        return eventError(QStringLiteral("Event shift must be nonzero and within 366 days"));
    if (scope != QLatin1String("series") && scope != QLatin1String("occurrence")
        && scope != QLatin1String("following"))
        return eventError(QStringLiteral("Move scope must be series, occurrence or following"));
    const int index = eventIndex(eventId);
    if (index < 0) return eventError(QStringLiteral("Event %1 was not found").arg(eventId));
    const QVariantList previous = m_events;
    QVariantMap row = m_events.at(index).toMap();
    const QTimeZone zone(row.value(QStringLiteral("zone"), QStringLiteral("UTC")).toString().toUtf8());
    QDate startDate, oldDate;
    QTime startTime;
    bool allDay = false;
    const QString startText = row.value(QStringLiteral("startLocal")).toString();
    if (!zone.isValid() || !RecurrenceService::parseLocal(startText, &startDate, &startTime, &allDay))
        return eventError(QStringLiteral("Event %1 has an invalid local start or zone").arg(eventId));
    if (allDay && minutes % 1440)
        return eventError(QStringLiteral("All-day events move by whole days"));
    const QString invalid = QStringLiteral("Event shift enters an invalid or ambiguous local time");

    const bool series = scope == QLatin1String("series") || !recurring(row);
    const QString local = series ? QString() : occurrenceLocal(row, occurrenceId);
    QVariantMap occurrence;
    if (!series) {
        occurrence = occurrenceOf(row, local, true);
        if (occurrence.isEmpty())
            return eventError(QStringLiteral("Occurrence %1 is not part of event %2").arg(occurrenceId, eventId));
    }
    oldDate = startDate;
    if (series || (scope == QLatin1String("following")
                   && QDate::fromString(local.left(10), Qt::ISODate) == startDate)) {
        const QString start = shiftWall(startText, minutes, zone, true);
        const QString end = shiftWall(row.value(QStringLiteral("endLocal")).toString(), minutes, zone, true);
        if (start.isEmpty() || end.isEmpty()) return eventError(invalid);
        row.insert(QStringLiteral("startLocal"), start);
        row.insert(QStringLiteral("endLocal"), end);
        if (recurring(row))
            shiftSeriesKeys(&row, minutes, int(oldDate.daysTo(QDate::fromString(start.left(10), Qt::ISODate))), zone);
        m_events[index] = row;
        commitEvents(previous);
        return result(true);
    }

    if (scope == QLatin1String("occurrence")) {
        QVariantList overrides = row.value(QStringLiteral("overrides")).toList();
        const int at = overrideIndex(overrides, local);
        QVariantMap entry = at >= 0 ? overrides.at(at).toMap() : QVariantMap{{QStringLiteral("recurrenceId"), local}};
        const QString baseStart = entry.value(QStringLiteral("startLocal"), local).toString();
        const QString baseEnd = entry.value(QStringLiteral("endLocal"), occurrenceEnd(occurrence, zone, allDay)).toString();
        const QString start = shiftWall(baseStart, minutes, zone, true);
        const QString end = shiftWall(baseEnd, minutes, zone, true);
        if (start.isEmpty() || end.isEmpty()) return eventError(invalid);
        entry.insert(QStringLiteral("startLocal"), start);
        entry.insert(QStringLiteral("endLocal"), end);
        if (at >= 0) overrides[at] = entry;
        else overrides.append(entry);
        row.insert(QStringLiteral("overrides"), overrides);
        m_events[index] = row;
        commitEvents(previous);
        return result(true);
    }

    // "following": end the original series before the occurrence and start a
    // new series there. COUNT is split by the occurrences already generated
    // (exclusions still count, as in RFC 5545).
    const QDate splitDate = QDate::fromString(local.left(10), Qt::ISODate);
    QVariantMap rule = ruleWith(row, false);
    QVariantMap tail = row;
    QVariantMap tailRule = rule;
    if (rule.value(QStringLiteral("endMode")).toString() == QLatin1String("count")) {
        RecurrenceService service;
        const QString zoneId = QString::fromUtf8(zone.id());
        const qint64 span = RecurrenceService::spanSeconds(startText, row.value(QStringLiteral("endLocal")).toString(), zoneId);
        const QVariantList before = service.expand(seriesId(row), startText, int(qMax<qint64>(0, span)), zoneId, rule,
            startDate.startOfDay(zone).toUTC().toString(Qt::ISODate),
            occurrence.value(QStringLiteral("startUtc")).toString(), 1000);
        const int remaining = rule.value(QStringLiteral("count")).toInt() - int(before.size());
        if (before.isEmpty() || before.size() >= 1000 || remaining < 1)
            return eventError(QStringLiteral("The series cannot be split by count at %1").arg(local));
        rule.insert(QStringLiteral("count"), int(before.size()));
        tailRule.insert(QStringLiteral("count"), remaining);
    } else {
        rule.insert(QStringLiteral("endMode"), QStringLiteral("until"));
        rule.insert(QStringLiteral("until"), splitDate.addDays(-1).toString(Qt::ISODate));
    }
    const QString tailStart = shiftWall(local, minutes, zone, true);
    const QString tailEnd = shiftWall(occurrenceEnd(occurrence, zone, allDay), minutes, zone, true);
    if (tailStart.isEmpty() || tailEnd.isEmpty()) return eventError(invalid);
    QVariantList headExceptions, tailExceptions, headOverrides, tailOverrides;
    for (const QVariant &value : row.value(QStringLiteral("exceptions")).toList()) {
        const QString text = RecurrenceService::canonicalLocal(value.toString());
        (QDate::fromString(text.left(10), Qt::ISODate) >= splitDate ? tailExceptions : headExceptions).append(text);
    }
    for (const QVariant &value : row.value(QStringLiteral("overrides")).toList()) {
        const QString text = RecurrenceService::canonicalLocal(value.toMap().value(QStringLiteral("recurrenceId")).toString());
        (QDate::fromString(text.left(10), Qt::ISODate) >= splitDate ? tailOverrides : headOverrides).append(value);
    }
    row.insert(QStringLiteral("recurrenceRule"), rule);
    row.insert(QStringLiteral("exceptions"), headExceptions);
    row.insert(QStringLiteral("overrides"), headOverrides);

    QString tailId = seriesId(row) + QLatin1Char('~') + splitDate.toString(QStringLiteral("yyyyMMdd"));
    for (int suffix = 2; eventIndex(tailId) >= 0; ++suffix)
        tailId = seriesId(row) + QLatin1Char('~') + splitDate.toString(QStringLiteral("yyyyMMdd")) + QLatin1Char('-') + QString::number(suffix);
    tail.insert(QStringLiteral("id"), tailId);
    tail.insert(QStringLiteral("eventId"), tailId);
    tail.insert(QStringLiteral("startLocal"), tailStart);
    tail.insert(QStringLiteral("endLocal"), tailEnd);
    tail.insert(QStringLiteral("recurrenceRule"), tailRule);
    tail.insert(QStringLiteral("exceptions"), tailExceptions);
    tail.insert(QStringLiteral("overrides"), tailOverrides);
    // AGENT-NOTE: the new series is a new remote resource; keeping only
    // remoteAccount makes CalDavProvider create it (docs/productivity-providers.md).
    for (const QString &field : {QStringLiteral("remoteHref"), QStringLiteral("remoteEtag"), QStringLiteral("remoteUid"),
                                 QStringLiteral("remoteIcs"), QStringLiteral("syncDigest"), QStringLiteral("syncState")})
        tail.remove(field);
    shiftSeriesKeys(&tail, minutes, int(splitDate.daysTo(QDate::fromString(tailStart.left(10), Qt::ISODate))), zone);
    m_events[index] = row;
    m_events.insert(index + 1, tail);
    commitEvents(previous);
    QVariantMap done = result(true);
    done.insert(QStringLiteral("eventId"), tailId);
    return done;
}

QVariantMap LocalProductivityStore::setEventRecurrence(const QString &eventId, const QVariantMap &proposed)
{
    const int index = eventIndex(eventId);
    if (index < 0) return eventError(QStringLiteral("Event %1 was not found").arg(eventId));
    const QVariantList previous = m_events;
    QVariantMap row = m_events.at(index).toMap();
    if (proposed.value(QStringLiteral("frequency")).toString().isEmpty()) {
        row.insert(QStringLiteral("recurrenceRule"), QVariant::fromValue(nullptr));
        row.insert(QStringLiteral("recurrenceRaw"), QString());
        row.insert(QStringLiteral("exceptions"), QVariantList());
        row.insert(QStringLiteral("overrides"), QVariantList());
        m_events[index] = row;
        commitEvents(previous);
        return result(true);
    }
    QVariantMap rule{{QStringLiteral("frequency"), proposed.value(QStringLiteral("frequency")).toString().toLower()},
                     {QStringLiteral("interval"), proposed.value(QStringLiteral("interval"), 1).toInt()},
                     {QStringLiteral("endMode"), proposed.value(QStringLiteral("endMode"), QStringLiteral("never")).toString()}};
    const QVariantList weekdays = rotateWeekdays(proposed.value(QStringLiteral("weekdays")).toList(), 0);
    if (!weekdays.isEmpty() && rule.value(QStringLiteral("frequency")).toString() == QLatin1String("weekly"))
        rule.insert(QStringLiteral("weekdays"), weekdays);
    if (rule.value(QStringLiteral("endMode")).toString() == QLatin1String("count"))
        rule.insert(QStringLiteral("count"), proposed.value(QStringLiteral("count")).toInt());
    if (rule.value(QStringLiteral("endMode")).toString() == QLatin1String("until"))
        rule.insert(QStringLiteral("until"), proposed.value(QStringLiteral("until")).toString());
    const QString zone = row.value(QStringLiteral("zone"), QStringLiteral("UTC")).toString();
    const QString start = row.value(QStringLiteral("startLocal")).toString();
    const qint64 span = RecurrenceService::spanSeconds(start, row.value(QStringLiteral("endLocal")).toString(), zone);
    RecurrenceService service;
    service.expand(seriesId(row), start, int(qMax<qint64>(0, span)), zone, rule,
                   QStringLiteral("2000-01-01T00:00:00Z"), QStringLiteral("2000-01-02T00:00:00Z"), 1);
    if (span < 0 || !service.error().isEmpty())
        return eventError(service.error().isEmpty() ? QStringLiteral("Event %1 has an invalid span").arg(eventId)
                                                    : service.error());
    row.insert(QStringLiteral("recurrenceRule"), rule);
    row.insert(QStringLiteral("recurrenceRaw"), QString());
    // Exclusions and overrides that the new rule no longer produces would be
    // dead identity keys; drop them rather than keep hidden state.
    QVariantList exceptions, overrides;
    for (const QVariant &value : row.value(QStringLiteral("exceptions")).toList()) {
        const QString local = RecurrenceService::canonicalLocal(value.toString().section(QLatin1Char('/'), -1));
        if (!occurrenceOf(row, local, false).isEmpty()) exceptions.append(local);
    }
    for (const QVariant &value : row.value(QStringLiteral("overrides")).toList()) {
        const QString local = RecurrenceService::canonicalLocal(value.toMap().value(QStringLiteral("recurrenceId")).toString());
        if (!occurrenceOf(row, local, false).isEmpty()) overrides.append(value);
    }
    row.insert(QStringLiteral("exceptions"), exceptions);
    row.insert(QStringLiteral("overrides"), overrides);
    m_events[index] = row;
    commitEvents(previous);
    return result(true);
}

QVariantMap LocalProductivityStore::setOccurrenceExcluded(const QString &eventId, const QString &occurrenceId,
                                                          bool excluded)
{
    const int index = eventIndex(eventId);
    if (index < 0) return eventError(QStringLiteral("Event %1 was not found").arg(eventId));
    QVariantMap row = m_events.at(index).toMap();
    const QString local = occurrenceLocal(row, occurrenceId);
    if (!recurring(row) || occurrenceOf(row, local, false).isEmpty())
        return eventError(QStringLiteral("Occurrence %1 is not part of event %2").arg(occurrenceId, eventId));
    const QVariantList previous = m_events;
    QVariantList exceptions;
    for (const QVariant &value : row.value(QStringLiteral("exceptions")).toList())
        if (RecurrenceService::canonicalLocal(value.toString().section(QLatin1Char('/'), -1)) != local)
            exceptions.append(value);
    if (excluded) {
        exceptions.append(local);
        QVariantList overrides = row.value(QStringLiteral("overrides")).toList();
        const int at = overrideIndex(overrides, local);
        if (at >= 0) overrides.removeAt(at);          // an excluded instance has no override
        row.insert(QStringLiteral("overrides"), overrides);
    }
    row.insert(QStringLiteral("exceptions"), exceptions);
    m_events[index] = row;
    commitEvents(previous);
    return result(true);
}

QVariantMap LocalProductivityStore::setOccurrenceOverride(const QString &eventId, const QString &occurrenceId,
                                                          const QVariantMap &fields)
{
    const int index = eventIndex(eventId);
    if (index < 0) return eventError(QStringLiteral("Event %1 was not found").arg(eventId));
    QVariantMap row = m_events.at(index).toMap();
    const QString local = occurrenceLocal(row, occurrenceId);
    if (!recurring(row) || occurrenceOf(row, local, true).isEmpty())
        return eventError(QStringLiteral("Occurrence %1 is not part of event %2").arg(occurrenceId, eventId));
    QVariantMap entry{{QStringLiteral("recurrenceId"), local}};
    for (const QString &field : {QStringLiteral("title"), QStringLiteral("description"), QStringLiteral("location"),
                                 QStringLiteral("startLocal"), QStringLiteral("endLocal")})
        if (fields.contains(field)) entry.insert(field, fields.value(field));
    const bool moved = entry.contains(QStringLiteral("startLocal")) || entry.contains(QStringLiteral("endLocal"));
    if (moved) {
        const QString zone = row.value(QStringLiteral("zone"), QStringLiteral("UTC")).toString();
        const qint64 span = RecurrenceService::spanSeconds(entry.value(QStringLiteral("startLocal")).toString(),
                                                           entry.value(QStringLiteral("endLocal")).toString(), zone);
        if (!entry.contains(QStringLiteral("startLocal")) || !entry.contains(QStringLiteral("endLocal")) || span <= 0)
            return eventError(QStringLiteral("An occurrence override needs a valid local start and end"));
    }
    const QVariantList previous = m_events;
    QVariantList overrides = row.value(QStringLiteral("overrides")).toList();
    const int at = overrideIndex(overrides, local);
    if (at >= 0) overrides.removeAt(at);
    if (entry.size() > 1) overrides.append(entry);
    row.insert(QStringLiteral("overrides"), overrides);
    m_events[index] = row;
    commitEvents(previous);
    return result(true);
}

QVariantMap LocalProductivityStore::undoEventChange()
{
    if (m_eventUndo.isEmpty()) return eventError(QStringLiteral("No event change to undo"));
    m_events = m_eventUndo.takeLast();
    ++m_revision;
    emit dataChanged();
    emit eventHistoryChanged();
    setStatus(QStringLiteral("modified"));
    return result(true);
}

} // namespace QindaTK
