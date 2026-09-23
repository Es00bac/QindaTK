// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_dataset.h"

#include <QSet>
#include <cmath>

namespace QindaTK {

ChartDataset::ChartDataset(QObject *parent) : QAbstractListModel(parent), m_series(nullptr),
    m_color(), m_fillColor() {}

int ChartDataset::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_series.count(); }

void ChartDataset::setId(const QString &v)
{ if (m_series.id() != v) { m_series.setId(v); emit tableMetadataChanged(); emit changed(); } }
void ChartDataset::setName(const QString &v)
{ if (m_series.name() != v) { m_series.setName(v); emit tableMetadataChanged(); emit changed(); } }
void ChartDataset::setXUnit(const QString &v)
{ if (m_series.xUnit() != v) { m_series.setXUnit(v); emit tableMetadataChanged(); emit changed(); } }
void ChartDataset::setYUnit(const QString &v)
{ if (m_series.yUnit() != v) { m_series.setYUnit(v); emit tableMetadataChanged(); emit changed(); } }

QHash<int, QByteArray> ChartDataset::roleNames() const
{
    return {{PointIdRole, "pointId"}, {XRole, "x"}, {UtcMsRole, "utcMs"},
        {YRole, "y"}, {CategoryRole, "category"}, {SizeRole, "size"},
        {DxRole, "dx"}, {DyRole, "dy"}, {GapRole, "gap"}, {TotalRole, "total"}};
}

QVariantMap ChartDataset::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_series.count()) return out;
    const ChartPoint &p = m_series.points().at(row);
    out.insert(QStringLiteral("pointId"), p.id);
    out.insert(QStringLiteral("x"), p.x);
    out.insert(QStringLiteral("utcMs"), QString::number(p.utcMs));
    out.insert(QStringLiteral("y"), p.y);
    out.insert(QStringLiteral("category"), p.category);
    out.insert(QStringLiteral("size"), p.size);
    out.insert(QStringLiteral("dx"), p.dx);
    out.insert(QStringLiteral("dy"), p.dy);
    out.insert(QStringLiteral("gap"), p.gap);
    out.insert(QStringLiteral("total"), p.total);
    return out;
}

QVariant ChartDataset::data(const QModelIndex &index, int role) const
{
    const QVariantMap row = get(index.row());
    if (!index.isValid() || row.isEmpty()) return {};
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.key() == role) return row.value(QString::fromLatin1(it.value()));
    return {};
}

bool ChartDataset::parse(const QVariantMap &row, ChartPoint *point, QString *error) const
{
    if (!point || !row.value(QStringLiteral("pointId")).isValid()) {
        if (error) *error = QStringLiteral("Each chart point requires a stable pointId");
        return false;
    }
    point->id = row.value(QStringLiteral("pointId")).toString();
    if (point->id.isEmpty()) {
        if (error) *error = QStringLiteral("Point ID cannot be empty");
        return false;
    }
    point->gap = row.value(QStringLiteral("gap")).toBool();
    point->total = row.value(QStringLiteral("total")).toBool();
    point->category = row.value(QStringLiteral("category")).toString();
    point->y = row.value(QStringLiteral("y")).toDouble();
    point->size = row.value(QStringLiteral("size")).toDouble();
    point->dx = row.value(QStringLiteral("dx")).toDouble();
    point->dy = row.value(QStringLiteral("dy")).toDouble();
    const QVariant x = row.value(QStringLiteral("x"));
    const QVariant utc = row.value(QStringLiteral("utcMs"));
    // Date axes accept only a decimal string. This check prevents a QML Number
    // from having already rounded an epoch value beyond 2^53.
    if (utc.isValid()) {
        if (utc.metaType().id() != QMetaType::QString) {
            if (error) *error = QStringLiteral("utcMs must be an exact decimal string");
            return false;
        }
        bool ok = false;
        point->utcMs = utc.toString().toLongLong(&ok, 10);
        if (!ok || QString::number(point->utcMs) != utc.toString()) {
            if (error) *error = QStringLiteral("utcMs is not a signed 64-bit decimal integer");
            return false;
        }
    } else if (x.isValid()) {
        point->x = x.toDouble();
        if (!std::isfinite(point->x)) {
            if (error) *error = QStringLiteral("x must be finite");
            return false;
        }
    }
    if (!point->gap && !std::isfinite(point->y)) {
        if (error) *error = QStringLiteral("y must be finite");
        return false;
    }
    return true;
}

bool ChartDataset::appendPoint(const QVariantMap &row)
{
    ChartPoint point;
    QString error;
    if (!parse(row, &point, &error)) { setLastError(error); return false; }
    if (!point.gap && m_series.kind() == ChartSeries::Bubble
        && (!std::isfinite(point.size) || point.size < 0)) {
        setLastError(QStringLiteral("Bubble size must be finite and nonnegative")); return false;
    }
    if (!point.gap && m_series.kind() == ChartSeries::Vector
        && (!std::isfinite(point.dx) || !std::isfinite(point.dy))) {
        setLastError(QStringLiteral("Vector displacement must be finite")); return false;
    }
    if (point.total && m_series.kind() != ChartSeries::Waterfall) {
        setLastError(QStringLiteral("Total requires a waterfall series")); return false;
    }
    const int oldCount = m_series.count();
    if (m_series.point(point.id)) { setLastError(QStringLiteral("Duplicate point ID: %1").arg(point.id)); return false; }
    if (!point.gap && (!std::isfinite(point.x) || !std::isfinite(point.y))) {
        setLastError(QStringLiteral("Point coordinates must be finite")); return false;
    }
    if (oldCount == capacity()) {
        const QString firstId = m_series.points().first().id;
        beginRemoveRows({}, 0, 0);
        m_series.removePoint(firstId);
        endRemoveRows();
    }
    const int index = m_series.count();
    beginInsertRows({}, index, index);
    const bool appended = m_series.appendPoint(point, &error);
    endInsertRows();
    if (!appended) { setLastError(error); return false; }
    setLastError({});
    emit rowsChanged(); emit changed();
    return true;
}

bool ChartDataset::replacePoints(const QVariantList &rows)
{
    if (rows.size() > capacity()) { setLastError(QStringLiteral("Point count exceeds capacity")); return false; }
    QVector<ChartPoint> points;
    QSet<QString> ids;
    points.reserve(rows.size());
    for (const QVariant &value : rows) {
        ChartPoint point;
        QString error;
        if (!parse(value.toMap(), &point, &error)) { setLastError(error); return false; }
        if (ids.contains(point.id)) { setLastError(QStringLiteral("Duplicate point ID: %1").arg(point.id)); return false; }
        if (!point.gap && m_series.kind() == ChartSeries::Bubble && (!std::isfinite(point.size) || point.size < 0)) {
            setLastError(QStringLiteral("Bubble size must be finite and nonnegative")); return false;
        }
        if (!point.gap && m_series.kind() == ChartSeries::Vector && (!std::isfinite(point.dx) || !std::isfinite(point.dy))) {
            setLastError(QStringLiteral("Vector displacement must be finite")); return false;
        }
        if (point.total && m_series.kind() != ChartSeries::Waterfall) {
            setLastError(QStringLiteral("Total requires a waterfall series")); return false;
        }
        ids.insert(point.id); points.append(point);
    }
    QString error;
    beginResetModel();
    const bool replaced = m_series.replacePoints(points, &error);
    endResetModel();
    if (!replaced) { setLastError(error); return false; }
    setLastError({});
    emit rowsChanged(); emit changed();
    return true;
}

bool ChartDataset::removePoint(const QString &pointId)
{
    for (int i = 0; i < m_series.count(); ++i) {
        if (m_series.points().at(i).id == pointId) {
            beginRemoveRows({}, i, i);
            m_series.removePoint(pointId);
            endRemoveRows(); setLastError({}); emit rowsChanged(); emit changed(); return true;
        }
    }
    setLastError(QStringLiteral("Point ID not found: %1").arg(pointId)); return false;
}

void ChartDataset::setLastError(const QString &error)
{ if (m_lastError != error) { m_lastError = error; emit errorChanged(); } }

void ChartDataset::setVisible(bool v)
{ if (m_series.isVisible() != v) { m_series.setVisible(v); emit changed(); } }

void ChartDataset::setCapacity(int v)
{
    v = qBound(2, v, 100000);
    if (v == capacity()) return;
    beginResetModel(); m_series.setCapacity(v); endResetModel();
    emit capacityChanged(); emit rowsChanged(); emit changed();
}

void ChartDataset::setColor(const QColor &v)
{ if (v.isValid() && m_color != v) { m_color = v; emit changed(); } }

void ChartDataset::setFillColor(const QColor &v)
{ if (v.isValid() && m_fillColor != v) { m_fillColor = v; emit changed(); } }

} // namespace QindaTK
