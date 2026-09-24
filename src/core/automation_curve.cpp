// SPDX-License-Identifier: LGPL-3.0-or-later
#include "automation_types.h"

#include "timeline_time.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AutomationModel", text);
}

OperationError invalid(const QString &message, const QString &field = {})
{
    return {ErrorCode::Validation, message, field, QStringLiteral("automation")};
}

bool unit(double value)
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

} // namespace

bool operator==(const AutomationParameter &a, const AutomationParameter &b)
{
    return a.id == b.id && a.name == b.name && a.range.dimension == b.range.dimension
        && a.range.unitId == b.range.unitId && a.range.minimum == b.range.minimum
        && a.range.maximum == b.range.maximum && a.defaultValue == b.defaultValue && a.step == b.step
        && a.interpolation == b.interpolation;
}

namespace AutomationCurve {

double normalized(const AutomationParameter &parameter, double value)
{
    return (value - parameter.range.minimum) / (parameter.range.maximum - parameter.range.minimum);
}

double quantized(const AutomationParameter &parameter, double value)
{
    const double low = parameter.range.minimum;
    const double high = parameter.range.maximum;
    if (!std::isfinite(value)) return parameter.defaultValue;
    double result = std::clamp(value, low, high);
    if (parameter.step > 0) result = std::clamp(low + std::round((result - low) / parameter.step) * parameter.step, low, high);
    return result;
}

CurveDefinition segmentCurve(const AutomationParameter &parameter, const AutomationPoint &left,
                             const AutomationPoint &right)
{
    CurveDefinition curve;
    curve.id = parameter.id;
    curve.yDomain = parameter.range;
    CurveKnot a;
    a.id = QStringLiteral("left");
    a.x = 0.0;
    a.y = normalized(parameter, left.value);
    a.interpolation = left.interpolation;
    if (left.outHandle) a.outHandle = QPointF(left.outHandle->x, left.outHandle->y);
    CurveKnot b;
    b.id = QStringLiteral("right");
    b.x = 1.0;
    b.y = normalized(parameter, right.value);
    if (right.inHandle) b.inHandle = QPointF(right.inHandle->x, right.inHandle->y);
    curve.knots = {a, b};
    return curve;
}

double valueAt(const AutomationParameter &parameter, const QVector<AutomationPoint> &points, qint64 ticks)
{
    if (points.isEmpty()) return parameter.defaultValue;
    const auto right = std::upper_bound(points.cbegin(), points.cend(), ticks,
                                        [](qint64 t, const AutomationPoint &point) { return t < point.ticks; });
    if (right == points.cbegin()) return points.first().value;
    if (right == points.cend()) return points.last().value;
    const AutomationPoint &left = *(right - 1);
    if (left.ticks == ticks || left.interpolation == CurveInterpolation::Step) return left.value;
    const auto offset = TimelineTime::subtract(ticks, left.ticks);
    const auto span = TimelineTime::subtract(right->ticks, left.ticks);
    if (!offset || !span || *span <= 0) return left.value;
    const double fraction = double(*offset) / double(*span);
    if (left.interpolation == CurveInterpolation::Linear) return std::lerp(left.value, right->value, fraction);
    // AGENT-NOTE: cubic segments reuse the WP-043 CurveEvaluator seam on a
    // segment-local curve, so normalized x never spans the whole timeline and
    // exact tick precision survives long sessions (docs/curve.md).
    const CurveEvaluation evaluation = CurveEvaluator::evaluateNormalized(segmentCurve(parameter, left, *right), fraction);
    return evaluation.sample ? evaluation.sample->value.value : std::lerp(left.value, right->value, fraction);
}

OperationError validateParameter(const AutomationParameter &parameter)
{
    if (parameter.id.trimmed().isEmpty()) return invalid(tr("A parameter needs an ID"), QStringLiteral("id"));
    const auto &range = parameter.range;
    if (range.unitId.trimmed().isEmpty() || !std::isfinite(range.minimum) || !std::isfinite(range.maximum)
        || range.maximum <= range.minimum || !std::isfinite(range.maximum - range.minimum))
        return invalid(tr("A parameter needs a unit and a finite increasing range"), QStringLiteral("range"));
    if (!std::isfinite(parameter.defaultValue) || parameter.defaultValue < range.minimum
        || parameter.defaultValue > range.maximum)
        return invalid(tr("The default value must lie in the range"), QStringLiteral("default"));
    if (!std::isfinite(parameter.step) || parameter.step < 0)
        return invalid(tr("The step must be zero or positive"), QStringLiteral("step"));
    return {};
}

OperationError validate(const AutomationParameter &parameter, const QVector<AutomationPoint> &points)
{
    if (auto error = validateParameter(parameter)) return error;
    if (points.size() > kMaxPoints) return invalid(tr("A lane holds at most 65536 points"));
    QSet<QUuid> ids;
    for (qsizetype i = 0; i < points.size(); ++i) {
        const auto &point = points.at(i);
        if (point.id.isNull() || ids.contains(point.id))
            return invalid(tr("Automation point IDs must be unique UUIDs"), QStringLiteral("id"));
        ids.insert(point.id);
        if (i > 0 && point.ticks <= points.at(i - 1).ticks)
            return invalid(tr("Two automation points cannot share a time"), QStringLiteral("ticks"));
        if (!std::isfinite(point.value) || point.value < parameter.range.minimum
            || point.value > parameter.range.maximum)
            return invalid(tr("An automation value lies outside the parameter range"), QStringLiteral("value"));
        for (const auto &handle : {point.inHandle, point.outHandle})
            if (handle && (!unit(handle->x) || !unit(handle->y)))
                return invalid(tr("Curve handles must be normalized"), QStringLiteral("handle"));
        if (i > 0 && points.at(i - 1).interpolation == CurveInterpolation::CubicBezier) {
            if (auto error = CurveEvaluator::validate(segmentCurve(parameter, points.at(i - 1), point)))
                return invalid(error.message, QStringLiteral("handle"));
        }
    }
    return {};
}

void sort(QVector<AutomationPoint> &points)
{
    std::sort(points.begin(), points.end(), [](const AutomationPoint &a, const AutomationPoint &b) {
        return a.ticks != b.ticks ? a.ticks < b.ticks : a.id < b.id;
    });
}

} // namespace AutomationCurve

namespace AutomationCodec {

QString modeName(AutomationMode mode)
{
    switch (mode) {
    case AutomationMode::Off: return QStringLiteral("off");
    case AutomationMode::Read: return QStringLiteral("read");
    case AutomationMode::Touch: return QStringLiteral("touch");
    case AutomationMode::Latch: return QStringLiteral("latch");
    case AutomationMode::Write: return QStringLiteral("write");
    }
    return {};
}

std::optional<AutomationMode> modeFromName(const QString &name)
{
    for (auto mode : {AutomationMode::Off, AutomationMode::Read, AutomationMode::Touch, AutomationMode::Latch,
                      AutomationMode::Write})
        if (modeName(mode) == name) return mode;
    return std::nullopt;
}

QString interpolationName(CurveInterpolation interpolation)
{
    switch (interpolation) {
    case CurveInterpolation::Step: return QStringLiteral("step");
    case CurveInterpolation::Linear: return QStringLiteral("linear");
    case CurveInterpolation::CubicBezier: return QStringLiteral("cubic");
    }
    return {};
}

std::optional<CurveInterpolation> interpolationFromName(const QString &name)
{
    for (auto value : {CurveInterpolation::Step, CurveInterpolation::Linear, CurveInterpolation::CubicBezier})
        if (interpolationName(value) == name) return value;
    return std::nullopt;
}

QString dimensionName(UnitDimension dimension)
{
    switch (dimension) {
    case UnitDimension::Scalar: return QStringLiteral("scalar");
    case UnitDimension::Length: return QStringLiteral("length");
    case UnitDimension::Angle: return QStringLiteral("angle");
    case UnitDimension::Duration: return QStringLiteral("duration");
    case UnitDimension::Frequency: return QStringLiteral("frequency");
    case UnitDimension::Gain: return QStringLiteral("gain");
    }
    return {};
}

std::optional<UnitDimension> dimensionFromName(const QString &name)
{
    for (auto value : {UnitDimension::Scalar, UnitDimension::Length, UnitDimension::Angle, UnitDimension::Duration,
                       UnitDimension::Frequency, UnitDimension::Gain})
        if (dimensionName(value) == name) return value;
    return std::nullopt;
}

QVariantMap toMap(const AutomationParameter &parameter)
{
    return {{QStringLiteral("id"), parameter.id},
            {QStringLiteral("name"), parameter.name},
            {QStringLiteral("dimension"), dimensionName(parameter.range.dimension)},
            {QStringLiteral("unitId"), parameter.range.unitId},
            {QStringLiteral("minimum"), parameter.range.minimum},
            {QStringLiteral("maximum"), parameter.range.maximum},
            {QStringLiteral("default"), parameter.defaultValue},
            {QStringLiteral("step"), parameter.step},
            {QStringLiteral("interpolation"), interpolationName(parameter.interpolation)}};
}

std::optional<AutomationParameter> parameterFromMap(const QVariantMap &map, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return std::optional<AutomationParameter>();
    };
    AutomationParameter parameter;
    parameter.id = map.value(QStringLiteral("id")).toString();
    parameter.name = map.value(QStringLiteral("name"), parameter.id).toString();
    const auto dimension = dimensionFromName(map.value(QStringLiteral("dimension"), QStringLiteral("scalar")).toString());
    const auto interpolation = interpolationFromName(map.value(QStringLiteral("interpolation"),
                                                               QStringLiteral("linear")).toString());
    if (!dimension) return fail(tr("Unknown unit dimension"));
    if (!interpolation) return fail(tr("interpolation must be step, linear or cubic"));
    parameter.range.dimension = *dimension;
    parameter.range.unitId = map.value(QStringLiteral("unitId"), QStringLiteral("normalized")).toString();
    bool okMin = false, okMax = false, okDefault = true, okStep = true;
    parameter.range.minimum = map.value(QStringLiteral("minimum")).toDouble(&okMin);
    parameter.range.maximum = map.value(QStringLiteral("maximum")).toDouble(&okMax);
    parameter.defaultValue = map.contains(QStringLiteral("default"))
        ? map.value(QStringLiteral("default")).toDouble(&okDefault) : parameter.range.minimum;
    if (map.contains(QStringLiteral("step"))) parameter.step = map.value(QStringLiteral("step")).toDouble(&okStep);
    parameter.interpolation = *interpolation;
    if (!okMin || !okMax || !okDefault || !okStep) return fail(tr("Parameter range values must be numbers"));
    if (auto failure = AutomationCurve::validateParameter(parameter)) return fail(failure.message);
    return parameter;
}

QVariantMap toMap(const AutomationPoint &point)
{
    QVariantMap map{{QStringLiteral("id"), point.id.toString(QUuid::WithoutBraces)},
                    {QStringLiteral("ticks"), QString::number(point.ticks)},
                    {QStringLiteral("value"), point.value},
                    {QStringLiteral("interpolation"), interpolationName(point.interpolation)}};
    if (point.inHandle)
        map.insert(QStringLiteral("inHandle"), QVariantMap{{QStringLiteral("x"), point.inHandle->x},
                                                           {QStringLiteral("y"), point.inHandle->y}});
    if (point.outHandle)
        map.insert(QStringLiteral("outHandle"), QVariantMap{{QStringLiteral("x"), point.outHandle->x},
                                                            {QStringLiteral("y"), point.outHandle->y}});
    return map;
}

std::optional<AutomationPoint> pointFromMap(const QVariantMap &map, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return std::optional<AutomationPoint>();
    };
    AutomationPoint point;
    point.id = QUuid(map.value(QStringLiteral("id")).toString());
    const QVariant ticks = map.value(QStringLiteral("ticks"));
    const auto parsed = ticks.metaType().id() == QMetaType::QString ? TimelineTime::parseDecimal(ticks.toString())
                                                                    : std::nullopt;
    if (point.id.isNull() || !parsed) return fail(tr("A point needs a UUID id and decimal-string ticks"));
    point.ticks = *parsed;
    bool ok = false;
    point.value = map.value(QStringLiteral("value")).toDouble(&ok);
    const auto interpolation = interpolationFromName(map.value(QStringLiteral("interpolation"),
                                                               QStringLiteral("linear")).toString());
    if (!ok || !interpolation) return fail(tr("A point needs a numeric value and a known interpolation"));
    point.interpolation = *interpolation;
    for (const auto &[key, target] : {std::pair{QStringLiteral("inHandle"), &point.inHandle},
                                      std::pair{QStringLiteral("outHandle"), &point.outHandle}}) {
        if (!map.contains(key)) continue;
        const QVariantMap handle = map.value(key).toMap();
        bool okX = false, okY = false;
        *target = AutomationHandle{handle.value(QStringLiteral("x")).toDouble(&okX),
                                   handle.value(QStringLiteral("y")).toDouble(&okY)};
        if (!okX || !okY) return fail(tr("A handle needs numeric x and y"));
    }
    return point;
}

} // namespace AutomationCodec
} // namespace QindaTK
