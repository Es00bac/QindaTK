// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_feature_model.h"
#include <QJsonArray>
#include <QUuid>
#include <cmath>

namespace QindaTK {
namespace {
bool validPoint(const QJsonValue &value)
{
    const auto point = value.toArray();
    if (point.size() != 2 || !point[0].isDouble() || !point[1].isDouble()) return false;
    const double lon = point[0].toDouble(), lat = point[1].toDouble();
    return std::isfinite(lon) && std::isfinite(lat) && lon >= -180 && lon <= 180
        && lat >= -90 && lat <= 90;
}
bool validShape(const QString &type, const QJsonValue &value)
{
    if (type == QLatin1String("Point")) return validPoint(value);
    const auto parts = value.toArray();
    if (type == QLatin1String("LineString")) {
        if (parts.size() < 2) return false;
        for (const auto &point : parts) if (!validPoint(point)) return false;
        return true;
    }
    if (type == QLatin1String("Polygon")) {
        if (parts.isEmpty()) return false;
        for (const auto &ringValue : parts) {
            const auto ring = ringValue.toArray();
            if (ring.size() < 4 || ring.first() != ring.last()) return false;
            for (const auto &point : ring) if (!validPoint(point)) return false;
        }
        return true;
    }
    return false;
}
QJsonArray shiftedPoint(const QJsonValue &value, double dx, double dy)
{
    const auto point = value.toArray();
    return {point[0].toDouble() + dx, point[1].toDouble() + dy};
}
QJsonArray shifted(const QString &type, const QJsonValue &value, double dx, double dy)
{
    if (type == QLatin1String("Point")) return shiftedPoint(value, dx, dy);
    QJsonArray result;
    for (const auto &part : value.toArray()) {
        if (type == QLatin1String("LineString")) result.append(shiftedPoint(part, dx, dy));
        else {
            QJsonArray ring;
            for (const auto &point : part.toArray()) ring.append(shiftedPoint(point, dx, dy));
            result.append(ring);
        }
    }
    return result;
}
}

int GeographicFeatureModel::indexOf(const QString &id) const
{
    for (int row = 0; row < m_features.size(); ++row)
        if (m_features[row].id == id) return row;
    return -1;
}

QString GeographicFeatureModel::addFeature(const QString &type, const QVariant &coordinates,
                                            const QVariantMap &properties, const QString &preferredId)
{
    const auto value = QJsonValue::fromVariant(coordinates);
    if (!validShape(type, value)) {
        setError(QStringLiteral("Only valid WGS84 Point, LineString and Polygon geometry can be added"));
        return {};
    }
    QString id = preferredId;
    if (id.isEmpty()) id = QStringLiteral("geo-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (indexOf(id) >= 0) { setError(QStringLiteral("Duplicate feature ID: %1").arg(id)); return {}; }
    QJsonObject geometry{{"type", type}, {"coordinates", value}};
    const int row = m_features.size();
    beginInsertRows({}, row, row);
    m_features.append({id, type, geometry, properties});
    endInsertRows();
    setError({}); emit changed();
    return id;
}

bool GeographicFeatureModel::replaceGeometry(const QString &id, const QVariant &coordinates)
{
    const int row = indexOf(id);
    if (row < 0) { setError(QStringLiteral("Unknown feature ID: %1").arg(id)); return false; }
    auto &feature = m_features[row];
    const auto value = QJsonValue::fromVariant(coordinates);
    if (!validShape(feature.type, value)) {
        setError(QStringLiteral("Invalid editable WGS84 geometry")); return false;
    }
    if (feature.geometry.value("coordinates") == value) { setError({}); return true; }
    feature.geometry.insert("coordinates", value);
    emit dataChanged(index(row), index(row), {GeometryRole});
    setError({}); emit changed();
    return true;
}

bool GeographicFeatureModel::movePoint(const QString &id, double longitude, double latitude)
{
    const int row = indexOf(id);
    if (row < 0 || m_features[row].type != QLatin1String("Point")) {
        setError(QStringLiteral("movePoint requires an existing Point")); return false;
    }
    return replaceGeometry(id, QVariantList{longitude, latitude});
}

bool GeographicFeatureModel::translateFeature(const QString &id, double dx, double dy)
{
    const int row = indexOf(id);
    if (row < 0 || !std::isfinite(dx) || !std::isfinite(dy)) {
        setError(QStringLiteral("Invalid feature translation")); return false;
    }
    const auto &feature = m_features[row];
    if (feature.type != QLatin1String("Point") && feature.type != QLatin1String("LineString")
        && feature.type != QLatin1String("Polygon")) {
        setError(QStringLiteral("Only Point, LineString and Polygon translation is supported")); return false;
    }
    return replaceGeometry(id, shifted(feature.type, feature.geometry.value("coordinates"), dx, dy).toVariantList());
}

bool GeographicFeatureModel::setFeatureProperties(const QString &id, const QVariantMap &properties)
{
    const int row = indexOf(id);
    if (row < 0) { setError(QStringLiteral("Unknown feature ID: %1").arg(id)); return false; }
    if (m_features[row].properties == properties) { setError({}); return true; }
    m_features[row].properties = properties;
    emit dataChanged(index(row), index(row), {PropertiesRole});
    setError({}); emit changed();
    return true;
}

bool GeographicFeatureModel::removeFeature(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0) { setError(QStringLiteral("Unknown feature ID: %1").arg(id)); return false; }
    beginRemoveRows({}, row, row);
    m_features.removeAt(row);
    endRemoveRows();
    setError({}); emit changed();
    return true;
}
}
