// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QDate>
#include <climits>
#include <QObject>
#include <QTime>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Expands local-wall-time event rules for CalendarView and RecurrenceEditor.
// The host remains responsible for event storage and provider synchronization.
// AGENT-CONTRACT (local_calendar_projection_model.cpp, local_productivity_events.cpp,
// ical_codec.cpp): a date-only start ("yyyy-MM-dd") is an all-day series. Its
// occurrences have a date-only localStart/occurrenceId, span whole local days
// (durationSeconds / 86400, at least one) and begin at the zone's start of day,
// so a DST change never shortens or skips them. Timed starts accept
// "yyyy-MM-ddTHH:mm[:ss]" or a space separator.
// AGENT-NOTE (RFC 5545 section 3.3.10): a monthly rule on the 29th-31st and a
// yearly rule on 29 February skip the months or years without that date; the
// skipped dates do not count toward `count` (docs/productivity-store.md).
class RecurrenceService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit RecurrenceService(QObject *parent = nullptr);
    QString error() const { return m_error; }

    Q_INVOKABLE QVariantList expand(const QString &eventId,
                                     const QString &startLocal,
                                     int durationSeconds,
                                     const QString &zoneId,
                                     const QVariantMap &rule,
                                     const QString &rangeStartUtc,
                                     const QString &rangeEndUtc,
                                     int limit = 200);
    // The series occurrence whose original local start is `occurrenceLocal`
    // ("yyyy-MM-ddTHH:mm:ss", or "yyyy-MM-dd" for all-day), or an empty map
    // when the rule does not produce it (excluded dates included).
    Q_INVOKABLE QVariantMap occurrenceAt(const QString &eventId,
                                         const QString &startLocal,
                                         int durationSeconds,
                                         const QString &zoneId,
                                         const QVariantMap &rule,
                                         const QString &occurrenceLocal);

    // spanSeconds() for QML hosts (preview durations).
    Q_INVOKABLE int durationSeconds(const QString &startLocal, const QString &endLocal,
                                    const QString &zoneId) const
    {
        const qint64 span = spanSeconds(startLocal, endLocal, zoneId);
        return span < 0 || span > INT_MAX ? -1 : int(span);
    }
    // Parses a stored local start/end; returns false for malformed text.
    static bool parseLocal(const QString &text, QDate *date, QTime *time, bool *dateOnly);
    // The duration to pass to expand(): whole days * 86400 for an all-day
    // span, elapsed seconds in `zoneId` otherwise; -1 when invalid.
    static qint64 spanSeconds(const QString &startLocal, const QString &endLocal, const QString &zoneId);
    // "yyyy-MM-ddTHH:mm:ss" (or "yyyy-MM-dd") for any accepted local text.
    static QString canonicalLocal(const QString &text);
signals:
    void errorChanged();
private:
    void setError(const QString &error);
    QString m_error;
};

} // namespace QindaTK
