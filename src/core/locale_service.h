// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QLocale>
#include <QTimeZone>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Per-window or per-document presentation policy. Values stored by domain
// models remain canonical; this service only formats and parses UI strings.
class LocaleService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString localeName READ localeName WRITE setLocaleName NOTIFY localeChanged)
    Q_PROPERTY(QString timeZoneId READ timeZoneId WRITE setTimeZoneId NOTIFY timeZoneChanged)
    Q_PROPERTY(QString numberingSystem READ numberingSystem WRITE setNumberingSystem NOTIFY numberingSystemChanged)
    Q_PROPERTY(QString calendarId READ calendarId WRITE setCalendarId NOTIFY calendarChanged)
    Q_PROPERTY(QString chromeDirection READ chromeDirection WRITE setChromeDirection NOTIFY chromeDirectionChanged)

public:
    explicit LocaleService(QObject *parent = nullptr);

    QString localeName() const { return m_locale.name(); }
    void setLocaleName(const QString &name);
    QString timeZoneId() const { return QString::fromUtf8(m_timeZone.id()); }
    void setTimeZoneId(const QString &id);
    QString numberingSystem() const { return m_numberingSystem; }
    void setNumberingSystem(const QString &system);
    QString calendarId() const { return m_calendarId; }
    void setCalendarId(const QString &id);
    QString chromeDirection() const { return m_chromeDirection; }
    void setChromeDirection(const QString &direction);

    Q_INVOKABLE QString formatNumber(double value, int precision = -1) const;
    Q_INVOKABLE QVariantMap parseNumber(const QString &text) const;
    Q_INVOKABLE QString formatDateTime(const QDateTime &value,
                                       const QString &format = QString()) const;
    Q_INVOKABLE QVariantMap parseDateTime(const QString &text,
                                          const QString &format) const;
    // Local wall-clock parsing is classified before an instant is chosen.
    // Candidate instants are exact ISO-8601 UTC strings, never JS dates.
    Q_INVOKABLE QVariantMap classifyLocalDateTime(const QString &text,
                                                  const QString &format) const;
    Q_INVOKABLE QString formatDate(const QString &dateIso,
                                   const QString &format = QString()) const;
    Q_INVOKABLE QVariantMap parseDate(const QString &text, const QString &format) const;
    // Date-only calendar projection; cell IDs remain Gregorian ISO dates.
    Q_INVOKABLE QVariantMap monthGrid(int year, int month,
                                     const QString &minDateIso = {},
                                     const QString &maxDateIso = {}) const;
    Q_INVOKABLE QVariantMap shiftMonth(int year, int month, int delta) const;
    Q_INVOKABLE QVariantMap calendarFields(const QString &dateIso) const;
    Q_INVOKABLE QString todayDateIso() const;
    Q_INVOKABLE QString formatUnit(double value, const QString &unit,
                                   int precision = -1) const;
    // Returns the direction of the first strong character. Document content
    // direction is intentionally independent of chromeDirection.
    Q_INVOKABLE QString contentDirection(const QString &text) const;

signals:
    void changed();
    void localeChanged();
    void timeZoneChanged();
    void numberingSystemChanged();
    void calendarChanged();
    void chromeDirectionChanged();
    void rejected(const QString &setting, const QString &value);

private:
    QLocale m_locale;
    QTimeZone m_timeZone;
    QString m_numberingSystem = QStringLiteral("locale");
    QString m_calendarId = QStringLiteral("gregorian");
    QString m_chromeDirection = QStringLiteral("ltr");
    QString mapDigitsToLocale(QString text) const;
    QString mapDigitsToLatin(QString text) const;
};

} // namespace QindaTK
