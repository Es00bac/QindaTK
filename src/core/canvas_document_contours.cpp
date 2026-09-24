// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document.h"
#include <cmath>

namespace QindaTK {
namespace {
bool readPoint(const QVariantMap &map, const QString &key, double fallback, double *out)
{
    bool ok = false;
    *out = map.contains(key) ? map.value(key).toDouble(&ok) : fallback;
    return (ok || !map.contains(key)) && std::isfinite(*out);
}
bool readNode(const QVariantMap &map, CanvasPathNode *node)
{
    double x, y, ix, iy, ox, oy;
    if (!readPoint(map, QStringLiteral("x"), qQNaN(), &x)
        || !readPoint(map, QStringLiteral("y"), qQNaN(), &y)
        || !readPoint(map, QStringLiteral("inX"), x, &ix)
        || !readPoint(map, QStringLiteral("inY"), y, &iy)
        || !readPoint(map, QStringLiteral("outX"), x, &ox)
        || !readPoint(map, QStringLiteral("outY"), y, &oy)) return false;
    *node = {{x,y}, {ix,iy}, {ox,oy}};
    return true;
}
QVariantMap nodeMap(const CanvasPathNode &node)
{
    return {{QStringLiteral("x"), node.anchor.x()}, {QStringLiteral("y"), node.anchor.y()},
            {QStringLiteral("inX"), node.in.x()}, {QStringLiteral("inY"), node.in.y()},
            {QStringLiteral("outX"), node.out.x()}, {QStringLiteral("outY"), node.out.y()}};
}
QVector<CanvasPathNode> *nodesAt(CanvasObject &o, int contour)
{
    if (contour == 0) return &o.pathNodes;
    if (contour > 0 && contour <= o.extraContours.size()) return &o.extraContours[contour - 1].nodes;
    return nullptr;
}
}

int CanvasDocument::pathContourCount(qulonglong id) const
{
    const auto *o = object(id);
    return o && o->shape == CanvasShape::Path ? 1 + o->extraContours.size() : 0;
}
QVariantList CanvasDocument::pathContourNodes(qulonglong id, int contour) const
{
    QVariantList values;
    const auto *o = object(id);
    if (!o || o->shape != CanvasShape::Path) return values;
    const auto *nodes = contour == 0 ? &o->pathNodes
        : contour > 0 && contour <= o->extraContours.size() ? &o->extraContours[contour - 1].nodes : nullptr;
    if (nodes) for (const auto &node : *nodes) values.append(nodeMap(node));
    return values;
}
bool CanvasDocument::addPathContour(qulonglong id, const QVariantList &values, bool closed)
{
    const auto *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)) return false;
    CanvasPathContour contour;
    contour.closed = closed;
    for (const auto &value : values) {
        CanvasPathNode node;
        if (!readNode(value.toMap(), &node)) return false;
        contour.nodes.append(node);
    }
    CanvasObject after = *before;
    after.extraContours.append(std::move(contour));
    after.clearParametric();
    CanvasEdit edit;
    edit.label = tr("Add path contour");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}
bool CanvasDocument::removePathContour(qulonglong id, int contour)
{
    const auto *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)
        || contour <= 0 || contour > before->extraContours.size()) return false;
    CanvasObject after = *before;
    after.extraContours.removeAt(contour - 1);
    CanvasEdit edit;
    edit.label = tr("Remove path contour");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}
bool CanvasDocument::movePathContourPart(qulonglong id, int contour, int index,
                                         const QString &part, qreal x, qreal y)
{
    if (contour == 0) return movePathPart(id, index, part, x, y);
    const auto *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)
        || !std::isfinite(x) || !std::isfinite(y)) return false;
    CanvasObject after = *before;
    auto *nodes = nodesAt(after, contour);
    if (!nodes || index < 0 || index >= nodes->size()) return false;
    auto &node = (*nodes)[index];
    const QPointF to(x, y);
    if (part == QLatin1String("anchor")) {
        const QPointF delta = to - node.anchor;
        node.anchor = to; node.in += delta; node.out += delta;
    } else if (part == QLatin1String("in")) node.in = to;
    else if (part == QLatin1String("out")) node.out = to;
    else return false;
    if (after == *before) return true;
    CanvasEdit edit;
    edit.label = tr("Move path node or handle");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}
bool CanvasDocument::insertPathContourNode(qulonglong id, int contour, int index, const QVariantMap &value)
{
    if (contour == 0) return insertPathNode(id, index, value);
    const auto *before = object(id);
    CanvasPathNode node;
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)
        || !readNode(value, &node)) return false;
    CanvasObject after = *before;
    auto *nodes = nodesAt(after, contour);
    if (!nodes || index < 0 || index > nodes->size()) return false;
    nodes->insert(index, node);
    CanvasEdit edit;
    edit.label = tr("Insert path node");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}
bool CanvasDocument::removePathContourNode(qulonglong id, int contour, int index)
{
    if (contour == 0) return removePathNode(id, index);
    const auto *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)) return false;
    CanvasObject after = *before;
    auto *nodes = nodesAt(after, contour);
    if (!nodes || index < 0 || index >= nodes->size()) return false;
    nodes->removeAt(index);
    CanvasEdit edit;
    edit.label = tr("Remove path node");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}
bool CanvasDocument::setPathContourClosed(qulonglong id, int contour, bool closed)
{
    if (contour == 0) return setPathClosed(id, closed);
    const auto *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)
        || contour <= 0 || contour > before->extraContours.size()) return false;
    CanvasObject after = *before;
    after.extraContours[contour - 1].closed = closed;
    CanvasEdit edit;
    edit.label = tr("Set path closure");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}
} // namespace QindaTK
