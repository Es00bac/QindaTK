// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_path.h"
#include <algorithm>

namespace QindaTK::CanvasPath {
QPainterPath local(const CanvasObject &object)
{
    QPainterPath path;
    path.setFillRule(object.fillRule);
    if (object.shape != CanvasShape::Path || object.pathNodes.isEmpty()) return path;
    const auto &nodes = object.pathNodes;
    path.moveTo(nodes.first().anchor);
    const int segments = object.pathClosed ? nodes.size() : nodes.size() - 1;
    for (int i = 0; i < segments; ++i) {
        const auto &a = nodes[i];
        const auto &b = nodes[(i + 1) % nodes.size()];
        if (a.out == a.anchor && b.in == b.anchor) path.lineTo(b.anchor);
        else path.cubicTo(a.out, b.in, b.anchor);
    }
    if (object.pathClosed) path.closeSubpath();
    return path;
}

QPainterPath document(const CanvasObject &object)
{ return object.transform.map(local(object)); }

QPen pen(const CanvasObject &object, qreal minimumWidth)
{
    QPen value(object.stroke, std::max(object.strokeWidth, minimumWidth));
    value.setCapStyle(object.strokeCap);
    value.setJoinStyle(object.strokeJoin);
    value.setMiterLimit(4.0);
    if (!object.dashPattern.isEmpty()) value.setDashPattern(object.dashPattern);
    return value;
}
} // namespace QindaTK::CanvasPath
