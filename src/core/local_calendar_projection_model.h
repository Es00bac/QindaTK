// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_productivity_store.h"
#include "recurrence_service.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Projects stored local calendar records into a UTC-bounded CalendarView agenda.
class LocalCalendarProjectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(LocalProductivityStore *store READ store WRITE setStore NOTIFY inputsChanged)
    Q_PROPERTY(RecurrenceService *recurrenceService READ recurrenceService WRITE setRecurrenceService NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList occurrences READ occurrences NOTIFY occurrencesChanged)
    Q_PROPERTY(QString rangeStartUtc READ rangeStartUtc NOTIFY rangeChanged)
    Q_PROPERTY(QString rangeEndUtc READ rangeEndUtc NOTIFY rangeChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit LocalCalendarProjectionModel(QObject *parent = nullptr);
    LocalProductivityStore *store() const { return m_store; }
    void setStore(LocalProductivityStore *store);
    RecurrenceService *recurrenceService() const { return m_recurrenceService; }
    void setRecurrenceService(RecurrenceService *service);
    QVariantList occurrences() const { return m_occurrences; }
    QString rangeStartUtc() const { return m_rangeStartUtc; }
    QString rangeEndUtc() const { return m_rangeEndUtc; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool project(const QString &rangeStartUtc, const QString &rangeEndUtc,
                             int limit = 500);

signals:
    void inputsChanged();
    void occurrencesChanged();
    void rangeChanged();
    void errorChanged();

private:
    bool projectCurrent(int limit = 500);
    void setError(const QString &error);

    LocalProductivityStore *m_store = nullptr;
    RecurrenceService *m_recurrenceService = nullptr;
    QVariantList m_occurrences;
    QString m_rangeStartUtc, m_rangeEndUtc, m_error;
};

} // namespace QindaTK
