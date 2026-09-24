// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr double kToleranceMm = 1e-6;
constexpr double kMinimumLengthMm = 1e-9;
constexpr double kMaxCoordinateMm = 1000000.0;
double distance(double x, double y) { return std::hypot(x, y); }
}

bool CadDocument::validGeometry(const State &state, QString *diagnostic) const
{
    for (const Point &p : state.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)
            || std::abs(p.x) > kMaxCoordinateMm || std::abs(p.y) > kMaxCoordinateMm) {
            *diagnostic = QStringLiteral("Point coordinate exceeds CAD range"); return false;
        }
    }
    for (const Segment &segment : state.segments) {
        const int ai = pointIndex(state, segment.start), bi = pointIndex(state, segment.end);
        if (ai < 0 || bi < 0 || ai == bi) {
            *diagnostic = QStringLiteral("Segment has a missing endpoint"); return false;
        }
        const Point &a = state.points.at(ai), &b = state.points.at(bi);
        if (distance(b.x - a.x, b.y - a.y) <= kMinimumLengthMm) {
            *diagnostic = QStringLiteral("Degenerate zero-length segment"); return false;
        }
    }
    for (const Circle &circle : state.circles) {
        if (pointIndex(state, circle.center) < 0 || !std::isfinite(circle.radius)
            || circle.radius <= kMinimumLengthMm || circle.radius > kMaxCoordinateMm) {
            *diagnostic = QStringLiteral("Degenerate circle or missing center"); return false;
        }
    }
    return true;
}

// AGENT-NOTE (docs/cad.md): projections are solved on a candidate snapshot.
// Only a converged, nondegenerate result is committed; failed edits are atomic.
bool CadDocument::solve(State *state, const QString &lockedPointId, QString *diagnostic) const
{
    auto endpoints = [&](const Constraint &c) {
        if (c.kind == QLatin1String("horizontal") || c.kind == QLatin1String("vertical")) {
            const Segment &s = state->segments.at(segmentIndex(*state, c.first));
            return QPair<int, int>(pointIndex(*state, s.start), pointIndex(*state, s.end));
        }
        return QPair<int, int>(pointIndex(*state, c.first), pointIndex(*state, c.second));
    };
    auto residual = [&](const Constraint &c) {
        const auto [ai, bi] = endpoints(c);
        const Point &a = state->points.at(ai), &b = state->points.at(bi);
        if (c.kind == QLatin1String("horizontal")) return std::abs(a.y - b.y);
        if (c.kind == QLatin1String("vertical")) return std::abs(a.x - b.x);
        const double separation = distance(b.x - a.x, b.y - a.y);
        return c.kind == QLatin1String("coincident") ? separation
            : std::abs(separation - c.value);
    };
    for (int iteration = 0; iteration < 256; ++iteration) {
        for (const Constraint &constraint : state->constraints) {
            const auto [ai, bi] = endpoints(constraint);
            Point &a = state->points[ai], &b = state->points[bi];
            const bool lockA = a.id == lockedPointId, lockB = b.id == lockedPointId;
            if (constraint.kind == QLatin1String("horizontal")) {
                const double middle = (a.y + b.y) / 2;
                if (!lockA) a.y = lockB ? b.y : middle;
                if (!lockB) b.y = lockA ? a.y : middle;
            } else if (constraint.kind == QLatin1String("vertical")) {
                const double middle = (a.x + b.x) / 2;
                if (!lockA) a.x = lockB ? b.x : middle;
                if (!lockB) b.x = lockA ? a.x : middle;
            } else if (constraint.kind == QLatin1String("coincident")) {
                const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
                if (!lockA) { a.x = lockB ? b.x : mx; a.y = lockB ? b.y : my; }
                if (!lockB) { b.x = lockA ? a.x : mx; b.y = lockA ? a.y : my; }
            } else {
                double dx = b.x - a.x, dy = b.y - a.y;
                double actual = distance(dx, dy);
                if (actual < kMinimumLengthMm) { dx = 1; dy = 0; actual = 1; }
                const double error = (actual - constraint.value) / actual;
                if (lockA) { b.x -= error * dx; b.y -= error * dy; }
                else if (lockB) { a.x += error * dx; a.y += error * dy; }
                else {
                    a.x += error * dx / 2; a.y += error * dy / 2;
                    b.x -= error * dx / 2; b.y -= error * dy / 2;
                }
            }
        }
        double largest = 0;
        const Constraint *worst = nullptr;
        for (const Constraint &constraint : state->constraints) {
            const double error = residual(constraint);
            if (error > largest) { largest = error; worst = &constraint; }
        }
        if (largest <= kToleranceMm) return validGeometry(*state, diagnostic);
        if (iteration == 255) {
            *diagnostic = QStringLiteral("Unsatisfiable %1 constraint %2: residual %3 mm")
                .arg(worst ? worst->kind : QStringLiteral("unknown"),
                     worst ? worst->id : QStringLiteral("unknown"),
                     QString::number(largest, 'g', 8));
            return false;
        }
    }
    return false;
}

} // namespace QindaTK
