// SPDX-License-Identifier: LGPL-3.0-or-later
#include "gradient_evaluator.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {

OperationError error(ErrorCode code, const QString &field, const QString &message)
{
    return {code, message, field, QStringLiteral("gradient-cpu")};
}
ColorTransformResult convert(const ColorValue &value, const ColorProfile &source,
                             const ColorProfile &target, ColorEngine &engine)
{
    ColorTransformRequest request;
    request.value = value;
    request.source = source;
    request.context.output = target;
    request.operation = ColorOperation::Output;
    return engine.convert(request);
}
GradientEvaluation failure(OperationError issue, const QString &prefix = {})
{
    if (!prefix.isEmpty()) issue.fieldPath = prefix + QLatin1Char('.') + issue.fieldPath;
    GradientEvaluation result;
    result.error = std::move(issue);
    return result;
}

} // namespace

OperationError GradientEvaluator::validate(const GradientDefinition &gradient)
{
    if (gradient.id.trimmed().isEmpty())
        return error(ErrorCode::Validation, QStringLiteral("id"), QStringLiteral("Gradient ID is required"));
    if (gradient.interpolation != GradientInterpolation::LinearSRgb
        || gradient.alphaPolicy != GradientAlphaPolicy::Premultiplied)
        return error(ErrorCode::Unsupported, QStringLiteral("interpolation/alphaPolicy"),
                     QStringLiteral("Only linear-sRGB light with premultiplied alpha is supported"));
    if (gradient.stops.isEmpty() || gradient.stops.size() > MaxStops)
        return error(ErrorCode::Validation, QStringLiteral("stops"), QStringLiteral("Gradient requires 1 to 4096 stops"));
    QSet<QString> ids;
    for (qsizetype i = 0; i < gradient.stops.size(); ++i) {
        const auto &stop = gradient.stops[i];
        const QString path = QStringLiteral("stops[%1]").arg(i);
        if (stop.id.trimmed().isEmpty() || ids.contains(stop.id))
            return error(ErrorCode::Validation, path + QStringLiteral(".id"),
                         QStringLiteral("Stop IDs must be nonempty and unique"));
        ids.insert(stop.id);
        if (!std::isfinite(stop.position) || stop.position < 0 || stop.position > 1
            || (i && stop.position < gradient.stops[i - 1].position))
            return error(ErrorCode::Validation, path + QStringLiteral(".position"),
                         QStringLiteral("Positions must be normalized and ordered nondecreasingly"));
        if (!stop.profile.isValid())
            return error(ErrorCode::Unavailable, path + QStringLiteral(".profile"),
                         QStringLiteral("Stop requires a resolved RGB ICC profile"));
        if (!stop.color.isValid())
            return error(ErrorCode::InvalidArgument, path + QStringLiteral(".color"),
                         QStringLiteral("Stop color must have finite RGB, alpha in [0,1] and encoding IDs"));
        if (stop.color.profileId != stop.profile.id() || stop.color.transferId != stop.profile.transferId())
            return error(ErrorCode::Validation, path + QStringLiteral(".color.profileId/transferId"),
                         QStringLiteral("Stop color encoding does not match its ICC profile"));
        const double limit = std::numeric_limits<float>::max();
        if (std::abs(stop.color.red) > limit || std::abs(stop.color.green) > limit
            || std::abs(stop.color.blue) > limit)
            return error(ErrorCode::InvalidArgument, path + QStringLiteral(".color.rgb"),
                         QStringLiteral("Stop RGB exceeds the ColorEngine Float32 range"));
    }
    return {};
}

GradientEvaluation GradientEvaluator::evaluate(const GradientDefinition &gradient, double position,
                                                const ColorProfile &target, ColorEngine &engine)
{
    if (auto issue = validate(gradient)) return failure(issue);
    if (!std::isfinite(position))
        return failure(error(ErrorCode::InvalidArgument, QStringLiteral("position"),
                             QStringLiteral("Query position must be finite")));
    if (!target.isValid())
        return failure(error(ErrorCode::Unavailable, QStringLiteral("target"),
                             QStringLiteral("A resolved target RGB ICC profile is required")));
    const auto linear = ColorProfile::linearSRgb();
    if (!linear.isValid())
        return failure(error(ErrorCode::BackendFailure, QStringLiteral("interpolation"),
                             QStringLiteral("Cannot resolve the linear-sRGB interpolation profile")));
    const auto &stops = gradient.stops;
    const auto upper = std::upper_bound(stops.begin(), stops.end(), position,
        [](double query, const GradientStop &stop) { return query < stop.position; });
    qsizetype left = 0, right = 0;
    double fraction = 0;
    if (upper == stops.end()) left = right = stops.size() - 1;
    else if (upper != stops.begin()) {
        left = (upper - stops.begin()) - 1;
        right = stops[left].position == position ? left : left + 1;
        if (left != right)
            fraction = (position - stops[left].position) / (stops[right].position - stops[left].position);
    }
    const auto a = convert(stops[left].color, stops[left].profile, linear, engine);
    if (a.error) return failure(a.error, QStringLiteral("stops[%1]").arg(left));
    const auto b = left == right ? a : convert(stops[right].color, stops[right].profile, linear, engine);
    if (b.error) return failure(b.error, QStringLiteral("stops[%1]").arg(right));
    ColorValue mixed{0, 0, 0, std::lerp(a.value->alpha, b.value->alpha, fraction),
                     linear.id(), linear.transferId()};
    if (mixed.alpha == 0) mixed.alpha = 0.0; // canonical positive zero
    if (mixed.alpha > 0) {
        // AGENT-GUARD: normalized coverage weights are algebraically the
        // premultiply/interpolate/unpremultiply operation. Scaling alpha first
        // avoids losing colored RGB when both alphas are tiny subnormals.
        const double scale = std::max(a.value->alpha, b.value->alpha);
        const double wa = (1 - fraction) * (a.value->alpha / scale);
        const double wb = fraction * (b.value->alpha / scale);
        const double weight = wb / (wa + wb);
        mixed.red = std::lerp(a.value->red, b.value->red, weight);
        mixed.green = std::lerp(a.value->green, b.value->green, weight);
        mixed.blue = std::lerp(a.value->blue, b.value->blue, weight);
    }
    auto output = convert(mixed, linear, target, engine);
    if (output.error) return failure(output.error, QStringLiteral("target"));
    // Gradient interpolation intentionally discards hidden RGB at zero alpha.
    // ColorEngine alone preserves it. Canonicalize in the target encoding too,
    // since an ICC transform need not map zero channels to zero channels.
    if (mixed.alpha == 0) {
        output.value->red = output.value->green = output.value->blue = 0;
        output.outsideNominalRange = false;
    }
    return {output.value, {}, output.precision, output.gamut, output.outsideNominalRange};
}

} // namespace QindaTK
