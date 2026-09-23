// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct ChartTick {
    double value = 0;
    QString label;
    qint64 utcMs = 0; // authoritative for DateTime ticks
};

// AGENT-CONTRACT: renderers use normalized coordinates [0,1]; category
// values are integer indices into categories(), never interpolated labels.
// DateTime values are signed 64-bit UTC milliseconds and labels use the
// declared time zone. Use mapUtc(), never a QML Number, for temporal data.
class ChartAxis : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY changed)
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY changed)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY changed)
    Q_PROPERTY(QStringList categories READ categories WRITE setCategories NOTIFY changed)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY changed)
    Q_PROPERTY(QString timeZoneId READ timeZoneId WRITE setTimeZoneId NOTIFY changed)
    Q_PROPERTY(QString minimumUtc READ minimumUtc WRITE setMinimumUtc NOTIFY changed)
    Q_PROPERTY(QString maximumUtc READ maximumUtc WRITE setMaximumUtc NOTIFY changed)
    Q_PROPERTY(bool reversed READ reversed WRITE setReversed NOTIFY changed)
    Q_PROPERTY(bool valid READ isValid NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    enum Kind { Numeric, Category, DateTime, Logarithmic };
    Q_ENUM(Kind)
    explicit ChartAxis(QObject *parent = nullptr);
    Kind kind() const { return m_kind; }
    void setKind(Kind value);
    double minimum() const { return m_minimum; }
    void setMinimum(double value);
    double maximum() const { return m_maximum; }
    void setMaximum(double value);
    QStringList categories() const { return m_categories; }
    void setCategories(const QStringList &value);
    QString unit() const { return m_unit; }
    void setUnit(const QString &value);
    QString timeZoneId() const { return m_timeZoneId; }
    void setTimeZoneId(const QString &value);
    QString minimumUtc() const;
    void setMinimumUtc(const QString &value);
    QString maximumUtc() const;
    void setMaximumUtc(const QString &value);
    void setUtcDomain(qint64 minimum, qint64 maximum);
    bool reversed() const { return m_reversed; }
    void setReversed(bool value);
    bool isValid() const;
    QString error() const;
    // Invalid input/domain yields NaN; callers must test isfinite().
    Q_INVOKABLE double map(double value) const;
    Q_INVOKABLE double unmap(double fraction) const;
    double mapUtc(qint64 milliseconds) const;
    qint64 unmapUtc(double fraction) const;
    Q_INVOKABLE int categoryIndex(const QString &category) const;
    QVector<ChartTick> ticks(int approximateCount = 5) const;
signals:
    void changed();
private:
    Kind m_kind = Numeric;
    double m_minimum = 0;
    double m_maximum = 1;
    QStringList m_categories;
    QString m_unit;
    QString m_timeZoneId = QStringLiteral("UTC");
    qint64 m_minimumUtc = 0;
    qint64 m_maximumUtc = 1000;
    bool m_utcDomainValid = true;
    bool m_reversed = false;
};

} // namespace QindaTK
