// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Point IDs are unique within a series and survive replacement, clipping,
// decimation, hit testing, table export and selection. A gap has an ID too.
struct ChartPoint {
    QString id;
    double x = 0;
    double y = 0;
    qint64 utcMs = 0;
    QString category;
    double size = 0; // bubble area data value, in sizeUnit
    double dx = 0;   // vector x displacement, in xUnit
    double dy = 0;   // vector y displacement, in yUnit
    bool gap = false;
    bool total = false; // waterfall absolute total; resets running value
};

// AGENT-CONTRACT: ChartSeries owns only data and semantic style. Plot geometry
// is computed by ChartData, and the renderer must use stable IDs from there.
class ChartSeries : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString id READ id WRITE setId NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY changed)
    Q_PROPERTY(QString stackKey READ stackKey WRITE setStackKey NOTIFY changed)
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY changed)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY changed)
    Q_PROPERTY(QString sizeUnit READ sizeUnit WRITE setSizeUnit NOTIFY changed)
    Q_PROPERTY(double baseline READ baseline WRITE setBaseline NOTIFY changed)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY changed)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY pointsChanged)
public:
    enum Kind { Line, Step, Area, StackedArea, Bar, StackedBar, Waterfall,
                Scatter, Bubble, Vector };
    Q_ENUM(Kind)
    explicit ChartSeries(QObject *parent = nullptr);
    QString id() const { return m_id; }
    void setId(const QString &value);
    QString name() const { return m_name; }
    void setName(const QString &value);
    Kind kind() const { return m_kind; }
    void setKind(Kind value);
    QString stackKey() const { return m_stackKey; }
    void setStackKey(const QString &value);
    QString xUnit() const { return m_xUnit; }
    void setXUnit(const QString &value);
    QString yUnit() const { return m_yUnit; }
    void setYUnit(const QString &value);
    QString sizeUnit() const { return m_sizeUnit; }
    void setSizeUnit(const QString &value);
    double baseline() const { return m_baseline; }
    void setBaseline(double value);
    bool isVisible() const { return m_visible; }
    void setVisible(bool value);
    int capacity() const { return m_capacity; }
    void setCapacity(int value);
    int count() const { return m_points.size(); }
    const QVector<ChartPoint> &points() const { return m_points; }
    // Reject duplicate/empty IDs, nonfinite non-gap values, and invalid
    // bubble/vector fields. Failure is atomic; error receives a reason.
    bool replacePoints(const QVector<ChartPoint> &points, QString *error = nullptr);
    bool appendPoint(const ChartPoint &point, QString *error = nullptr);
    bool removePoint(const QString &pointId);
    const ChartPoint *point(const QString &pointId) const;
    Q_INVOKABLE void clear();
signals:
    void changed();
    void pointsChanged();
private:
    bool validate(const ChartPoint &point, QString *error) const;
    QString m_id;
    QString m_name;
    QString m_stackKey;
    QString m_xUnit;
    QString m_yUnit;
    QString m_sizeUnit;
    QVector<ChartPoint> m_points;
    Kind m_kind = Line;
    double m_baseline = 0;
    int m_capacity = 100000;
    bool m_visible = true;
};

} // namespace QindaTK
