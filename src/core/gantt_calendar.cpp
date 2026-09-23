// SPDX-License-Identifier: LGPL-3.0-or-later
#include "gantt_calendar.h"

#include <QDateTime>
#include <algorithm>
#include <limits>

namespace QindaTK {

GanttCalendar::GanttCalendar(QObject *parent) : QObject(parent), m_zone(QTimeZone::UTC) {}

void GanttCalendar::error(const QString &message) const
{ if (m_error != message) { m_error = message; emit const_cast<GanttCalendar *>(this)->errorChanged(); } }

bool GanttCalendar::parseUtc(const QString &value, qint64 *milliseconds)
{
    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok, 10);
    if (!ok || QString::number(parsed) != value) return false;
    if (milliseconds) *milliseconds = parsed;
    return true;
}

void GanttCalendar::setTimeZoneId(const QString &id)
{
    const QTimeZone zone(id.toUtf8());
    if (!zone.isValid()) { error(QStringLiteral("Invalid time zone: %1").arg(id)); return; }
    if (zone == m_zone) return;
    m_zone = zone; error({}); emit changed();
}
void GanttCalendar::setWorkingDays(int days)
{
    if (days <= 0 || days > 0x7f) { error(QStringLiteral("Working days must use weekday bits 0–6")); return; }
    if (days == m_workingDays) return;
    m_workingDays = days; error({}); emit changed();
}
void GanttCalendar::setWorkStart(QTime time)
{
    if (!time.isValid() || time >= m_end) { error(QStringLiteral("Work start must precede work end")); return; }
    if (time == m_start) return;
    m_start = time; error({}); emit changed();
}
void GanttCalendar::setWorkEnd(QTime time)
{
    if (!time.isValid() || time <= m_start) { error(QStringLiteral("Work end must follow work start")); return; }
    if (time == m_end) return;
    m_end = time; error({}); emit changed();
}
void GanttCalendar::setHolidays(const QStringList &dates)
{
    for (const QString &date : dates) {
        if (QDate::fromString(date, Qt::ISODate).toString(Qt::ISODate) != date) {
            error(QStringLiteral("Holidays must be ISO local dates")); return;
        }
    }
    if (dates == m_holidays) return;
    m_holidays = dates; error({}); emit changed();
}
bool GanttCalendar::isWorkingDay(const QDate &date) const
{ return date.isValid() && (m_workingDays & (1 << (date.dayOfWeek() - 1)))
         && !m_holidays.contains(date.toString(Qt::ISODate)); }

bool GanttCalendar::interval(QDate date, qint64 *start, qint64 *end) const
{
    if (!isWorkingDay(date)) return false;
    const QDateTime a(date, m_start, m_zone);
    const QDateTime b(date, m_end, m_zone);
    if (!a.isValid() || !b.isValid() || a >= b) return false;
    *start = a.toMSecsSinceEpoch(); *end = b.toMSecsSinceEpoch();
    return true;
}

double GanttCalendar::workingMinutesBetween(const QString &fromUtc, const QString &toUtc) const
{
    qint64 from = 0, to = 0;
    if (!parseUtc(fromUtc, &from) || !parseUtc(toUtc, &to)) {
        error(QStringLiteral("UTC instants must be exact decimal millisecond strings")); return 0;
    }
    if (from == to) return 0;
    if (from > to) return -workingMinutesBetween(toUtc, fromUtc);
    const QDate first = QDateTime::fromMSecsSinceEpoch(from, m_zone).date();
    const QDate last = QDateTime::fromMSecsSinceEpoch(to, m_zone).date();
    if (first.daysTo(last) > 36600) { error(QStringLiteral("Calendar range exceeds 100 years")); return 0; }
    qint64 milliseconds = 0;
    for (QDate day = first; day <= last; day = day.addDays(1)) {
        qint64 start = 0, end = 0;
        if (interval(day, &start, &end))
            milliseconds += std::max<qint64>(0, std::min(end, to) - std::max(start, from));
    }
    error({});
    return double(milliseconds) / 60000.0;
}

QString GanttCalendar::snapForward(const QString &utc) const
{
    qint64 instant = 0;
    if (!parseUtc(utc, &instant)) { error(QStringLiteral("Invalid UTC instant")); return {}; }
    QDate day = QDateTime::fromMSecsSinceEpoch(instant, m_zone).date();
    for (int i = 0; i < 36600; ++i, day = day.addDays(1)) {
        qint64 start = 0, end = 0;
        if (interval(day, &start, &end)) {
            if (instant < start) return QString::number(start);
            if (instant < end) return QString::number(instant);
        }
    }
    error(QStringLiteral("No working interval within 100 years")); return {};
}

QString GanttCalendar::formatLocal(const QString &utc) const
{
    qint64 instant = 0;
    if (!parseUtc(utc, &instant)) return {};
    return QDateTime::fromMSecsSinceEpoch(instant, m_zone)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm t"));
}

QString GanttCalendar::addWorkingMinutes(const QString &fromUtc, qint64 minutes) const
{
    qint64 instant = 0;
    if (!parseUtc(fromUtc, &instant) || minutes == std::numeric_limits<qint64>::min()
        || minutes > std::numeric_limits<qint64>::max() / 60000
        || minutes < std::numeric_limits<qint64>::min() / 60000) {
        error(QStringLiteral("Invalid UTC instant or minute offset")); return {};
    }
    if (minutes == 0) return snapForward(fromUtc);
    qint64 remaining = (minutes > 0 ? minutes : -minutes) * 60000;
    const int direction = minutes > 0 ? 1 : -1;
    QDate day = QDateTime::fromMSecsSinceEpoch(instant, m_zone).date();
    for (int i = 0; i < 36600; ++i, day = day.addDays(direction)) {
        qint64 start = 0, end = 0;
        if (!interval(day, &start, &end)) continue;
        if (direction > 0) {
            const qint64 cursor = std::max(instant, start);
            if (cursor >= end) continue;
            const qint64 step = std::min(remaining, end - cursor);
            remaining -= step;
            if (!remaining) { error({}); return QString::number(cursor + step); }
        } else {
            const qint64 cursor = std::min(instant, end);
            if (cursor <= start) continue;
            const qint64 step = std::min(remaining, cursor - start);
            remaining -= step;
            if (!remaining) { error({}); return QString::number(cursor - step); }
        }
    }
    error(QStringLiteral("Minute offset exceeds 100 years of working time")); return {};
}

} // namespace QindaTK
