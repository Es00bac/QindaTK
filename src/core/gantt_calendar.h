// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QDate>
#include <QTime>
#include <QTimeZone>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: Gantt dates cross QML as exact decimal UTC millisecond
// strings. A working minute is an elapsed UTC minute inside a local calendar
// window; this preserves the true duration through DST transitions.
class GanttCalendar : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString timeZoneId READ timeZoneId WRITE setTimeZoneId NOTIFY changed)
    Q_PROPERTY(int workingDays READ workingDays WRITE setWorkingDays NOTIFY changed)
    Q_PROPERTY(QTime workStart READ workStart WRITE setWorkStart NOTIFY changed)
    Q_PROPERTY(QTime workEnd READ workEnd WRITE setWorkEnd NOTIFY changed)
    Q_PROPERTY(QStringList holidays READ holidays WRITE setHolidays NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit GanttCalendar(QObject *parent = nullptr);
    QString timeZoneId() const { return QString::fromUtf8(m_zone.id()); }
    void setTimeZoneId(const QString &id);
    int workingDays() const { return m_workingDays; }
    void setWorkingDays(int days);
    QTime workStart() const { return m_start; }
    void setWorkStart(QTime time);
    QTime workEnd() const { return m_end; }
    void setWorkEnd(QTime time);
    QStringList holidays() const { return m_holidays; }
    void setHolidays(const QStringList &dates);
    QString lastError() const { return m_error; }

    Q_INVOKABLE double workingMinutesBetween(const QString &fromUtc,
                                               const QString &toUtc) const;
    Q_INVOKABLE QString addWorkingMinutes(const QString &fromUtc, qint64 minutes) const;
    Q_INVOKABLE QString snapForward(const QString &utc) const;
    Q_INVOKABLE QString formatLocal(const QString &utc) const;
    Q_INVOKABLE bool isWorkingDay(const QDate &date) const;
    static bool parseUtc(const QString &value, qint64 *milliseconds);
private:
    bool interval(QDate date, qint64 *start, qint64 *end) const;
    void error(const QString &message) const;
    QTimeZone m_zone;
    int m_workingDays = 0x1f; // Monday through Friday, bit 0 = Monday.
    QTime m_start{9, 0};
    QTime m_end{17, 0};
    QStringList m_holidays;
    mutable QString m_error;
signals:
    void changed();
    void errorChanged();
};

} // namespace QindaTK
