// SPDX-License-Identifier: LGPL-3.0-or-later
#include "transfer_function_model.h"
#include "undo_service.h"
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
    m_documentId = DocumentId::create().toString();
    m_localHistory = new UndoService(this);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, 128);
}
TransferFunctionModel::~TransferFunctionModel() = default;
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
    State loaded;
    loaded.min = minimum; loaded.max = maximum; loaded.unit = unit;
    loaded.points = {{QStringLiteral("low"), minimum, QColor(Qt::black), 0},
                     {QStringLiteral("high"), maximum, QColor(Qt::white), 1}};
    loaded.missing = {true, QColor(Qt::black), 0};
    setState(loaded);
    resetHistory(); publish(); emit committed(); return true;
}
bool TransferFunctionModel::replacePoints(const QVariantList &points)
{
    QVector<Point> parsed;
    if (!parsePoints(points, m_min, m_max, &parsed)) return false;
    return apply(std::move(parsed), tr("Replace transfer points"));
}
bool TransferFunctionModel::parsePoints(const QVariantList &points, double min, double max,
                                        QVector<Point> *out)
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
            || opacity < 0 || opacity > 1 || value < min || value > max
            || (!parsed.isEmpty() && value <= parsed.last().value)) {
            fail(QStringLiteral("Invalid transfer point or order")); return false;
        }
        ids.insert(id); parsed.append({id, value, opaque(color), opacity});
    }
    if (parsed.first().value != min || parsed.last().value != max) {
        fail(QStringLiteral("Transfer endpoints must equal domain bounds")); return false;
    }
    *out = std::move(parsed); return true;
}
QString TransferFunctionModel::addPoint(double value, const QColor &color, double opacity)
{
    if (!std::isfinite(value) || value <= m_min || value >= m_max || !color.isValid()
        || !std::isfinite(opacity) || opacity < 0 || opacity > 1 || m_points.size() >= 256) {
        fail(QStringLiteral("Invalid transfer point")); return {};
    }
    const auto at = std::lower_bound(m_points.cbegin(), m_points.cend(), value,
                                     [](const Point &point, double v) { return point.value < v; });
    if (at != m_points.cend() && at->value == value) {
        fail(QStringLiteral("A transfer point already has this value")); return {};
    }
    const qsizetype position = at - m_points.cbegin();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVector<Point> after = m_points;
    after.insert(position, {id, value, opaque(color), opacity});
    return apply(std::move(after), tr("Add transfer point")) ? id : QString();
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
    QVector<Point> after = m_points;
    after[index].value = value;
    return apply(std::move(after), tr("Move transfer point"));
}
bool TransferFunctionModel::setPointColor(const QString &id, const QColor &color)
{
    const int index = indexOf(id);
    if (index < 0 || !color.isValid()) {
        fail(QStringLiteral("Unknown transfer point or invalid color")); return false;
    }
    QVector<Point> after = m_points;
    after[index].color = opaque(color);
    return apply(std::move(after), tr("Change transfer color"));
}
bool TransferFunctionModel::setPointOpacity(const QString &id, double opacity)
{
    const int index = indexOf(id);
    if (index < 0 || !std::isfinite(opacity) || opacity < 0 || opacity > 1) {
        fail(QStringLiteral("Unknown transfer point or opacity outside 0–1")); return false;
    }
    QVector<Point> after = m_points;
    after[index].opacity = opacity;
    return apply(std::move(after), tr("Change transfer opacity"));
}
bool TransferFunctionModel::removePoint(const QString &id)
{
    const int index = indexOf(id);
    if (index <= 0 || index >= m_points.size() - 1) {
        fail(QStringLiteral("Only an inner transfer point can be removed")); return false;
    }
    QVector<Point> after = m_points;
    after.removeAt(index);
    return apply(std::move(after), tr("Remove transfer point"));
}
const TransferFunctionModel::Special *TransferFunctionModel::specialFor(double value) const
{
    if (!std::isfinite(value)) return &m_missing;
    if (value < m_min && m_below.set) return &m_below;
    if (value > m_max && m_above.set) return &m_above;
    return nullptr;
}
QString TransferFunctionModel::classify(double value) const
{
    if (!std::isfinite(value)) return QStringLiteral("missing");
    if (value < m_min) return QStringLiteral("below");
    if (value > m_max) return QStringLiteral("above");
    return QStringLiteral("inside");
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
    if (const Special *special = specialFor(value)) {
        QColor color = special->color;
        color.setAlpha(int(std::lround(255 * special->opacity)));
        return color;
    }
    const Segment s = segmentAt(value);
    const auto mix = [&s](int a, int b) { return int(std::lround(a + s.t * (b - a))); };
    return QColor(mix(s.left->color.red(), s.right->color.red()),
                  mix(s.left->color.green(), s.right->color.green()),
                  mix(s.left->color.blue(), s.right->color.blue()),
                  int(std::lround(255 * (s.left->opacity + s.t * (s.right->opacity - s.left->opacity)))));
}
double TransferFunctionModel::opacityAt(double value) const
{
    if (const Special *special = specialFor(value)) return special->opacity;
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
} // namespace QindaTK
