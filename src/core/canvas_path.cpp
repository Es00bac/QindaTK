// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_path.h"
#include <algorithm>

namespace QindaTK::CanvasPath {
QPainterPath local(const CanvasObject &object)
{
    QPainterPath path;
    path.setFillRule(object.fillRule);
    if (object.shape != CanvasShape::Path || object.pathNodes.isEmpty()) return path;
    const auto appendContour = [&path](const QVector<CanvasPathNode> &nodes, bool closed) {
        if (nodes.isEmpty()) return;
        path.moveTo(nodes.first().anchor);
        const int segments = closed ? nodes.size() : nodes.size() - 1;
        for (int i = 0; i < segments; ++i) {
            const auto &a = nodes[i];
            const auto &b = nodes[(i + 1) % nodes.size()];
            if (a.out == a.anchor && b.in == b.anchor) path.lineTo(b.anchor);
            else path.cubicTo(a.out, b.in, b.anchor);
        }
        if (closed) path.closeSubpath();
    };
    appendContour(object.pathNodes, object.pathClosed);
    for (const auto &contour : object.extraContours) appendContour(contour.nodes, contour.closed);
    return path;
}

QPainterPath document(const CanvasObject &object)
{ return object.transform.map(local(object)); }

QPen pen(const CanvasObject &object, qreal minimumWidth)
{
    QPen value(object.stroke, std::max(object.strokeWidth, minimumWidth));
    value.setCapStyle(object.strokeCap);
    value.setJoinStyle(object.strokeJoin);
    // AGENT-NOTE: Qt's stroker limits the miter length (tip to inner corner)
    // to limit x width, the SVG stroke-miterlimit ratio, which export writes.
    value.setMiterLimit(object.appearance->miterLimit);
    if (!object.dashPattern.isEmpty()) value.setDashPattern(object.dashPattern);
    return value;
}

QVector<CanvasPathContour> contours(const CanvasObject &object)
{
    const auto straight = [](QPointF p) { return CanvasPathNode{p, p, p}; };
    QVector<CanvasPathContour> result;
    switch (object.shape) {
    case CanvasShape::Path:
        result.append(CanvasPathContour{object.pathNodes, object.pathClosed});
        result.append(object.extraContours);
        break;
    case CanvasShape::Rect: {
        const QRectF &r = object.rect;
        result.append(CanvasPathContour{{straight(r.topLeft()), straight(r.topRight()),
                                         straight(r.bottomRight()), straight(r.bottomLeft())}, true});
        break;
    }
    case CanvasShape::Ellipse: {
        constexpr qreal kappa = 0.5522847498307936; // cubic quarter-circle handle ratio
        const QPointF c = object.rect.center();
        const qreal rx = object.rect.width() / 2, ry = object.rect.height() / 2;
        const qreal kx = rx * kappa, ky = ry * kappa;
        result.append(CanvasPathContour{{
            CanvasPathNode{{c.x() + rx, c.y()}, {c.x() + rx, c.y() - ky}, {c.x() + rx, c.y() + ky}},
            CanvasPathNode{{c.x(), c.y() + ry}, {c.x() + kx, c.y() + ry}, {c.x() - kx, c.y() + ry}},
            CanvasPathNode{{c.x() - rx, c.y()}, {c.x() - rx, c.y() + ky}, {c.x() - rx, c.y() - ky}},
            CanvasPathNode{{c.x(), c.y() - ry}, {c.x() - kx, c.y() - ry}, {c.x() + kx, c.y() - ry}}}, true});
        break;
    }
    case CanvasShape::Polyline: {
        CanvasPathContour open;
        open.closed = false;
        for (const QPointF &p : object.points) open.nodes.append(straight(p));
        result.append(open);
        break;
    }
    case CanvasShape::Image:
        break;
    }
    return result;
}
} // namespace QindaTK::CanvasPath
