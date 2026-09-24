// SPDX-License-Identifier: LGPL-3.0-or-later
#include "transfer_function_model.h"
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
// AGENT-NOTE: opacity is its own channel. A stop colour's alpha would
// otherwise tint editor swatches while colorAt() ignores it, so stored
// colours are opaque RGB.
QColor opaque(const QColor &color)
{
    QColor rgb = color.toRgb();
    rgb.setAlpha(255);
    return rgb;
}
// Saved settings carry "#rrggbb" strings so they survive JSON; QML passes
// color values. Both are accepted.
QColor colorFrom(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QString) return QColor::fromString(value.toString());
    return value.value<QColor>();
}
} // namespace

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
        const QColor color = colorFrom(map.value(QStringLiteral("color")));
        if (id.isEmpty() || ids.contains(id) || !valueOk || !opacityOk
            || !std::isfinite(value) || !std::isfinite(opacity) || !color.isValid()
            || opacity < 0 || opacity > 1 || value < m_min || value > m_max
            || (!parsed.isEmpty() && value <= parsed.last().value)) {
            fail(QStringLiteral("Invalid transfer point or order")); return false;
        }
        ids.insert(id); parsed.append({id, value, opaque(color), opacity});
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
    if (at != m_points.end() && at->value == value) {
        fail(QStringLiteral("A transfer point already has this value")); return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_points.insert(at, {id, value, opaque(color), opacity}); publish(); return id;
}
bool TransferFunctionModel::movePoint(const QString &id, double value)
{
    const int index = indexOf(id);
    if (index <= 0 || index >= m_points.size() - 1) {
        fail(QStringLiteral("Only an inner transfer point can move")); return false;
    }
    if (!std::isfinite(value) || value <= m_points[index - 1].value
        || value >= m_points[index + 1].value) {
        fail(QStringLiteral("A transfer point must stay between its neighbours")); return false;
    }
    m_points[index].value = value; publish(); return true;
}
bool TransferFunctionModel::setPointColor(const QString &id, const QColor &color)
{
    const int index = indexOf(id);
    if (index < 0 || !color.isValid()) {
        fail(QStringLiteral("Unknown transfer point or invalid color")); return false;
    }
    m_points[index].color = opaque(color); publish(); return true;
}
bool TransferFunctionModel::setPointOpacity(const QString &id, double opacity)
{
    const int index = indexOf(id);
    if (index < 0 || !std::isfinite(opacity) || opacity < 0 || opacity > 1) {
        fail(QStringLiteral("Unknown transfer point or opacity outside 0–1")); return false;
    }
    m_points[index].opacity = opacity; publish(); return true;
}
bool TransferFunctionModel::removePoint(const QString &id)
{
    const int index = indexOf(id);
    if (index <= 0 || index >= m_points.size() - 1) {
        fail(QStringLiteral("Only an inner transfer point can be removed")); return false;
    }
    m_points.removeAt(index); publish(); return true;
}
TransferFunctionModel::Segment TransferFunctionModel::segmentAt(double value) const
{
    // Values outside the domain clamp to an endpoint; inside it, the first
    // stop at or above the value closes the segment (stops strictly increase).
    if (value <= m_min) return {&m_points.first(), &m_points.first(), 0};
    if (value >= m_max) return {&m_points.last(), &m_points.last(), 0};
    const auto right = std::lower_bound(m_points.cbegin() + 1, m_points.cend(), value,
                                        [](const Point &point, double v) { return point.value < v; });
    const Point *left = &*(right - 1);
    return {left, &*right, (value - left->value) / (right->value - left->value)};
}
QColor TransferFunctionModel::colorAt(double value) const
{
    if (!std::isfinite(value)) return Qt::transparent;
    const Segment s = segmentAt(value);
    const auto mix = [&s](int a, int b) { return int(std::lround(a + s.t * (b - a))); };
    return QColor(mix(s.left->color.red(), s.right->color.red()),
                  mix(s.left->color.green(), s.right->color.green()),
                  mix(s.left->color.blue(), s.right->color.blue()),
                  int(std::lround(255 * (s.left->opacity + s.t * (s.right->opacity - s.left->opacity)))));
}
double TransferFunctionModel::opacityAt(double value) const
{
    if (!std::isfinite(value)) return 0;
    const Segment s = segmentAt(value);
    return s.left->opacity + s.t * (s.right->opacity - s.left->opacity);
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
    QVariantList saved;
    for (const auto &point : m_points)
        saved.append(QVariantMap{{QStringLiteral("id"), point.id},
                                 {QStringLiteral("value"), point.value},
                                 {QStringLiteral("color"), point.color.name(QColor::HexRgb)},
                                 {QStringLiteral("opacity"), point.opacity}});
    return {{QStringLiteral("domainMin"), m_min}, {QStringLiteral("domainMax"), m_max},
            {QStringLiteral("unit"), m_unit}, {QStringLiteral("points"), saved}};
}
bool TransferFunctionModel::restoreSettings(const QVariantMap &settings)
{
    const double oldMin = m_min, oldMax = m_max;
    const QString oldUnit = m_unit;
    bool minOk = false, maxOk = false;
    const double min = settings.value(QStringLiteral("domainMin")).toDouble(&minOk);
    const double max = settings.value(QStringLiteral("domainMax")).toDouble(&maxOk);
    if (!minOk || !maxOk || !std::isfinite(min) || !std::isfinite(max) || min >= max) {
        fail(QStringLiteral("Transfer domain must be finite and increasing")); return false;
    }
    // replacePoints validates against the domain being restored and leaves
    // m_points untouched on failure; only the domain needs rolling back.
    m_min = min; m_max = max; m_unit = settings.value(QStringLiteral("unit")).toString();
    if (replacePoints(settings.value(QStringLiteral("points")).toList())) return true;
    m_min = oldMin; m_max = oldMax; m_unit = oldUnit;
    return false;
}
} // namespace QindaTK
