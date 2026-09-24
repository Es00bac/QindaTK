// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Expands local-wall-time event rules for CalendarView and RecurrenceEditor.
// The host remains responsible for event storage and provider synchronization.
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
signals:
    void errorChanged();
private:
    void setError(const QString &error);
    QString m_error;
};

} // namespace QindaTK
