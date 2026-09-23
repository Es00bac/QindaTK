// SPDX-License-Identifier: LGPL-3.0-or-later
#include "locale_service.h"

#include <QCalendar>
#include <QChar>
#include <QDateTime>
#include <QtMath>
#include <algorithm>

namespace QindaTK {
namespace {
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
