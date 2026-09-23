// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A selected contiguous set of source bins expressed in its declared domain.
// The last source bin alone includes the final upper edge.
class ChartHistogramSelection : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Selections are owned by ChartRenderer")
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(int firstBin READ firstBin NOTIFY changed)
    Q_PROPERTY(int lastBin READ lastBin NOTIFY changed)
    Q_PROPERTY(double lower READ lower NOTIFY changed)
    Q_PROPERTY(double upper READ upper NOTIFY changed)
    Q_PROPERTY(bool lowerInclusive READ lowerInclusive NOTIFY changed)
    Q_PROPERTY(bool upperInclusive READ upperInclusive NOTIFY changed)
    Q_PROPERTY(QString unit READ unit NOTIFY changed)
    Q_PROPERTY(QString channel READ channel NOTIFY changed)
    Q_PROPERTY(int normalization READ normalization NOTIFY changed)
public:
    explicit ChartHistogramSelection(QObject *parent = nullptr) : QObject(parent) {}
    bool valid() const { return m_valid; }
    int firstBin() const { return m_first; }
    int lastBin() const { return m_last; }
    double lower() const { return m_lower; }
    double upper() const { return m_upper; }
    bool lowerInclusive() const { return true; }
    bool upperInclusive() const { return m_final; }
    QString unit() const { return m_unit; }
    QString channel() const { return m_channel; }
    int normalization() const { return m_normalization; }
    void assign(int first, int last, double lower, double upper, bool final,
                const QString &unit, const QString &channel, int normalization);
    void clear();
signals:
    void changed();
private:
    bool m_valid = false, m_final = false;
    int m_first = -1, m_last = -1, m_normalization = 0;
    double m_lower = 0, m_upper = 0;
    QString m_unit, m_channel;
};

} // namespace QindaTK
