// SPDX-License-Identifier: LGPL-3.0-or-later
#include "curve_evaluator_p.h"

#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {

bool normalized(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }
bool validDimension(UnitDimension dimension)
{
    switch (dimension) {
    case UnitDimension::Scalar: case UnitDimension::Length: case UnitDimension::Angle:
    case UnitDimension::Duration: case UnitDimension::Frequency: case UnitDimension::Gain:
        return true;
    }
    return false;
}
bool validEdge(CurveExtrapolation edge)
{
    return edge == CurveExtrapolation::Hold || edge == CurveExtrapolation::Linear
        || edge == CurveExtrapolation::Repeat;
}
bool validInterpolation(CurveInterpolation interpolation)
{
    return interpolation == CurveInterpolation::Step || interpolation == CurveInterpolation::Linear
        || interpolation == CurveInterpolation::CubicBezier;
}

} // namespace

OperationError CurveEvaluator::validate(const CurveDefinition &curve)
{
    auto invalid = [](const QString &field, const QString &message) {
        return CurveDetail::error(ErrorCode::Validation, field, message);
    };
    if (curve.id.trimmed().isEmpty())
        return invalid(QStringLiteral("id"), QStringLiteral("Curve ID is required"));
    for (bool horizontal : {true, false}) {
        const auto &axis = horizontal ? curve.xDomain : curve.yDomain;
        if (!validDimension(axis.dimension) || axis.unitId.trimmed().isEmpty()
            || !std::isfinite(axis.minimum) || !std::isfinite(axis.maximum)
            || axis.maximum <= axis.minimum || !std::isfinite(axis.maximum - axis.minimum))
            return invalid(horizontal ? QStringLiteral("xDomain") : QStringLiteral("yDomain"),
                           QStringLiteral("Axis requires a unit, dimension and finite increasing range"));
    }
    if (curve.knots.isEmpty() || curve.knots.size() > MaxKnots)
        return invalid(QStringLiteral("knots"), QStringLiteral("Curve requires 1 to 65536 knots"));
    if (!validEdge(curve.before) || !validEdge(curve.after))
        return invalid(QStringLiteral("extrapolation"), QStringLiteral("Unknown edge policy"));
    if (curve.knots.size() == 1
        && (curve.before == CurveExtrapolation::Repeat || curve.after == CurveExtrapolation::Repeat))
        return invalid(QStringLiteral("extrapolation"), QStringLiteral("Repeat requires a nonzero period"));
    QSet<QString> ids;
    for (qsizetype i = 0; i < curve.knots.size(); ++i) {
        const auto &knot = curve.knots[i];
        const QString path = QStringLiteral("knots[%1]").arg(i);
        if (knot.id.trimmed().isEmpty() || ids.contains(knot.id))
            return invalid(path + QStringLiteral(".id"), QStringLiteral("Knot IDs must be nonempty and unique"));
        ids.insert(knot.id);
        if (!normalized(knot.x) || !normalized(knot.y))
            return invalid(path, QStringLiteral("Knot coordinates must be finite and normalized"));
        if (i && knot.x - curve.knots[i - 1].x < MinimumXSpacing)
            return invalid(path + QStringLiteral(".x"), QStringLiteral("Knot x must increase by at least 1e-12"));
        if (!validInterpolation(knot.interpolation))
            return invalid(path + QStringLiteral(".interpolation"), QStringLiteral("Unknown interpolation"));
        for (bool incoming : {true, false}) {
            const auto &handle = incoming ? knot.inHandle : knot.outHandle;
            if (!handle) continue;
            const double low = incoming ? (i ? curve.knots[i - 1].x : 0.0) : knot.x;
            const double high = incoming ? knot.x :
                (i + 1 < curve.knots.size() ? curve.knots[i + 1].x : 1.0);
            if (!normalized(handle->x()) || !normalized(handle->y())
                || handle->x() < low || handle->x() > high)
                return invalid(path + (incoming ? QStringLiteral(".inHandle") : QStringLiteral(".outHandle")),
                               QStringLiteral("Handle must be normalized and lie between adjacent knot x values"));
        }
        if (i && curve.knots[i - 1].interpolation == CurveInterpolation::CubicBezier) {
            const auto &left = curve.knots[i - 1];
            if (CurveDetail::outgoing(left, knot).x() > CurveDetail::incoming(left, knot).x())
                return invalid(path + QStringLiteral(".inHandle"),
                               QStringLiteral("Cubic controls must be ordered in x"));
        }
    }
    if (curve.knots.size() > 1) {
        for (bool start : {true, false}) {
            if ((start ? curve.before : curve.after) != CurveExtrapolation::Linear) continue;
            const qsizetype index = start ? 0 : curve.knots.size() - 2;
            if (!CurveDetail::edgeSlope(curve.knots[index], curve.knots[index + 1], start))
                return invalid(start ? QStringLiteral("before") : QStringLiteral("after"),
                               QStringLiteral("Linear extrapolation requires a finite endpoint slope"));
        }
    }
    return {};
}

} // namespace QindaTK
