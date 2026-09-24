// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_feature_model.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <cmath>
#include <algorithm>
#include <numbers>

namespace QindaTK {
namespace {
bool coordinate(const QJsonValue &value)
{
    const auto a = value.toArray();
    if (a.size() < 2 || !a[0].isDouble() || !a[1].isDouble()) return false;
    const double lon = a[0].toDouble(), lat = a[1].toDouble();
    return std::isfinite(lon) && std::isfinite(lat) && lon >= -180 && lon <= 180
        && lat >= -90 && lat <= 90;
}
bool coordinates(const QJsonValue &value, int depth)
{
    const auto a = value.toArray();
    if (a.isEmpty()) return false;
    if (depth == 0) return coordinate(value);
    for (const auto &v : a) if (!coordinates(v, depth - 1)) return false;
    return true;
}
int geometryDepth(const QString &type)
{
    if (type == QLatin1String("Point")) return 0;
    if (type == QLatin1String("MultiPoint") || type == QLatin1String("LineString")) return 1;
    if (type == QLatin1String("MultiLineString") || type == QLatin1String("Polygon")) return 2;
    if (type == QLatin1String("MultiPolygon")) return 3;
    return -1;
}
QString jsonId(const QJsonValue &v)
{
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 16);
    return {};
}
QVariantMap view(const GeographicFeatureModel::Feature &f)
{
    return {{QStringLiteral("id"), f.id}, {QStringLiteral("type"), f.type},
            {QStringLiteral("geometry"), f.geometry.toVariantMap()},
            {QStringLiteral("properties"), f.properties}};
}
}
GeographicFeatureModel::GeographicFeatureModel(QObject *parent) : QAbstractListModel(parent) {}
void GeographicFeatureModel::setAttribution(const QString &value)
{ if (m_attribution == value) return; m_attribution = value; emit changed(); }
void GeographicFeatureModel::setError(const QString &value)
{ if (m_error == value) return; m_error = value; emit errorChanged(); }
int GeographicFeatureModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_features.size(); }
QVariant GeographicFeatureModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto &f = m_features.at(index.row());
    switch (role) { case IdRole: return f.id; case TypeRole: return f.type;
        case PropertiesRole: return f.properties;
        case GeometryRole: return f.geometry.toVariantMap();
        default: return {}; }
}
QHash<int, QByteArray> GeographicFeatureModel::roleNames() const
{ return {{IdRole, "featureId"}, {TypeRole, "geometryType"},
          {PropertiesRole, "attributes"}, {GeometryRole, "geometry"}}; }
QString GeographicFeatureModel::pathFromInput(const QString &input)
{
    const QUrl url(input);
    if (url.scheme().isEmpty()) return input;
    return url.isLocalFile() ? url.toLocalFile() : QString();
}
bool GeographicFeatureModel::loadGeoJson(const QString &localPath)
{
    const auto path = pathFromInput(localPath);
    if (path.isEmpty()) { setError(QStringLiteral("Only local GeoJSON files are supported")); return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { setError(file.errorString()); return false; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError) { setError(error.errorString()); return false; }
    if (root.value("type") != QLatin1String("FeatureCollection")) {
        setError(QStringLiteral("Expected a GeoJSON FeatureCollection")); return false;
    }
    // AGENT-GUARD: never silently interpret a declared alternate CRS as WGS84.
    if (root.contains("crs")) {
        setError(QStringLiteral("GeoJSON CRS overrides are unsupported; coordinates must be WGS84")); return false;
    }
    const auto rows = root.value("features");
    if (!rows.isArray()) { setError(QStringLiteral("GeoJSON features must be an array")); return false; }
    QVector<Feature> parsed;
    QSet<QString> ids;
    int row = 0;
    for (const auto &entry : rows.toArray()) {
        ++row;
        const auto object = entry.toObject(), geometry = object.value("geometry").toObject();
        const auto type = geometry.value("type").toString();
        const int depth = geometryDepth(type);
        if (object.value("type") != QLatin1String("Feature") || depth < 0
            || !coordinates(geometry.value("coordinates"), depth)
            || !object.value("properties").isObject()) {
            setError(QStringLiteral("Invalid or unsupported geometry at feature %1").arg(row)); return false;
        }
        QString id = jsonId(object.value("id"));
        if (id.isEmpty()) {
            QJsonObject identity{{"geometry", geometry}, {"properties", object.value("properties")}};
            id = QStringLiteral("geo-") + QString::fromLatin1(QCryptographicHash::hash(
                QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex().left(16));
        }
        if (ids.contains(id)) { setError(QStringLiteral("Duplicate feature ID: %1").arg(id)); return false; }
        ids.insert(id);
        parsed.append({id, type, geometry, object.value("properties").toObject().toVariantMap()});
    }
    beginResetModel(); m_features = std::move(parsed); m_spatialDirty = true; endResetModel();
    m_sourcePath = path;
    m_attribution = root.value("attribution").toString();
    setError({}); emit changed();
    return true;
}
bool GeographicFeatureModel::saveGeoJson(const QString &localPath)
{ return writeGeoJson(localPath, true); }
bool GeographicFeatureModel::exportGeoJson(const QString &localPath)
{ return writeGeoJson(localPath, false); }
bool GeographicFeatureModel::writeGeoJson(const QString &localPath, bool updateSourcePath)
{
    const auto path = pathFromInput(localPath);
    if (path.isEmpty()) { setError(QStringLiteral("Only local GeoJSON files are supported")); return false; }
    QJsonArray rows;
    for (const auto &f : m_features) rows.append(QJsonObject{{"type", "Feature"}, {"id", f.id},
        {"geometry", f.geometry}, {"properties", QJsonObject::fromVariantMap(f.properties)}});
    QJsonObject root{{"type", "FeatureCollection"}, {"features", rows}};
    if (!m_attribution.isEmpty()) root.insert("attribution", m_attribution);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit()) { setError(file.errorString()); return false; }
    if (updateSourcePath && m_sourcePath != path) { m_sourcePath = path; emit changed(); }
    setError({}); return true;
}
QVariantMap GeographicFeatureModel::featureById(const QString &id) const
{ for (const auto &f : m_features) if (f.id == id) return view(f); return {}; }
QVariantMap GeographicFeatureModel::featureAt(int index) const
{ return index >= 0 && index < count() ? view(m_features.at(index)) : QVariantMap{}; }
QPointF GeographicFeatureModel::mercator(double longitude, double latitude)
{
    const double lon = std::clamp(longitude, -180.0, 180.0);
    const double lat = std::clamp(latitude, -latitudeLimit, latitudeLimit);
    const double radians = lat * std::numbers::pi / 180.0;
    return {(lon + 180.0) / 360.0,
            (1.0 - std::asinh(std::tan(radians)) / std::numbers::pi) / 2.0};
}
QPointF GeographicFeatureModel::geographic(double x, double y)
{
    double lon = std::fmod(x * 360.0, 360.0);
    if (lon < 0) lon += 360.0;
    lon -= 180.0;
    const double lat = std::atan(std::sinh(std::numbers::pi * (1.0 - 2.0 * std::clamp(y, 0.0, 1.0)))) * 180.0 / std::numbers::pi;
    return {lon, lat};
}
QVariantMap GeographicFeatureModel::project(double longitude, double latitude) const
{
    if (!std::isfinite(longitude) || !std::isfinite(latitude) || longitude < -180 || longitude > 180
        || latitude < -90 || latitude > 90) return {{"valid", false}};
    const auto p = mercator(longitude, latitude);
    return {{"valid", true}, {"x", p.x()}, {"y", p.y()},
        {"latitudeClamped", std::abs(latitude) > latitudeLimit}};
}
QVariantMap GeographicFeatureModel::unproject(double x, double y) const
{
    if (!std::isfinite(x) || !std::isfinite(y)) return {{"valid", false}};
    const auto p = geographic(x, y);
    return {{"valid", true}, {"longitude", p.x()}, {"latitude", p.y()}};
}
}
