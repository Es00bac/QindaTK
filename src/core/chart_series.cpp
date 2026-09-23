// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_series.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK {
ChartSeries::ChartSeries(QObject *parent) : QObject(parent) {}
void ChartSeries::setId(const QString &v) { if (m_id != v) { m_id = v; emit changed(); } }
void ChartSeries::setName(const QString &v) { if (m_name != v) { m_name = v; emit changed(); } }
void ChartSeries::setKind(Kind v) { if (m_kind != v) { m_kind = v; emit changed(); } }
void ChartSeries::setStackKey(const QString &v) { if (m_stackKey != v) { m_stackKey = v; emit changed(); } }
void ChartSeries::setXUnit(const QString &v) { if (m_xUnit != v) { m_xUnit = v; emit changed(); } }
void ChartSeries::setYUnit(const QString &v) { if (m_yUnit != v) { m_yUnit = v; emit changed(); } }
void ChartSeries::setSizeUnit(const QString &v) { if (m_sizeUnit != v) { m_sizeUnit = v; emit changed(); } }
void ChartSeries::setBaseline(double v) { if (m_baseline != v) { m_baseline = v; emit changed(); } }
void ChartSeries::setVisible(bool v) { if (m_visible != v) { m_visible = v; emit changed(); } }
void ChartSeries::setCapacity(int v)
{
    v = std::max(1, v);
    if (m_capacity == v) return;
    m_capacity = v;
    if (m_points.size() > m_capacity) {
        m_points.erase(m_points.begin(), m_points.end() - m_capacity);
        emit pointsChanged();
    }
    emit changed();
}
bool ChartSeries::validate(const ChartPoint &p, QString *error) const
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (p.id.isEmpty()) return fail(QStringLiteral("Point ID is empty"));
    if (p.gap) return true;
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return fail(QStringLiteral("Point coordinates must be finite"));
    if (m_kind == Bubble && (!std::isfinite(p.size) || p.size < 0))
        return fail(QStringLiteral("Bubble size must be finite and nonnegative"));
    if (m_kind == Vector && (!std::isfinite(p.dx) || !std::isfinite(p.dy)))
        return fail(QStringLiteral("Vector displacement must be finite"));
    if (p.total && m_kind != Waterfall) return fail(QStringLiteral("Total requires a waterfall series"));
    return true;
}
bool ChartSeries::replacePoints(const QVector<ChartPoint> &points, QString *error)
{
    if (points.size() > m_capacity) {
        if (error) *error = QStringLiteral("Point count exceeds capacity");
        return false;
    }
    QSet<QString> ids;
    for (const ChartPoint &p : points) {
        if (!validate(p, error)) return false;
        if (ids.contains(p.id)) {
            if (error) *error = QStringLiteral("Duplicate point ID: %1").arg(p.id);
            return false;
        }
        ids.insert(p.id);
    }
    m_points = points;
    emit pointsChanged();
    return true;
}
bool ChartSeries::appendPoint(const ChartPoint &p, QString *error)
{
    if (!validate(p, error)) return false;
    if (point(p.id)) {
        if (error) *error = QStringLiteral("Duplicate point ID: %1").arg(p.id);
        return false;
    }
    if (m_points.size() == m_capacity) m_points.removeFirst();
    m_points.append(p);
    emit pointsChanged();
    return true;
}
bool ChartSeries::removePoint(const QString &pointId)
{
    for (qsizetype i = 0; i < m_points.size(); ++i) {
        if (m_points.at(i).id == pointId) {
            m_points.removeAt(i);
            emit pointsChanged();
            return true;
        }
    }
    return false;
}
const ChartPoint *ChartSeries::point(const QString &pointId) const
{
    for (const ChartPoint &p : m_points) if (p.id == pointId) return &p;
    return nullptr;
}
void ChartSeries::clear() { if (!m_points.isEmpty()) { m_points.clear(); emit pointsChanged(); } }
} // namespace QindaTK
