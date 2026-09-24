// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document.h"
#include "canvas_svg_export.h"
#include <cmath>

namespace QindaTK {
namespace {
bool readCoordinate(const QVariantMap &map, const QString &key, double fallback, double *out)
{
    bool ok = false;
    *out = map.contains(key) ? map.value(key).toDouble(&ok) : fallback;
    return (ok || !map.contains(key)) && std::isfinite(*out);
}
bool readNode(const QVariantMap &map, CanvasPathNode *node)
{
    double x, y, ix, iy, ox, oy;
    if (!readCoordinate(map, QStringLiteral("x"), qQNaN(), &x)
        || !readCoordinate(map, QStringLiteral("y"), qQNaN(), &y)
        || !readCoordinate(map, QStringLiteral("inX"), x, &ix)
        || !readCoordinate(map, QStringLiteral("inY"), y, &iy)
        || !readCoordinate(map, QStringLiteral("outX"), x, &ox)
        || !readCoordinate(map, QStringLiteral("outY"), y, &oy)) return false;
    *node = {{x,y}, {ix,iy}, {ox,oy}};
    return true;
}
QVariantMap nodeMap(const CanvasPathNode &node)
{
    return {{QStringLiteral("x"), node.anchor.x()}, {QStringLiteral("y"), node.anchor.y()},
            {QStringLiteral("inX"), node.in.x()}, {QStringLiteral("inY"), node.in.y()},
            {QStringLiteral("outX"), node.out.x()}, {QStringLiteral("outY"), node.out.y()}};
}
}

qulonglong CanvasDocument::addPath(const QVariantList &nodes, bool closed,
                                  const QColor &fill, const QColor &stroke, qreal strokeWidth)
{
    CanvasObject path;
    path.shape = CanvasShape::Path;
    path.pathClosed = closed;
    path.fill = fill;
    path.stroke = stroke;
    path.strokeWidth = strokeWidth;
    for (const QVariant &value : nodes) {
        CanvasPathNode node;
        if (!readNode(value.toMap(), &node)) return 0;
        path.pathNodes.append(node);
    }
    return insert(path);
}

QVariantList CanvasDocument::pathNodes(qulonglong id) const
{
    QVariantList nodes;
    const CanvasObject *path = object(id);
    if (!path || path->shape != CanvasShape::Path) return nodes;
    for (const auto &node : path->pathNodes) nodes.append(nodeMap(node));
    return nodes;
}

bool CanvasDocument::movePathPart(qulonglong id, int index, const QString &part, qreal x, qreal y)
{
    const CanvasObject *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)
        || index < 0 || index >= before->pathNodes.size() || !std::isfinite(x) || !std::isfinite(y)) return false;
    CanvasObject after = *before;
    auto &node = after.pathNodes[index];
    const QPointF to(x,y);
    if (part == QLatin1String("anchor")) {
        const QPointF delta = to - node.anchor;
        node.anchor = to; node.in += delta; node.out += delta;
    } else if (part == QLatin1String("in")) node.in = to;
    else if (part == QLatin1String("out")) node.out = to;
    else return false;
    if (after.pathNodes == before->pathNodes) return true;
    // AGENT-GUARD (docs/vector-authoring.md#parametric-shapes): direct node
    // edits demote a generated shape to freeform so later parameter edits
    // cannot erase them; validation rejects an edit that forgets to.
    after.clearParametric();
    CanvasEdit edit;
    edit.label = tr("Move path node or handle");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}

bool CanvasDocument::insertPathNode(qulonglong id, int index, const QVariantMap &value)
{
    const CanvasObject *before = object(id);
    CanvasPathNode node;
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)
        || index < 0 || index > before->pathNodes.size() || !readNode(value, &node)) return false;
    CanvasObject after = *before;
    after.pathNodes.insert(index, node);
    after.clearParametric();
    CanvasEdit edit;
    edit.label = tr("Insert path node");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}

bool CanvasDocument::removePathNode(qulonglong id, int index)
{
    const CanvasObject *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)
        || index < 0 || index >= before->pathNodes.size()) return false;
    CanvasObject after = *before;
    after.pathNodes.removeAt(index);
    after.clearParametric();
    CanvasEdit edit;
    edit.label = tr("Remove path node");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}

bool CanvasDocument::setPathClosed(qulonglong id, bool closed)
{
    const CanvasObject *before = object(id);
    if (!before || before->shape != CanvasShape::Path || effectiveLocked(*before)) return false;
    if (before->pathClosed == closed) return true;
    CanvasObject after = *before;
    after.pathClosed = closed;
    after.clearParametric();
    CanvasEdit edit;
    edit.label = tr("Set path closure");
    edit.changes.append({id, *before, after});
    return !commitEdit(edit);
}

bool CanvasDocument::exportSvg(const QString &filePath)
{
    OperationError error;
    if (!CanvasSvgExport::write(*this, filePath, &error)) {
        m_lastError = error.message;
        emit historyChanged();
        return false;
    }
    m_lastError.clear();
    emit historyChanged();
    return true;
}
} // namespace QindaTK
