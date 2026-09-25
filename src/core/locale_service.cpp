// SPDX-License-Identifier: LGPL-3.0-or-later
#include "locale_service.h"

#include <QCalendar>
#include <QChar>
#include <QDateTime>
#include <QtMath>
#include <algorithm>

namespace QindaTK {
namespace {

// A year is a label, not a quantity: "September 2026", never "2,026"
// (QLocale::toString groups thousands by default).
QString yearText(QLocale locale, int year)
{
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale.toString(year);
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QVariantMap success(const QVariant &value)
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("value"), value}};
}
}

LocaleService::LocaleService(QObject *parent)
    : QObject(parent), m_locale(QLocale::system()), m_timeZone(QTimeZone::systemTimeZone())
{
    if (!m_timeZone.isValid()) m_timeZone = QTimeZone::utc();
    m_chromeDirection = m_locale.textDirection() == Qt::RightToLeft
                            ? QStringLiteral("rtl") : QStringLiteral("ltr");
}

void LocaleService::setLocaleName(const QString &name)
{
    const QLocale locale(name);
    if (locale.language() == QLocale::C || locale.name().compare(name, Qt::CaseInsensitive) != 0) {
        emit rejected(QStringLiteral("localeName"), name);
        return;
    }
    if (m_locale == locale) return;
    m_locale = locale;
    emit localeChanged();
    emit changed();
}

void LocaleService::setTimeZoneId(const QString &id)
{
    const auto ids = QTimeZone::availableTimeZoneIds();
    if (!ids.contains(id.toUtf8())) {
        emit rejected(QStringLiteral("timeZoneId"), id);
        return;
    }
    const QTimeZone zone(id.toUtf8());
    if (!zone.isValid()) { emit rejected(QStringLiteral("timeZoneId"), id); return; }
    if (m_timeZone == zone) return;
    m_timeZone = zone;
    emit timeZoneChanged();
    emit changed();
}

void LocaleService::setNumberingSystem(const QString &system)
{
    if (system != QStringLiteral("locale") && system != QStringLiteral("latn")) {
        emit rejected(QStringLiteral("numberingSystem"), system);
        return;
    }
    if (m_numberingSystem == system) return;
    m_numberingSystem = system;
    emit numberingSystemChanged();
    emit changed();
}

void LocaleService::setCalendarId(const QString &id)
{
    const auto calendars = QCalendar::availableCalendars();
    if (!calendars.contains(id, Qt::CaseInsensitive)) {
        emit rejected(QStringLiteral("calendarId"), id);
        return;
    }
    const QString canonical = *std::find_if(calendars.cbegin(), calendars.cend(),
        [&id](const QString &candidate) { return candidate.compare(id, Qt::CaseInsensitive) == 0; });
    if (m_calendarId == canonical) return;
    m_calendarId = canonical;
    emit calendarChanged();
    emit changed();
}

void LocaleService::setChromeDirection(const QString &direction)
{
    if (direction != QStringLiteral("ltr") && direction != QStringLiteral("rtl")) {
        emit rejected(QStringLiteral("chromeDirection"), direction);
        return;
    }
    if (m_chromeDirection == direction) return;
    m_chromeDirection = direction;
    emit chromeDirectionChanged();
    emit changed();
}

QString LocaleService::mapDigitsToLocale(QString text) const
{
    if (m_numberingSystem != QStringLiteral("locale")) return text;
    const QString zero = m_locale.zeroDigit();
    if (zero.size() != 1 || zero.front() == QLatin1Char('0')) return text;
    for (int digit = 0; digit < 10; ++digit)
        text.replace(QChar(QLatin1Char(char('0' + digit))), QChar(zero.front().unicode() + digit));
    return text;
}

QString LocaleService::mapDigitsToLatin(QString text) const
{
    const QString zero = m_locale.zeroDigit();
    if (zero.size() != 1 || zero.front() == QLatin1Char('0')) return text;
    for (int digit = 0; digit < 10; ++digit)
        text.replace(QChar(zero.front().unicode() + digit), QChar(QLatin1Char(char('0' + digit))));
    return text;
}

QString LocaleService::formatNumber(double value, int precision) const
{
    if (!qIsFinite(value)) return {};
    QString formatted = precision < 0 ? m_locale.toString(value, 'g', 15)
                                      : m_locale.toString(value, 'f', qBound(0, precision, 15));
    return m_numberingSystem == QStringLiteral("latn") ? mapDigitsToLatin(formatted)
                                                        : mapDigitsToLocale(formatted);
}

QVariantMap LocaleService::parseNumber(const QString &text) const
{
    bool ok = false;
    const double value = m_locale.toDouble(mapDigitsToLatin(text).trimmed(), &ok);
    if (!ok || !qIsFinite(value)) return failure(QStringLiteral("Invalid finite number"));
    return success(value);
}

QString LocaleService::formatDateTime(const QDateTime &value, const QString &format) const
{
    if (!value.isValid()) return {};
    const QDateTime local = value.toTimeZone(m_timeZone);
    const QCalendar calendar(m_calendarId);
    const QString pattern = format.isEmpty() ? QStringLiteral("yyyy-MM-dd HH:mm:ss t") : format;
    const QString formatted = m_locale.toString(local, QStringView(pattern), calendar);
    return m_numberingSystem == QStringLiteral("latn") ? mapDigitsToLatin(formatted)
                                                        : mapDigitsToLocale(formatted);
}

QVariantMap LocaleService::parseDateTime(const QString &text, const QString &format) const
{
    if (format.isEmpty()) return failure(QStringLiteral("A date/time format is required"));
    const QCalendar calendar(m_calendarId);
    const QString latinText = mapDigitsToLatin(text);
    const QDateTime parsed = m_locale.toDateTime(latinText, format, calendar);
    if (!parsed.isValid()) return failure(QStringLiteral("Invalid date/time"));
    QDateTime inZone(parsed.date(), parsed.time(), m_timeZone);
    if (!inZone.isValid()) return failure(QStringLiteral("Date/time is invalid in selected time zone"));
    return success(inZone);
}

QVariantMap LocaleService::classifyLocalDateTime(const QString &text,
                                                  const QString &format) const
{
    if (format.isEmpty())
        return {{QStringLiteral("status"), QStringLiteral("invalid")},
                {QStringLiteral("error"), QStringLiteral("A date/time format is required")}};
    const QCalendar calendar(m_calendarId);
    // Locale's default local-zone parser normalizes DST gaps before we can
    // inspect the requested wall time. Parse the fields under UTC first, then
    // resolve those unchanged fields in the configured target zone.
    const QDateTime parsed = m_locale.toDateTime(mapDigitsToLatin(text) + QStringLiteral(" UTC"),
                                                 format + QStringLiteral(" t"), calendar);
    if (!parsed.isValid())
        return {{QStringLiteral("status"), QStringLiteral("invalid")},
                {QStringLiteral("error"), QStringLiteral("Invalid local date/time")}};
    const QDate date = parsed.date();
    const QTime time = parsed.time();
    const auto strict = QDateTime(date, time, m_timeZone,
                                  QDateTime::TransitionResolution::Reject);
    if (strict.isValid())
        return {{QStringLiteral("status"), QStringLiteral("valid")},
                {QStringLiteral("candidates"), QVariantList{
                    strict.toUTC().toString(Qt::ISODateWithMs)}},
                {QStringLiteral("error"), QString{}}};
    const auto before = QDateTime(date, time, m_timeZone,
                                  QDateTime::TransitionResolution::PreferBefore);
    const auto after = QDateTime(date, time, m_timeZone,
                                 QDateTime::TransitionResolution::PreferAfter);
    const auto sameWallTime = [&](const QDateTime &candidate) {
        return candidate.isValid() && candidate.date() == date && candidate.time() == time;
    };
    if (!sameWallTime(before) || !sameWallTime(after)
        || before.toMSecsSinceEpoch() == after.toMSecsSinceEpoch())
        return {{QStringLiteral("status"), QStringLiteral("nonexistent")},
                {QStringLiteral("candidates"), QVariantList{}},
                {QStringLiteral("error"), QStringLiteral("Local time does not exist in selected zone")}};
    QVariantList candidates;
    const auto append = [&](const QDateTime &candidate) {
        if (!sameWallTime(candidate)) return;
        const QString utc = candidate.toUTC().toString(Qt::ISODateWithMs);
        if (!candidates.contains(utc)) candidates.append(utc);
    };
    append(before);
    append(after);
    std::sort(candidates.begin(), candidates.end(), [](const QVariant &a, const QVariant &b) {
        return a.toString() < b.toString();
    });
    return {{QStringLiteral("status"), QStringLiteral("ambiguous")},
            {QStringLiteral("candidates"), candidates},
            {QStringLiteral("error"), QString{}}};
}

QString LocaleService::formatDate(const QString &dateIso, const QString &format) const
{
    const QDate date = QDate::fromString(dateIso, Qt::ISODate);
    if (!date.isValid()) return {};
    const QString pattern = format.isEmpty() ? QStringLiteral("yyyy-MM-dd") : format;
    const QString text = m_locale.toString(date, QStringView(pattern), QCalendar(m_calendarId));
    return m_numberingSystem == QStringLiteral("latn") ? mapDigitsToLatin(text)
                                                        : mapDigitsToLocale(text);
}

QVariantMap LocaleService::parseDate(const QString &text, const QString &format) const
{
    if (format.isEmpty()) return failure(QStringLiteral("A date format is required"));
    const QDate date = m_locale.toDate(mapDigitsToLatin(text), format, QCalendar(m_calendarId));
    if (!date.isValid()) return failure(QStringLiteral("Invalid date"));
    return success(date.toString(Qt::ISODate));
}

QVariantMap LocaleService::monthGrid(int year, int month, const QString &minDateIso,
                                     const QString &maxDateIso) const
{
    const QCalendar calendar(m_calendarId);
    const QDate first = calendar.dateFromParts(year, month, 1);
    const QDate minimum = minDateIso.isEmpty() ? QDate{} : QDate::fromString(minDateIso, Qt::ISODate);
    const QDate maximum = maxDateIso.isEmpty() ? QDate{} : QDate::fromString(maxDateIso, Qt::ISODate);
    if (!first.isValid() || (!minDateIso.isEmpty() && !minimum.isValid())
        || (!maxDateIso.isEmpty() && !maximum.isValid())
        || (minimum.isValid() && maximum.isValid() && minimum > maximum))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Invalid calendar month or date bounds")}};
    const int startDay = int(m_locale.firstDayOfWeek());
    const int offset = (calendar.dayOfWeek(first) - startDay + 7) % 7;
    const QDate gridStart = first.addDays(-offset);
    QVariantList weekdays;
    QVariantList cells;
    for (int index = 0; index < 7; ++index)
        weekdays.append(calendar.standaloneWeekDayName(m_locale, (startDay + index - 1) % 7 + 1,
                                                       QLocale::ShortFormat));
    for (int index = 0; index < 42; ++index) {
        const QDate date = gridStart.addDays(index);
        const auto parts = calendar.partsFromDate(date);
        const bool enabled = (!minimum.isValid() || date >= minimum)
            && (!maximum.isValid() || date <= maximum);
        cells.append(QVariantMap{{QStringLiteral("date"), date.toString(Qt::ISODate)},
                                 {QStringLiteral("day"), parts.day},
                                 {QStringLiteral("label"), mapDigitsToLocale(m_locale.toString(parts.day))},
                                 {QStringLiteral("inMonth"), parts.year == year && parts.month == month},
                                 {QStringLiteral("enabled"), enabled},
                                 {QStringLiteral("today"), date == QDate::currentDate()}});
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("year"), year}, {QStringLiteral("month"), month},
            {QStringLiteral("monthLabel"),
                calendar.standaloneMonthName(m_locale, month, year) + QLatin1Char(' ')
                    + mapDigitsToLocale(yearText(m_locale, year))},
            {QStringLiteral("firstDayOfWeek"), startDay},
            {QStringLiteral("weekdayLabels"), weekdays},
            {QStringLiteral("cells"), cells}};
}

QVariantMap LocaleService::shiftMonth(int year, int month, int delta) const
{
    const QCalendar calendar(m_calendarId);
    if (!calendar.isDateValid(year, month, 1) || delta < -1200 || delta > 1200)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Invalid calendar month or shift")}};
    while (delta > 0) {
        if (month < calendar.monthsInYear(year)) ++month;
        else {
            ++year;
            if (year == 0 && !calendar.hasYearZero()) ++year;
            month = 1;
        }
        --delta;
    }
    while (delta < 0) {
        if (month > 1) --month;
        else {
            --year;
            if (year == 0 && !calendar.hasYearZero()) --year;
            month = calendar.monthsInYear(year);
        }
        ++delta;
    }
    if (!calendar.isDateValid(year, month, 1))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Calendar month is outside supported range")}};
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("year"), year}, {QStringLiteral("month"), month}};
}

QVariantMap LocaleService::calendarFields(const QString &dateIso) const
{
    const QDate date = QDate::fromString(dateIso, Qt::ISODate);
    if (!date.isValid())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Invalid date")}};
    const auto parts = QCalendar(m_calendarId).partsFromDate(date);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("year"), parts.year},
            {QStringLiteral("month"), parts.month},
            {QStringLiteral("day"), parts.day}};
}

QString LocaleService::todayDateIso() const
{
    return QDate::currentDate().toString(Qt::ISODate);
}

QString LocaleService::formatUnit(double value, const QString &unit, int precision) const
{
    if (!qIsFinite(value) || unit.trimmed().isEmpty()) return {};
    return formatNumber(value, precision) + QLatin1Char(' ') + unit;
}

QString LocaleService::contentDirection(const QString &text) const
{
    for (const QChar ch : text) {
        switch (ch.direction()) {
        case QChar::DirR: case QChar::DirAL: case QChar::DirRLE: case QChar::DirRLO:
        case QChar::DirRLI:
            return QStringLiteral("rtl");
        case QChar::DirL: case QChar::DirLRE: case QChar::DirLRO: case QChar::DirLRI:
            return QStringLiteral("ltr");
        default: break;
        }
    }
    return QStringLiteral("ltr");
}

} // namespace QindaTK
