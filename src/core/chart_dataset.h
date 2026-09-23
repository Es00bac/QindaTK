// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "chart_series.h"

#include <QAbstractListModel>
#include <QColor>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Bounded QML ingestion boundary. Date coordinates enter as decimal strings,
// never as JavaScript Numbers; model rows retain the source point IDs.
class ChartDataset : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString seriesId READ id WRITE setId NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY changed)
    Q_PROPERTY(QString stackKey READ stackKey WRITE setStackKey NOTIFY changed)
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY changed)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY changed)
    Q_PROPERTY(QString sizeUnit READ sizeUnit WRITE setSizeUnit NOTIFY changed)
    Q_PROPERTY(double baseline READ baseline WRITE setBaseline NOTIFY changed)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY changed)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(bool hasColor READ hasColor NOTIFY changed)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Kind { Line, Step, Area, StackedArea, Bar, StackedBar, Waterfall,
                Scatter, Bubble, Vector };
    Q_ENUM(Kind)
    enum Role { PointIdRole = Qt::UserRole + 1, XRole, UtcMsRole, YRole,
                CategoryRole, SizeRole, DxRole, DyRole, GapRole, TotalRole };
    Q_ENUM(Role)
    explicit ChartDataset(QObject *parent = nullptr);
    QString id() const { return m_series.id(); }
    void setId(const QString &v);
    QString name() const { return m_series.name(); }
    void setName(const QString &v);
    Kind kind() const { return Kind(m_series.kind()); }
    void setKind(Kind v) { m_series.setKind(ChartSeries::Kind(v)); emit changed(); }
    QString stackKey() const { return m_series.stackKey(); }
    void setStackKey(const QString &v) { m_series.setStackKey(v); emit changed(); }
    QString xUnit() const { return m_series.xUnit(); }
    void setXUnit(const QString &v);
    QString yUnit() const { return m_series.yUnit(); }
    void setYUnit(const QString &v);
    QString sizeUnit() const { return m_series.sizeUnit(); }
    void setSizeUnit(const QString &v) { m_series.setSizeUnit(v); emit changed(); }
    double baseline() const { return m_series.baseline(); }
    void setBaseline(double v) { m_series.setBaseline(v); emit changed(); }
    bool isVisible() const { return m_series.isVisible(); }
    void setVisible(bool v);
    int capacity() const { return m_series.capacity(); }
    void setCapacity(int v);
    QColor color() const { return m_color; }
    bool hasColor() const { return m_color.isValid(); }
    void setColor(const QColor &v);
    QColor fillColor() const { return m_fillColor; }
    void setFillColor(const QColor &v);
    int rowCount(const QModelIndex &parent = {}) const override;
    int count() const { return m_series.count(); }
    QString lastError() const { return m_lastError; }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE bool appendPoint(const QVariantMap &row);
    Q_INVOKABLE bool replacePoints(const QVariantList &rows);
    Q_INVOKABLE bool removePoint(const QString &pointId);
    Q_INVOKABLE QVariantMap get(int row) const;
    const ChartSeries *series() const { return &m_series; }
    ChartSeries *series() { return &m_series; }
signals:
    void changed();
    void rowsChanged();
    void capacityChanged();
    void errorChanged();
    void tableMetadataChanged();
private:
    bool parse(const QVariantMap &row, ChartPoint *point, QString *error) const;
    void setLastError(const QString &error);
    ChartSeries m_series;
    QColor m_color;
    QColor m_fillColor;
    QString m_lastError;
};

} // namespace QindaTK
