// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QJsonObject>
#include <QPointF>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <array>

namespace QindaTK {
// AGENT-CONTRACT: coordinates are GeoJSON WGS84 [longitude, latitude] degrees;
// projection results are normalized EPSG:3857 world coordinates (x east, y south).
class GeographicFeatureModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString attribution READ attribution WRITE setAttribution NOTIFY changed)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString crs READ crs CONSTANT)
public:
    struct Feature { QString id; QString type; QJsonObject geometry; QVariantMap properties; };
    explicit GeographicFeatureModel(QObject *parent = nullptr);
    enum Roles { IdRole = Qt::UserRole + 1, TypeRole, PropertiesRole, GeometryRole };
    QString attribution() const { return m_attribution; }
    void setAttribution(const QString &value);
    QString sourcePath() const { return m_sourcePath; }
    QString lastError() const { return m_error; }
    int count() const { return m_features.size(); }
    QString crs() const { return QStringLiteral("EPSG:4326"); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE bool loadGeoJson(const QString &localPath);
    Q_INVOKABLE bool saveGeoJson(const QString &localPath);
    Q_INVOKABLE bool exportGeoJson(const QString &localPath);
    Q_INVOKABLE QString addFeature(const QString &type, const QVariant &coordinates,
                                   const QVariantMap &properties = {}, const QString &id = {});
    Q_INVOKABLE bool replaceGeometry(const QString &id, const QVariant &coordinates);
    Q_INVOKABLE bool movePoint(const QString &id, double longitude, double latitude);
    Q_INVOKABLE bool translateFeature(const QString &id, double deltaLongitude, double deltaLatitude);
    Q_INVOKABLE bool setFeatureProperties(const QString &id, const QVariantMap &properties);
    Q_INVOKABLE bool removeFeature(const QString &id);
    Q_INVOKABLE QVariantMap featureById(const QString &id) const;
    Q_INVOKABLE QVariantMap featureAt(int index) const;
    Q_INVOKABLE QVariantMap project(double longitude, double latitude) const;
    Q_INVOKABLE QVariantMap unproject(double x, double y) const;
    static QPointF mercator(double longitude, double latitude);
    static QPointF geographic(double x, double y);
    static constexpr double latitudeLimit = 85.0511287798066;
    const QVector<Feature> &features() const { return m_features; }
    // Unwrapped longitude interval and WGS84 latitude interval; indices retain model order.
    QVector<int> queryFeatureIndices(double west, double east, double south, double north) const;
signals:
    void changed();
    void errorChanged();
private:
    bool writeGeoJson(const QString &localPath, bool updateSourcePath);
    int indexOf(const QString &id) const;
    void setError(const QString &value);
    static QString pathFromInput(const QString &input);
    QVector<Feature> m_features;
    struct Bounds { double west, east, south, north; };
    void rebuildSpatialIndex() const;
    mutable std::array<QVector<int>, 36 * 18> m_spatialCells;
    mutable QVector<int> m_spatialOverflow;
    mutable QVector<Bounds> m_spatialBounds;
    mutable bool m_spatialDirty = true;
    QString m_attribution, m_sourcePath, m_error;
};
}
