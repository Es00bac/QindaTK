// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "curve_evaluator.h"

#include <QUuid>
#include <QVariantMap>
#include <QVector>
#include <optional>

namespace QindaTK {

// read/write/touch/latch, plus off. The automation engine owns what each mode
// does (AutomationModel is the reference implementation); UI only requests.
enum class AutomationMode { Off, Read, Touch, Latch, Write };

// A parameter's canonical domain. Stored values are in range.unitId units and
// never pass through normalized or pixel space; normalized 0..1 is only the
// affine CurveEditor projection of [minimum, maximum].
struct AutomationParameter {
    QString id;                 // stable parameter/curve ID, e.g. "track:<uuid>/cutoff"
    QString name;
    CurveAxisDomain range;      // dimension, unitId, minimum < maximum
    double defaultValue = 0.0;  // value when the lane has no points
    double step = 0.0;          // > 0: edited and recorded values snap to minimum + k * step
    CurveInterpolation interpolation = CurveInterpolation::Linear; // for new points
};
bool operator==(const AutomationParameter &a, const AutomationParameter &b);

// A cubic handle in its segment's local frame: x is the fraction of the
// segment's exact tick span, y the normalized value. Exact double compare.
struct AutomationHandle {
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const AutomationHandle &, const AutomationHandle &) = default;
};

struct AutomationPoint {
    QUuid id;
    qint64 ticks = 0;           // exact timeline ticks (the timeline's timebase)
    double value = 0.0;         // canonical units, within the parameter range
    CurveInterpolation interpolation = CurveInterpolation::Linear; // outgoing segment
    std::optional<AutomationHandle> inHandle;  // incoming segment (from the previous point)
    std::optional<AutomationHandle> outHandle; // outgoing segment
    friend bool operator==(const AutomationPoint &, const AutomationPoint &) = default;
};

// One point or parameter before/after a transaction (missing = absent).
struct AutomationChange {
    QString parameterId;
    QUuid pointId;              // null for a parameter change
    std::optional<AutomationPoint> before, after;
    std::optional<AutomationParameter> parameterBefore, parameterAfter;
};

struct AutomationChangeSet {
    QString transactionId;
    QString label;
    QVector<AutomationChange> changes;
};

// Pure evaluation shared by the model, the lanes and engine snapshots.
namespace AutomationCurve {
inline constexpr qsizetype kMaxPoints = 65536;
double normalized(const AutomationParameter &parameter, double value);
// Clamps into range and applies the step grid.
double quantized(const AutomationParameter &parameter, double value);
// Value at exact ticks: points hold before the first and after the last;
// step and linear evaluate in canonical units, cubic through CurveEvaluator on
// the segment's local normalized curve. No points: defaultValue.
double valueAt(const AutomationParameter &parameter, const QVector<AutomationPoint> &points, qint64 ticks);
OperationError validateParameter(const AutomationParameter &parameter);
OperationError validate(const AutomationParameter &parameter, const QVector<AutomationPoint> &points);
// The CurveEvaluator definition of the segment starting at points[index].
CurveDefinition segmentCurve(const AutomationParameter &parameter, const AutomationPoint &left,
                             const AutomationPoint &right);
void sort(QVector<AutomationPoint> &points);
} // namespace AutomationCurve

// QML/JSON maps: ticks are decimal strings, values JSON numbers.
namespace AutomationCodec {
QString modeName(AutomationMode mode);
std::optional<AutomationMode> modeFromName(const QString &name);
QString interpolationName(CurveInterpolation interpolation);
std::optional<CurveInterpolation> interpolationFromName(const QString &name);
QString dimensionName(UnitDimension dimension);
std::optional<UnitDimension> dimensionFromName(const QString &name);
QVariantMap toMap(const AutomationParameter &parameter);
std::optional<AutomationParameter> parameterFromMap(const QVariantMap &map, QString *error);
QVariantMap toMap(const AutomationPoint &point);
std::optional<AutomationPoint> pointFromMap(const QVariantMap &map, QString *error);
} // namespace AutomationCodec

} // namespace QindaTK
