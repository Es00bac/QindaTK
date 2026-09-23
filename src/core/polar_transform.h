// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPointF>
#include <QRectF>
#include <limits>

namespace QindaTK {

struct PolarCoordinates {
    double angle = std::numeric_limits<double>::quiet_NaN();
    double radius = std::numeric_limits<double>::quiet_NaN();
};

// AGENT-CONTRACT: zero is north; positive angles are clockwise unless
// clockwise=false. The angular unit applies to source angles and the inverse,
// while startAngleDegrees always rotates the zero direction in degrees.
class PolarTransform {
public:
    static double normalizeDegrees(double degrees);
    static QPointF project(double angle, double radius, const QRectF &square,
                           double radialMinimum, double radialMaximum,
                           bool radians, double startAngleDegrees, bool clockwise);
    static PolarCoordinates unproject(const QPointF &point, const QRectF &square,
                                      double radialMinimum, double radialMaximum,
                                      bool radians, double startAngleDegrees, bool clockwise);
    static double angularDistanceDegrees(double a, double b);
};

} // namespace QindaTK
