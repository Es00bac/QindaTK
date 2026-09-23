// SPDX-License-Identifier: LGPL-3.0-or-later
#include "polar_data_model.h"

#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int MaxPoints = 10000;
constexpr int MaxSeries = 32;
}

PolarDataModel::PolarDataModel(QObject *parent) : QAbstractListModel(parent) {}
int PolarDataModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_points.size(); }
QHash<int, QByteArray> PolarDataModel::roleNames() const
{
    return {{SeriesIdRole, "seriesId"}, {SeriesNameRole, "seriesName"},
            {PointIdRole, "pointId"}, {AngleRole, "angle"},
            {CategoryRole, "category"}, {RadiusRole, "radius"}, {KindRole, "kind"}};
}
QVariant PolarDataModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size()) return {};
    return get(index.row()).value(QString::fromLatin1(roleNames().value(role)));
}
QVariantMap PolarDataModel::get(int row) const
{
    if (row < 0 || row >= m_points.size()) return {};
    const PolarPoint &p = m_points.at(row);
    return {{QStringLiteral("seriesId"), p.seriesId},
            {QStringLiteral("seriesName"), p.seriesName},
            {QStringLiteral("pointId"), p.pointId},
            {QStringLiteral("angle"), p.hasCategory ? QVariant{} : QVariant(p.angle)},
            {QStringLiteral("category"), p.hasCategory ? QVariant(p.category) : QVariant{}},
            {QStringLiteral("radius"), p.radius}, {QStringLiteral("kind"), int(p.kind)}};
}
void PolarDataModel::setError(const QString &message)
{ if (m_error != message) { m_error = message; emit errorChanged(); } }

bool PolarDataModel::parse(const QVariantMap &row, PolarPoint *point, QString *error)
{
    point->seriesId = row.value(QStringLiteral("seriesId")).toString();
    point->pointId = row.value(QStringLiteral("pointId")).toString();
    point->seriesName = row.value(QStringLiteral("seriesName")).toString();
    if (point->seriesId.isEmpty() || point->seriesId.size() > 128
        || point->pointId.isEmpty() || point->pointId.size() > 128
        || point->seriesName.size() > 256) {
        *error = QStringLiteral("Series and point IDs are required and bounded"); return false;
    }
    if (point->seriesName.isEmpty()) point->seriesName = point->seriesId;
    bool kindOk = false;
    const int kind = row.value(QStringLiteral("kind"), int(Points)).toInt(&kindOk);
    if (!kindOk || kind < Points || kind > RadialBar) {
        *error = QStringLiteral("Unknown polar series kind"); return false;
    }
    point->kind = PolarPoint::Kind(kind);
    const bool hasAngle = row.contains(QStringLiteral("angle"));
    point->hasCategory = row.contains(QStringLiteral("category"));
    if (hasAngle == point->hasCategory) {
        *error = QStringLiteral("Each point requires exactly one of angle or category"); return false;
    }
    if (point->hasCategory) {
        point->category = row.value(QStringLiteral("category")).toString();
        if (point->category.isEmpty() || point->category.size() > 128) {
            *error = QStringLiteral("Category must be nonempty and bounded"); return false;
        }
    } else {
        bool ok = false;
        point->angle = row.value(QStringLiteral("angle")).toDouble(&ok);
        if (!ok || !std::isfinite(point->angle) || std::abs(point->angle) > 1e9) {
            *error = QStringLiteral("Angle must be finite and within ±1e9 input units"); return false;
        }
    }
    bool radiusOk = false;
    point->radius = row.value(QStringLiteral("radius")).toDouble(&radiusOk);
    if (!radiusOk || !std::isfinite(point->radius) || point->radius < 0
        || point->radius > 1e12) {
        *error = QStringLiteral("Radius must be finite and between 0 and 1e12"); return false;
    }
    return true;
}

bool PolarDataModel::validateSeries(const QVector<PolarPoint> &points, QString *error)
{
    QSet<QString> ids;
    QHash<QString, PolarPoint::Kind> kinds;
    QHash<QString, bool> categories;
    QHash<QString, QString> names;
    QSet<QString> categoryKeys;
    for (const PolarPoint &p : points) {
        const QString pointKey = p.seriesId + QChar(u'\0') + p.pointId;
        if (ids.contains(pointKey)) { *error = QStringLiteral("Duplicate series/point ID"); return false; }
        ids.insert(pointKey);
        if (kinds.contains(p.seriesId)
            && (kinds.value(p.seriesId) != p.kind
                || categories.value(p.seriesId) != p.hasCategory
                || names.value(p.seriesId) != p.seriesName)) {
            *error = QStringLiteral("One series must keep its kind, angular mode and name"); return false;
        }
        kinds.insert(p.seriesId, p.kind);
        categories.insert(p.seriesId, p.hasCategory);
        names.insert(p.seriesId, p.seriesName);
        if (p.hasCategory && p.kind != PolarPoint::Points) {
            const QString key = p.seriesId + QChar(u'\0') + p.category;
            if (categoryKeys.contains(key)) {
                *error = QStringLiteral("One plotted category may appear only once per series"); return false;
            }
            categoryKeys.insert(key);
        }
    }
    if (kinds.size() > MaxSeries) { *error = QStringLiteral("Polar plot exceeds 32 series"); return false; }
    return true;
}

bool PolarDataModel::replacePoints(const QVariantList &rows)
{
    if (rows.size() > MaxPoints) { setError(QStringLiteral("Polar plot exceeds 10,000 points")); return false; }
    QVector<PolarPoint> points; points.reserve(rows.size());
    QString error;
    for (const QVariant &value : rows) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            setError(QStringLiteral("Every point must be a record")); return false;
        }
        PolarPoint point;
        if (!parse(value.toMap(), &point, &error)) { setError(error); return false; }
        points.append(point);
    }
    if (!validateSeries(points, &error)) { setError(error); return false; }
    beginResetModel(); m_points = std::move(points); endResetModel();
    setError({}); emit rowsChanged(); return true;
}

bool PolarDataModel::appendPoint(const QVariantMap &row)
{
    if (m_points.size() >= MaxPoints) { setError(QStringLiteral("Polar plot exceeds 10,000 points")); return false; }
    PolarPoint point; QString error;
    if (!parse(row, &point, &error)) { setError(error); return false; }
    QVector<PolarPoint> proposed = m_points;
    proposed.append(point);
    if (!validateSeries(proposed, &error)) { setError(error); return false; }
    const int index = m_points.size();
    beginInsertRows({}, index, index); m_points.append(point); endInsertRows();
    setError({}); emit rowsChanged(); return true;
}

} // namespace QindaTK
