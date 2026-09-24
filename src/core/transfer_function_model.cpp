// SPDX-License-Identifier: LGPL-3.0-or-later
#include "transfer_function_model.h"
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace QindaTK {
TransferFunctionModel::TransferFunctionModel(QObject *parent) : QObject(parent)
{
    m_points = {{QStringLiteral("low"), 0, QColor(Qt::black), 0},
                {QStringLiteral("high"), 255, QColor(Qt::white), 1}};
}
void TransferFunctionModel::fail(const QString &message)
{
    if (m_error == message) return;
    m_error = message; emit errorChanged();
}
void TransferFunctionModel::publish() { ++m_revision; fail({}); emit changed(); }
int TransferFunctionModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_points.size(); ++i) if (m_points[i].id == id) return i;
    return -1;
}
QVariantList TransferFunctionModel::points() const
{
    QVariantList result;
    for (const auto &point : m_points)
        result.append(QVariantMap{{QStringLiteral("id"), point.id},
                                  {QStringLiteral("value"), point.value},
                                  {QStringLiteral("color"), point.color},
                                  {QStringLiteral("opacity"), point.opacity}});
    return result;
}
QVariantMap TransferFunctionModel::point(const QString &id) const
{
    const int index = indexOf(id);
    if (index < 0) return {};
    const auto &point = m_points[index];
    return {{QStringLiteral("id"), point.id}, {QStringLiteral("value"), point.value},
            {QStringLiteral("color"), point.color}, {QStringLiteral("opacity"), point.opacity}};
}
bool TransferFunctionModel::configure(double minimum, double maximum, const QString &unit)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum) {
        fail(QStringLiteral("Transfer domain must be finite and increasing")); return false;
    }
    m_min = minimum; m_max = maximum; m_unit = unit;
    m_points = {{QStringLiteral("low"), minimum, QColor(Qt::black), 0},
                {QStringLiteral("high"), maximum, QColor(Qt::white), 1}};
    publish(); return true;
}
bool TransferFunctionModel::replacePoints(const QVariantList &points)
{
    if (points.size() < 2 || points.size() > 256) {
        fail(QStringLiteral("Transfer function needs 2–256 points")); return false;
    }
    QVector<Point> parsed;
    QSet<QString> ids;
    for (const auto &raw : points) {
        const QVariantMap map = raw.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        bool valueOk = false, opacityOk = false;
        const double value = map.value(QStringLiteral("value")).toDouble(&valueOk);
        const double opacity = map.value(QStringLiteral("opacity")).toDouble(&opacityOk);
        const QColor color = map.value(QStringLiteral("color")).value<QColor>();
        if (id.isEmpty() || ids.contains(id) || !valueOk || !opacityOk
            || !std::isfinite(value) || !std::isfinite(opacity) || !color.isValid()
            || opacity < 0 || opacity > 1 || value < m_min || value > m_max
            || (!parsed.isEmpty() && value <= parsed.last().value)) {
            fail(QStringLiteral("Invalid transfer point or order")); return false;
        }
        ids.insert(id); parsed.append({id, value, color, opacity});
    }
    if (parsed.first().value != m_min || parsed.last().value != m_max) {
        fail(QStringLiteral("Transfer endpoints must equal domain bounds")); return false;
    }
    m_points = std::move(parsed); publish(); return true;
}
QString TransferFunctionModel::addPoint(double value, const QColor &color, double opacity)
{
    if (!std::isfinite(value) || value <= m_min || value >= m_max || !color.isValid()
        || !std::isfinite(opacity) || opacity < 0 || opacity > 1 || m_points.size() >= 256) {
        fail(QStringLiteral("Invalid transfer point")); return {};
    }
    auto at = std::lower_bound(m_points.begin(), m_points.end(), value,
                               [](const Point &point, double v) { return point.value < v; });
    if (at != m_points.end() && at->value == value) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_points.insert(at, {id, value, color, opacity}); publish(); return id;
}
bool TransferFunctionModel::movePoint(const QString &id, double value)
{
    const int index = indexOf(id);
    if (index <= 0 || index >= m_points.size() - 1 || !std::isfinite(value)
        || value <= m_points[index - 1].value || value >= m_points[index + 1].value) return false;
    m_points[index].value = value; publish(); return true;
}
bool TransferFunctionModel::setPointColor(const QString &id, const QColor &color)
{
    const int index = indexOf(id);
    if (index < 0 || !color.isValid()) return false;
    m_points[index].color = color; publish(); return true;
}
bool TransferFunctionModel::setPointOpacity(const QString &id, double opacity)
{
    const int index = indexOf(id);
    if (index < 0 || !std::isfinite(opacity) || opacity < 0 || opacity > 1) return false;
    m_points[index].opacity = opacity; publish(); return true;
}
bool TransferFunctionModel::removePoint(const QString &id)
{
    const int index = indexOf(id);
    if (index <= 0 || index >= m_points.size() - 1) return false;
    m_points.removeAt(index); publish(); return true;
}
QColor TransferFunctionModel::colorAt(double value) const
{
    if (!std::isfinite(value)) return Qt::transparent;
    const Point *left = &m_points.first(), *right = &m_points.last();
    if (value <= m_min) right = left;
    else if (value >= m_max) left = right;
    else for (int i = 1; i < m_points.size(); ++i)
        if (value <= m_points[i].value) { left = &m_points[i - 1]; right = &m_points[i]; break; }
    const double t = left == right ? 0 : (value - left->value) / (right->value - left->value);
    const auto mix = [t](int a, int b) { return int(std::lround(a + t * (b - a))); };
    return QColor(mix(left->color.red(), right->color.red()),
                  mix(left->color.green(), right->color.green()),
                  mix(left->color.blue(), right->color.blue()),
                  int(std::lround(255 * (left->opacity + t * (right->opacity - left->opacity)))));
}
QVariantList TransferFunctionModel::previewColors(int count) const
{
    QVariantList colors;
    count = std::clamp(count, 2, 512);
    for (int i = 0; i < count; ++i)
        colors.append(colorAt(m_min + (m_max - m_min) * i / (count - 1)).name(QColor::HexRgb));
    return colors;
}
QVariantMap TransferFunctionModel::saveSettings() const
{
    return {{QStringLiteral("domainMin"), m_min}, {QStringLiteral("domainMax"), m_max},
            {QStringLiteral("unit"), m_unit}, {QStringLiteral("points"), points()}};
}
bool TransferFunctionModel::restoreSettings(const QVariantMap &settings)
{
    const double oldMin = m_min, oldMax = m_max;
    const QString oldUnit = m_unit;
    const auto oldPoints = m_points;
    bool minOk = false, maxOk = false;
    const double min = settings.value(QStringLiteral("domainMin")).toDouble(&minOk);
    const double max = settings.value(QStringLiteral("domainMax")).toDouble(&maxOk);
    if (!minOk || !maxOk || !std::isfinite(min) || !std::isfinite(max) || min >= max) return false;
    m_min = min; m_max = max; m_unit = settings.value(QStringLiteral("unit")).toString();
    if (replacePoints(settings.value(QStringLiteral("points")).toList())) return true;
    m_min = oldMin; m_max = oldMax; m_unit = oldUnit; m_points = oldPoints;
    return false;
}
} // namespace QindaTK
