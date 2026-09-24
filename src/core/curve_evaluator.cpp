// SPDX-License-Identifier: LGPL-3.0-or-later
#include "curve_evaluator_p.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace QindaTK {
namespace CurveDetail {

OperationError error(ErrorCode code, const QString &field, const QString &message)
{
    return {code, message, field, QStringLiteral("curve-cpu")};
}
QPointF outgoing(const CurveKnot &left, const CurveKnot &right)
{
    return left.outHandle.value_or(QPointF(std::lerp(left.x, right.x, 1.0 / 3),
                                           std::lerp(left.y, right.y, 1.0 / 3)));
}
QPointF incoming(const CurveKnot &left, const CurveKnot &right)
{
    return right.inHandle.value_or(QPointF(std::lerp(left.x, right.x, 2.0 / 3),
                                           std::lerp(left.y, right.y, 2.0 / 3)));
}
std::optional<double> edgeSlope(const CurveKnot &left, const CurveKnot &right, bool atStart)
{
    if (left.interpolation == CurveInterpolation::Step) return 0.0;
    if (left.interpolation == CurveInterpolation::Linear) return (right.y - left.y) / (right.x - left.x);
    const QPointF origin = atStart ? QPointF(left.x, left.y) : QPointF(right.x, right.y);
    const std::array<QPointF, 3> points = atStart
        ? std::array{outgoing(left, right), incoming(left, right), QPointF(right.x, right.y)}
        : std::array{incoming(left, right), outgoing(left, right), QPointF(left.x, left.y)};
    // The first noncoincident control determines the one-sided limiting
    // slope, even when the first derivative is zero at the endpoint.
    for (const auto &point : points) {
        const auto difference = point - origin;
        if (difference.x() == 0 && difference.y() == 0) continue;
        if (difference.x() == 0) return std::nullopt;
        const double slope = difference.y() / difference.x();
        return std::isfinite(slope) ? std::optional{slope} : std::nullopt;
    }
    return 0.0;
}

} // namespace CurveDetail
namespace {

double bezier(double p0, double p1, double p2, double p3, double t)
{
    const double a = std::lerp(p0, p1, t);
    const double b = std::lerp(p1, p2, t);
    const double c = std::lerp(p2, p3, t);
    return std::lerp(std::lerp(a, b, t), std::lerp(b, c, t), t);
}
double segment(const CurveKnot &left, const CurveKnot &right, double x)
{
    if (left.interpolation == CurveInterpolation::Step) return left.y;
    const double fraction = (x - left.x) / (right.x - left.x);
    if (left.interpolation == CurveInterpolation::Linear) return std::lerp(left.y, right.y, fraction);
    const auto out = CurveDetail::outgoing(left, right);
    const auto in = CurveDetail::incoming(left, right);
    const double x1 = (out.x() - left.x) / (right.x - left.x);
    const double x2 = (in.x() - left.x) / (right.x - left.x);
    double low = 0, high = 1;
    // AGENT-GUARD: controls are ordered by validate(), giving a unique
    // monotone x inverse even for vertical endpoint tangents. Solve in local
    // segment coordinates; a Newton-only step fails when dx/dt vanishes.
    for (int i = 0; i < 64; ++i) {
        const double mid = std::midpoint(low, high);
        if (mid == low || mid == high) break;
        if (bezier(0, x1, x2, 1, mid) < fraction) low = mid;
        else high = mid;
    }
    return bezier(left.y, out.y(), in.y(), right.y, std::midpoint(low, high));
}
CurveEvaluation evaluateValid(const CurveDefinition &curve, double originalX)
{
    if (!std::isfinite(originalX))
        return {{}, CurveDetail::error(ErrorCode::InvalidArgument, QStringLiteral("x"),
                                      QStringLiteral("Query coordinate must be finite"))};
    const auto &first = curve.knots.first();
    const auto &last = curve.knots.last();
    double x = originalX;
    double y;
    const bool before = x < first.x;
    const bool after = x > last.x;
    const auto mode = before ? curve.before : curve.after;
    if ((before || after) && mode == CurveExtrapolation::Repeat) {
        const double period = last.x - first.x;
        double offset = std::fmod(x - first.x, period);
        if (offset < 0) offset += period;
        x = first.x + offset;
        // Rounding an offset near the seam must retain the half-open policy.
        if (x >= last.x) x = first.x;
    } else if (before || after) {
        const auto &edge = before ? first : last;
        double slope = 0;
        if (mode == CurveExtrapolation::Linear && curve.knots.size() > 1) {
            const qsizetype index = before ? 0 : curve.knots.size() - 2;
            slope = *CurveDetail::edgeSlope(curve.knots[index], curve.knots[index + 1], before);
        }
        y = std::fma(slope, x - edge.x, edge.y);
        const double value = std::lerp(curve.yDomain.minimum, curve.yDomain.maximum, y);
        if (!std::isfinite(y) || !std::isfinite(value))
            return {{}, CurveDetail::error(ErrorCode::InvalidArgument, QStringLiteral("x"),
                                          QStringLiteral("Extrapolated value exceeds finite double range"))};
        return {CurveSample{originalX, y, {value, curve.yDomain.dimension, curve.yDomain.unitId}}, {}};
    }
    const auto right = std::lower_bound(curve.knots.begin(), curve.knots.end(), x,
        [](const CurveKnot &knot, double value) { return knot.x < value; });
    if (right == curve.knots.end()) y = last.y;
    else if (right->x == x || right == curve.knots.begin()) y = right->y;
    else y = segment(*(right - 1), *right, x);
    const double value = std::lerp(curve.yDomain.minimum, curve.yDomain.maximum, y);
    if (!std::isfinite(value))
        return {{}, CurveDetail::error(ErrorCode::InvalidArgument, QStringLiteral("yDomain"),
                                      QStringLiteral("Mapped value exceeds finite double range"))};
    return {CurveSample{originalX, y, {value, curve.yDomain.dimension, curve.yDomain.unitId}}, {}};
}

} // namespace

CurveEvaluation CurveEvaluator::evaluateNormalized(const CurveDefinition &curve, double x)
{
    if (auto error = validate(curve)) return {{}, error};
    return evaluateValid(curve, x);
}

CurveEvaluation CurveEvaluator::evaluate(const CurveDefinition &curve, const UnitQuantity &x)
{
    if (auto error = validate(curve)) return {{}, error};
    if (!std::isfinite(x.value) || x.dimension != curve.xDomain.dimension || x.unitId != curve.xDomain.unitId)
        return {{}, CurveDetail::error(ErrorCode::InvalidArgument, QStringLiteral("x"),
                                      QStringLiteral("Query must be finite and match the declared axis unit/dimension"))};
    const double span = curve.xDomain.maximum - curve.xDomain.minimum;
    double normalized = (x.value - curve.xDomain.minimum) / span;
    if (!std::isfinite(normalized)) normalized = x.value / span - curve.xDomain.minimum / span;
    return evaluateValid(curve, normalized);
}

} // namespace QindaTK
