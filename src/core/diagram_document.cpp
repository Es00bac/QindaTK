// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QHash>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int kMaxNodes = 2000;
constexpr int kMaxEdges = 4000;
constexpr double kMaxCoordinate = 1000000.0;

bool validCoordinate(double value)
{
    return std::isfinite(value) && std::abs(value) <= kMaxCoordinate;
}

QString freshId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

DiagramDocument::DiagramDocument(QObject *parent) : QObject(parent) {}

void DiagramDocument::setError(const QString &message)
{
    if (message == m_error) return;
    m_error = message;
    emit errorChanged();
}

int DiagramDocument::nodeIndex(const QString &id) const
{
    for (int i = 0; i < m_state.nodes.size(); ++i)
        if (m_state.nodes.at(i).id == id) return i;
    return -1;
}

int DiagramDocument::edgeIndex(const QString &id) const
{
    for (int i = 0; i < m_state.edges.size(); ++i)
        if (m_state.edges.at(i).id == id) return i;
    return -1;
}

QString DiagramDocument::portType(const QString &nodeId, const QString &portId, bool output) const
{
    const int index = nodeIndex(nodeId);
    if (index < 0) return {};
    const auto &ports = output ? m_state.nodes.at(index).outputs : m_state.nodes.at(index).inputs;
    for (const Port &port : ports) if (port.id == portId) return port.type;
    return {};
}

QVariantList DiagramDocument::nodes() const
{
    QVariantList result;
    result.reserve(m_state.nodes.size());
    QHash<QString, double> incoming, outgoing;
    for (const Edge &edge : m_state.edges) {
        outgoing[edge.fromNode] += edge.flow;
        incoming[edge.toNode] += edge.flow;
    }
    for (const Node &node : m_state.nodes) {
        QVariantList inputs, outputs;
        for (const Port &port : node.inputs)
            inputs.append(QVariantMap{{QStringLiteral("portId"), port.id},
                {QStringLiteral("label"), port.label}, {QStringLiteral("type"), port.type}});
        for (const Port &port : node.outputs)
            outputs.append(QVariantMap{{QStringLiteral("portId"), port.id},
                {QStringLiteral("label"), port.label}, {QStringLiteral("type"), port.type}});
        result.append(QVariantMap{{QStringLiteral("nodeId"), node.id},
            {QStringLiteral("title"), node.title}, {QStringLiteral("stencilId"), node.stencilId},
            {QStringLiteral("x"), node.x}, {QStringLiteral("y"), node.y},
            {QStringLiteral("inFlow"), incoming.value(node.id)},
            {QStringLiteral("outFlow"), outgoing.value(node.id)},
            {QStringLiteral("inputs"), inputs}, {QStringLiteral("outputs"), outputs}});
    }
    return result;
}

QVariantList DiagramDocument::edges() const
{
    QVariantList result;
    result.reserve(m_state.edges.size());
    for (const Edge &edge : m_state.edges)
        result.append(QVariantMap{{QStringLiteral("edgeId"), edge.id},
            {QStringLiteral("fromNodeId"), edge.fromNode},
            {QStringLiteral("fromPortId"), edge.fromPort},
            {QStringLiteral("toNodeId"), edge.toNode},
            {QStringLiteral("toPortId"), edge.toPort},
            {QStringLiteral("flow"), edge.flow}});
    return result;
}

void DiagramDocument::rememberEdit()
{
    m_undo.append(m_state);
    if (m_undo.size() > 128) m_undo.removeFirst();
    m_redo.clear();
    ++m_revision;
}

QString DiagramDocument::addNode(const QString &title, double x, double y, const QString &stencilId)
{
    if (title.trimmed().isEmpty() || title.size() > 128 || stencilId.isEmpty()
        || stencilId.size() > 64 || !validCoordinate(x) || !validCoordinate(y)
        || m_state.nodes.size() >= kMaxNodes) {
        setError(QStringLiteral("Invalid diagram node"));
        return {};
    }
    rememberEdit();
    const QString id = freshId();
    m_state.nodes.append({id, title, stencilId, x, y, {}, {}});
    setError({});
    emit changed();
    return id;
}

QString DiagramDocument::addPort(const QString &nodeId, const QString &label,
                                 const QString &direction, const QString &type)
{
    const int index = nodeIndex(nodeId);
    if (index < 0 || (direction != QLatin1String("input") && direction != QLatin1String("output"))
        || label.trimmed().isEmpty() || label.size() > 64 || type.isEmpty() || type.size() > 64) {
        setError(QStringLiteral("Invalid diagram port"));
        return {};
    }
    const bool output = direction == QLatin1String("output");
    const int count = output ? m_state.nodes.at(index).outputs.size()
                             : m_state.nodes.at(index).inputs.size();
    if (count >= 16) { setError(QStringLiteral("Too many diagram ports")); return {}; }
    rememberEdit();
    auto &ports = output ? m_state.nodes[index].outputs : m_state.nodes[index].inputs;
    const QString id = freshId();
    ports.append({id, label, type});
    setError({});
    emit changed();
    return id;
}

QString DiagramDocument::connectPorts(const QString &fromNodeId, const QString &fromPortId,
                                      const QString &toNodeId, const QString &toPortId)
{
    const QString sourceType = portType(fromNodeId, fromPortId, true);
    const QString targetType = portType(toNodeId, toPortId, false);
    if (sourceType.isEmpty() || targetType.isEmpty()
        || (sourceType != targetType && sourceType != QLatin1String("any")
            && targetType != QLatin1String("any"))
        || m_state.edges.size() >= kMaxEdges) {
        setError(QStringLiteral("Invalid diagram connection"));
        return {};
    }
    for (const Edge &edge : m_state.edges) {
        if (edge.fromNode == fromNodeId && edge.fromPort == fromPortId
            && edge.toNode == toNodeId && edge.toPort == toPortId) {
            setError(QStringLiteral("Connection already exists"));
            return {};
        }
    }
    rememberEdit();
    const QString id = freshId();
    m_state.edges.append({id, fromNodeId, fromPortId, toNodeId, toPortId});
    setError({});
    emit changed();
    return id;
}

bool DiagramDocument::disconnectEdge(const QString &edgeId)
{
    const int index = edgeIndex(edgeId);
    if (index < 0) { setError(QStringLiteral("Unknown diagram edge")); return false; }
    rememberEdit();
    m_state.edges.removeAt(index);
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::removeNode(const QString &nodeId)
{
    const int index = nodeIndex(nodeId);
    if (index < 0) { setError(QStringLiteral("Unknown diagram node")); return false; }
    rememberEdit();
    m_state.nodes.removeAt(index);
    m_state.edges.removeIf([&](const Edge &edge) {
        return edge.fromNode == nodeId || edge.toNode == nodeId;
    });
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::removeNodes(const QStringList &nodeIds)
{
    const QSet<QString> unique(nodeIds.cbegin(), nodeIds.cend());
    if (unique.isEmpty()) { setError(QStringLiteral("Unknown diagram node")); return false; }
    for (const QString &id : unique)
        if (nodeIndex(id) < 0) { setError(QStringLiteral("Unknown diagram node")); return false; }
    rememberEdit();
    m_state.nodes.removeIf([&](const Node &node) { return unique.contains(node.id); });
    m_state.edges.removeIf([&](const Edge &edge) {
        return unique.contains(edge.fromNode) || unique.contains(edge.toNode);
    });
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::moveNode(const QString &nodeId, double x, double y)
{
    const int index = nodeIndex(nodeId);
    if (index < 0 || !validCoordinate(x) || !validCoordinate(y)) {
        setError(QStringLiteral("Invalid diagram move")); return false;
    }
    if (m_state.nodes.at(index).x == x && m_state.nodes.at(index).y == y) return true;
    rememberEdit();
    m_state.nodes[index].x = x;
    m_state.nodes[index].y = y;
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::moveNodes(const QStringList &nodeIds, double dx, double dy)
{
    const QSet<QString> unique(nodeIds.cbegin(), nodeIds.cend());
    if (unique.isEmpty() || !std::isfinite(dx) || !std::isfinite(dy)) {
        setError(QStringLiteral("Invalid diagram group move")); return false;
    }
    for (const QString &id : unique) {
        const int index = nodeIndex(id);
        if (index < 0 || !validCoordinate(m_state.nodes.at(index).x + dx)
            || !validCoordinate(m_state.nodes.at(index).y + dy)) {
            setError(QStringLiteral("Invalid diagram group move")); return false;
        }
    }
    if (dx == 0 && dy == 0) return true;
    rememberEdit();
    for (Node &node : m_state.nodes)
        if (unique.contains(node.id)) { node.x += dx; node.y += dy; }
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::setNodeTitle(const QString &nodeId, const QString &title)
{
    const int index = nodeIndex(nodeId);
    if (index < 0 || title.trimmed().isEmpty() || title.size() > 128) {
        setError(QStringLiteral("Invalid diagram title")); return false;
    }
    if (m_state.nodes.at(index).title == title) return true;
    rememberEdit();
    m_state.nodes[index].title = title;
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::setEdgeFlow(const QString &edgeId, double flow)
{
    const int index = edgeIndex(edgeId);
    if (index < 0 || !std::isfinite(flow) || flow < 0 || flow > 1000000000.0) {
        setError(QStringLiteral("Invalid diagram flow")); return false;
    }
    if (m_state.edges.at(index).flow == flow) return true;
    rememberEdit();
    m_state.edges[index].flow = flow;
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::undo()
{
    if (m_undo.isEmpty()) return false;
    m_redo.append(m_state);
    m_state = m_undo.takeLast();
    ++m_revision;
    setError({});
    emit changed();
    return true;
}

bool DiagramDocument::redo()
{
    if (m_redo.isEmpty()) return false;
    m_undo.append(m_state);
    m_state = m_redo.takeLast();
    ++m_revision;
    setError({});
    emit changed();
    return true;
}

} // namespace QindaTK
