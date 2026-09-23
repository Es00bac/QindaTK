// SPDX-License-Identifier: LGPL-3.0-or-later
#include "polar_transform.h"

#include <cmath>
#include <algorithm>
#include <numbers>

namespace QindaTK {
namespace {
QPointF invalidPoint()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    return {nan, nan};
}
}

double PolarTransform::normalizeDegrees(double degrees)
{
    if (!std::isfinite(degrees)) return std::numeric_limits<double>::quiet_NaN();
    double result = std::fmod(degrees, 360.0);
    if (result < 0) result += 360.0;
    return result;
}

QPointF PolarTransform::project(double angle, double radius, const QRectF &square,
    double radialMinimum, double radialMaximum, bool radians,
    double startAngleDegrees, bool clockwise)
{
    if (!std::isfinite(angle) || !std::isfinite(radius) || !std::isfinite(startAngleDegrees)
        || !std::isfinite(radialMinimum) || !std::isfinite(radialMaximum)
        || radialMaximum <= radialMinimum || square.width() <= 0 || square.height() <= 0)
        return invalidPoint();
    const double sourceDegrees = radians ? angle * 180.0 / std::numbers::pi : angle;
    const double bearing = normalizeDegrees(startAngleDegrees
        + (clockwise ? sourceDegrees : -sourceDegrees));
    const double theta = (bearing - 90.0) * std::numbers::pi / 180.0;
    const double fraction = (radius - radialMinimum) / (radialMaximum - radialMinimum);
    const double extent = std::min(square.width(), square.height()) / 2.0;
    return {square.center().x() + std::cos(theta) * fraction * extent,
            square.center().y() + std::sin(theta) * fraction * extent};
}

PolarCoordinates PolarTransform::unproject(const QPointF &point, const QRectF &square,
    double radialMinimum, double radialMaximum, bool radians,
    double startAngleDegrees, bool clockwise)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())
        || !std::isfinite(startAngleDegrees) || !std::isfinite(radialMinimum)
        || !std::isfinite(radialMaximum) || radialMaximum <= radialMinimum
        || square.width() <= 0 || square.height() <= 0) return {};
    const QPointF offset = point - square.center();
    const double extent = std::min(square.width(), square.height()) / 2.0;
    if (offset.isNull())
        return {std::numeric_limits<double>::quiet_NaN(), radialMinimum};
    const double bearing = normalizeDegrees(
        std::atan2(offset.y(), offset.x()) * 180.0 / std::numbers::pi + 90.0);
    const double sourceDegrees = normalizeDegrees(
        (clockwise ? 1.0 : -1.0) * (bearing - startAngleDegrees));
    return {radians ? sourceDegrees * std::numbers::pi / 180.0 : sourceDegrees,
            radialMinimum + std::hypot(offset.x(), offset.y()) / extent
                                * (radialMaximum - radialMinimum)};
}

double PolarTransform::angularDistanceDegrees(double a, double b)
{
    const double difference = normalizeDegrees(a - b);
    return std::min(difference, 360.0 - difference);
}

} // namespace QindaTK
