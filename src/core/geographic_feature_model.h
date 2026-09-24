// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QJsonObject>
#include <QPointF>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

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
    enum Roles { IdRole = Qt::UserRole + 1, TypeRole, PropertiesRole };
    QString attribution() const { return m_attribution; }
    void setAttribution(const QString &value);
    QString sourcePath() const { return m_sourcePath; }
    QString lastError() const { return m_error; }
    int count() const { return m_features.size(); }
    QString crs() const { return QStringLiteral("EPSG:4326 → EPSG:3857"); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE bool loadGeoJson(const QString &localPath);
    Q_INVOKABLE bool saveGeoJson(const QString &localPath);
    Q_INVOKABLE QVariantMap featureById(const QString &id) const;
    Q_INVOKABLE QVariantMap featureAt(int index) const;
    Q_INVOKABLE QVariantMap project(double longitude, double latitude) const;
    Q_INVOKABLE QVariantMap unproject(double x, double y) const;
    static QPointF mercator(double longitude, double latitude);
    static QPointF geographic(double x, double y);
    static constexpr double latitudeLimit = 85.0511287798066;
    const QVector<Feature> &features() const { return m_features; }
signals:
    void changed();
    void errorChanged();
private:
    void setError(const QString &value);
    static QString pathFromInput(const QString &input);
    QVector<Feature> m_features;
    QString m_attribution, m_sourcePath, m_error;
};
}
