// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct PolarPoint {
    enum Kind { Points, Polyline, Polygon, RadialBar };
    QString seriesId;
    QString seriesName;
    QString pointId;
    QString category;
    double angle = 0;
    double radius = 0;
    Kind kind = Points;
    bool hasCategory = false;
};

// AGENT-GUARD: replacement is all-or-nothing. Stable (seriesId, pointId)
// identity and source values survive rendering order, table access and CSV.
class PolarDataModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Kind { Points = PolarPoint::Points, Polyline = PolarPoint::Polyline,
                Polygon = PolarPoint::Polygon, RadialBar = PolarPoint::RadialBar };
    Q_ENUM(Kind)
    enum Role { SeriesIdRole = Qt::UserRole + 1, SeriesNameRole, PointIdRole,
                AngleRole, CategoryRole, RadiusRole, KindRole };
    Q_ENUM(Role)
    explicit PolarDataModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int count() const { return m_points.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString lastError() const { return m_error; }
    const QVector<PolarPoint> &points() const { return m_points; }
    Q_INVOKABLE bool replacePoints(const QVariantList &rows);
    Q_INVOKABLE bool appendPoint(const QVariantMap &row);
    Q_INVOKABLE QVariantMap get(int row) const;
signals:
    void rowsChanged();
    void errorChanged();
private:
    static bool parse(const QVariantMap &row, PolarPoint *point, QString *error);
    static bool validateSeries(const QVector<PolarPoint> &points, QString *error);
    void setError(const QString &message);
    QVector<PolarPoint> m_points;
    QString m_error;
};

} // namespace QindaTK
