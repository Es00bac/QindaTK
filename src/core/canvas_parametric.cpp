// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_parametric.h"
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasParametric {
namespace {
// Cubic handle length of a unit quarter circle.
constexpr qreal Kappa = 0.5522847498307936;

QVector<CanvasPathNode> roundedRect(const QRectF &r, qreal radius)
{
    const qreal x = r.left(), y = r.top(), right = r.right(), bottom = r.bottom();
    QVector<CanvasPathNode> result;
    if (radius == 0) {
        for (const QPointF p : {QPointF(x,y), QPointF(right,y),
                                QPointF(right,bottom), QPointF(x,bottom)})
            result.append({p,p,p});
        return result;
    }
    const qreal k = Kappa * radius;
    // The two ends of each straight side, clockwise from the top side. The
    // first end keeps the incoming arc handle, the second the outgoing one.
    const CanvasPathNode ends[8] = {
        {{x+radius,y}, {x+radius-k,y}, {x+radius,y}},
        {{right-radius,y}, {right-radius,y}, {right-radius+k,y}},
        {{right,y+radius}, {right,y+radius-k}, {right,y+radius}},
        {{right,bottom-radius}, {right,bottom-radius}, {right,bottom-radius+k}},
        {{right-radius,bottom}, {right-radius+k,bottom}, {right-radius,bottom}},
        {{x+radius,bottom}, {x+radius,bottom}, {x+radius-k,bottom}},
        {{x,bottom-radius}, {x,bottom-radius+k}, {x,bottom-radius}},
        {{x,y+radius}, {x,y+radius}, {x,y+radius-k}}};
    // AGENT-NOTE: a radius of half a side leaves that side zero long. Its two
    // ends merge into one smooth node so generated paths never hold a
    // zero-length segment; a full-radius square is the 4-node cubic circle.
    const bool horizontalSides = radius * 2 < r.width();
    const bool verticalSides = radius * 2 < r.height();
    for (int i = 0; i < 8; i += 2) {
        if (i % 4 == 0 ? horizontalSides : verticalSides) result << ends[i] << ends[i + 1];
        else result.append({ends[i].anchor, ends[i].in, ends[i + 1].out});
    }
    return result;
}

// Vertices lie on the ellipse inscribed in the bounds, starting at the top
// centre and running clockwise on screen (y down); star tips alternate with
// inner vertices at `innerRatio` of the outer radius.
QVector<CanvasPathNode> radial(const QRectF &r, int count, bool star, qreal innerRatio)
{
    QVector<CanvasPathNode> result;
    const QPointF center = r.center();
    const qreal rx = r.width() / 2, ry = r.height() / 2;
    constexpr qreal pi = 3.14159265358979323846;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        const qreal angle = -pi / 2 + qreal(2) * pi * i / count;
        const qreal scale = star && i % 2 ? innerRatio : 1;
        const QPointF p(center.x() + std::cos(angle) * rx * scale,
                        center.y() + std::sin(angle) * ry * scale);
        result.append({p,p,p});
    }
    return result;
}
}

CanvasParametricKind kind(const QString &id)
{
    if (id == QLatin1String("polygon")) return CanvasParametricKind::Polygon;
    if (id == QLatin1String("star")) return CanvasParametricKind::Star;
    if (id == QLatin1String("rounded-rectangle")) return CanvasParametricKind::RoundedRect;
    return CanvasParametricKind::None;
}
QString id(CanvasParametricKind kind)
{
    switch (kind) {
    case CanvasParametricKind::Polygon: return QStringLiteral("polygon");
    case CanvasParametricKind::Star: return QStringLiteral("star");
    case CanvasParametricKind::RoundedRect: return QStringLiteral("rounded-rectangle");
    case CanvasParametricKind::None: return {};
    }
    return {};
}
bool valid(const CanvasObject &o)
{
    if (o.parametricKind == CanvasParametricKind::None)
        return o.parametricPoints == 0 && o.parametricInnerRatio == 0
            && o.parametricCornerRadius == 0;
    const QRectF &r = o.rect;
    // Finite far edges keep every generated coordinate finite.
    if (o.shape != CanvasShape::Path || !o.pathClosed || !o.extraContours.isEmpty()
        || !std::isfinite(r.left()) || !std::isfinite(r.top())
        || !std::isfinite(r.right()) || !std::isfinite(r.bottom())
        || !(r.width() > 0) || !(r.height() > 0)) return false;
    const bool countInRange = o.parametricPoints >= MinimumPoints
        && o.parametricPoints <= MaximumPoints;
    if (o.parametricKind == CanvasParametricKind::RoundedRect)
        return o.parametricPoints == 0 && o.parametricInnerRatio == 0
            && std::isfinite(o.parametricCornerRadius) && o.parametricCornerRadius >= 0
            && o.parametricCornerRadius <= std::min(r.width(), r.height()) / 2;
    if (o.parametricKind == CanvasParametricKind::Polygon)
        return countInRange && o.parametricInnerRatio == 0 && o.parametricCornerRadius == 0;
    if (o.parametricKind == CanvasParametricKind::Star)
        return countInRange && std::isfinite(o.parametricInnerRatio)
            && o.parametricInnerRatio > 0 && o.parametricInnerRatio < 1
            && o.parametricCornerRadius == 0;
    return false;
}
QVector<CanvasPathNode> nodes(const CanvasObject &o)
{
    if (!valid(o) || o.parametricKind == CanvasParametricKind::None) return {};
    if (o.parametricKind == CanvasParametricKind::RoundedRect)
        return roundedRect(o.rect, o.parametricCornerRadius);
    const bool star = o.parametricKind == CanvasParametricKind::Star;
    return radial(o.rect, int(o.parametricPoints) * (star ? 2 : 1), star, o.parametricInnerRatio);
}
bool matchesNodes(const CanvasObject &o)
{
    if (o.parametricKind == CanvasParametricKind::None) return true;
    const QVector<CanvasPathNode> expected = nodes(o);
    if (expected.isEmpty() || expected.size() != o.pathNodes.size()) return false;
    const QRectF &r = o.rect;
    const qreal tolerance = 1e-9 * std::max({qreal(1), std::abs(r.left()), std::abs(r.right()),
                                             std::abs(r.top()), std::abs(r.bottom())});
    const auto near = [tolerance](QPointF a, QPointF b) {
        return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
    };
    for (qsizetype i = 0; i < expected.size(); ++i) {
        const CanvasPathNode &a = expected.at(i), &b = o.pathNodes.at(i);
        if (!near(a.anchor, b.anchor) || !near(a.in, b.in) || !near(a.out, b.out)) return false;
    }
    return true;
}
} // namespace QindaTK::CanvasParametric
