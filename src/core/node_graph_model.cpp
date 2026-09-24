// SPDX-License-Identifier: LGPL-3.0-or-later
#include "node_graph_model.h"

#include <QUuid>
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {
constexpr int kMaxNodes = 1000;
constexpr int kMaxEdges = 2000;
}

NodeGraphModel::NodeGraphModel(QObject *parent) : QObject(parent) {}

void NodeGraphModel::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}

int NodeGraphModel::nodeIndex(const QString &id) const
{
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes.at(i).id == id) return i;
    return -1;
}

int NodeGraphModel::edgeIndex(const QString &id) const
{
    for (int i = 0; i < m_edges.size(); ++i)
        if (m_edges.at(i).id == id) return i;
    return -1;
}

QVariantList NodeGraphModel::nodes() const
{
    QVariantList result;
    result.reserve(m_nodes.size());
    for (const Node &node : m_nodes) {
        result.append(QVariantMap{{QStringLiteral("nodeId"), node.id},
            {QStringLiteral("type"), node.type}, {QStringLiteral("title"), node.title},
            {QStringLiteral("x"), node.x}, {QStringLiteral("y"), node.y},
            {QStringLiteral("parameters"), node.parameters},
            {QStringLiteral("inputs"), portList(node.type, true)},
            {QStringLiteral("outputs"), portList(node.type, false)}});
    }
    return result;
}

QVariantList NodeGraphModel::edges() const
{
    QVariantList result;
    result.reserve(m_edges.size());
    for (const Edge &edge : m_edges) {
        result.append(QVariantMap{{QStringLiteral("edgeId"), edge.id},
            {QStringLiteral("fromNodeId"), edge.fromNode},
            {QStringLiteral("fromPortId"), edge.fromPort},
            {QStringLiteral("toNodeId"), edge.toNode},
            {QStringLiteral("toPortId"), edge.toPort}});
    }
    return result;
}

void NodeGraphModel::rememberEdit()
{
    // Inside beginEdit/commitEdit the base snapshot is already on the stack.
    if (!m_editing) {
        m_undo.append(state());
        if (m_undo.size() > 128) m_undo.removeFirst();
    }
    m_redo.clear();
    ++m_revision;
}

void NodeGraphModel::invalidateAll()
{
    m_cache.clear();
    m_cacheOrder.clear();
    m_cacheBytes = 0;
}

void NodeGraphModel::invalidateFrom(const QString &nodeId)
{
    QSet<QString> dirty{nodeId};
    QStringList frontier{nodeId};
    for (qsizetype i = 0; i < frontier.size(); ++i) {
        for (const Edge &edge : m_edges) {
            if (edge.fromNode != frontier.at(i) || dirty.contains(edge.toNode)) continue;
            dirty.insert(edge.toNode);
            frontier.append(edge.toNode);
        }
    }
    for (const QString &id : dirty) {
        const auto it = m_cache.constFind(id);
        if (it != m_cache.cend()) m_cacheBytes -= it.value().sizeInBytes();
        m_cache.remove(id);
        m_cacheOrder.removeAll(id);
    }
}

QString NodeGraphModel::addNode(const QString &type, const QString &title,
                                double x, double y, const QVariantMap &parameters)
{
    if (!validType(type) || !validParameters(type, parameters)
        || !std::isfinite(x) || !std::isfinite(y)
        || std::abs(x) > 1000000 || std::abs(y) > 1000000
        || title.size() > 128 || m_nodes.size() >= kMaxNodes) {
        setError(QStringLiteral("Invalid or oversized node"));
        return {};
    }
    rememberEdit();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_nodes.append({id, type, title.isEmpty() ? type : title, x, y, parameters});
    setError({});
    emit changed();
    return id;
}

bool NodeGraphModel::removeNode(const QString &nodeId)
{
    const int index = nodeIndex(nodeId);
    if (index < 0) { setError(QStringLiteral("Unknown node")); return false; }
    rememberEdit();
    m_nodes.removeAt(index);
    const auto source = m_sources.constFind(nodeId);
    if (source != m_sources.cend()) m_sourceBytes -= source.value().sizeInBytes();
    m_sources.remove(nodeId);
    m_edges.removeIf([&](const Edge &edge) {
        return edge.fromNode == nodeId || edge.toNode == nodeId;
    });
    for (Group &group : m_groups) group.nodeIds.removeAll(nodeId);
    m_groups.removeIf([](const Group &group) { return group.nodeIds.isEmpty(); });
    invalidateAll();
    setError({});
    emit changed();
    return true;
}

bool NodeGraphModel::moveNode(const QString &nodeId, double x, double y)
{
    const int index = nodeIndex(nodeId);
    if (index < 0 || !std::isfinite(x) || !std::isfinite(y)
        || std::abs(x) > 1000000 || std::abs(y) > 1000000) {
        setError(QStringLiteral("Invalid node move")); return false;
    }
    if (m_nodes.at(index).x == x && m_nodes.at(index).y == y) return true;
    rememberEdit();
    m_nodes[index].x = x; m_nodes[index].y = y;
    setError({});
    emit changed();
    return true;
}

bool NodeGraphModel::setParameters(const QString &nodeId, const QVariantMap &parameters)
{
    const int index = nodeIndex(nodeId);
    if (index < 0 || !validParameters(m_nodes.at(index).type, parameters)) {
        setError(QStringLiteral("Invalid node parameters")); return false;
    }
    if (m_nodes.at(index).parameters == parameters) return true;
    rememberEdit();
    if (m_nodes.at(index).type == QLatin1String("source")
        && m_nodes.at(index).parameters.value(QStringLiteral("sourceId"))
           != parameters.value(QStringLiteral("sourceId"))) {
        const auto source = m_sources.constFind(nodeId);
        if (source != m_sources.cend()) m_sourceBytes -= source.value().sizeInBytes();
        m_sources.remove(nodeId);
    }
    m_nodes[index].parameters = parameters;
    invalidateFrom(nodeId);
    setError({});
    emit changed();
    return true;
}

bool NodeGraphModel::setParameter(const QString &nodeId, const QString &parameterId,
                                  const QVariant &value)
{
    const int index = nodeIndex(nodeId);
    if (index < 0) { setError(QStringLiteral("Unknown node")); return false; }
    QVariantMap parameters = m_nodes.at(index).parameters;
    parameters.insert(parameterId, value);
    return setParameters(nodeId, parameters);
}

bool NodeGraphModel::reaches(const QString &start, const QString &target) const
{
    QSet<QString> seen{start};
    QStringList frontier{start};
    for (qsizetype i = 0; i < frontier.size(); ++i) {
        if (frontier.at(i) == target) return true;
        for (const Edge &edge : m_edges) {
            if (edge.fromNode != frontier.at(i) || seen.contains(edge.toNode)) continue;
            seen.insert(edge.toNode);
            frontier.append(edge.toNode);
        }
    }
    return false;
}

QString NodeGraphModel::connectPorts(const QString &fromNodeId, const QString &fromPortId,
                                     const QString &toNodeId, const QString &toPortId)
{
    const int from = nodeIndex(fromNodeId), to = nodeIndex(toNodeId);
    const QString fromType = from < 0 ? QString() : portType(m_nodes.at(from).type, fromPortId, false);
    const QString toType = to < 0 ? QString() : portType(m_nodes.at(to).type, toPortId, true);
    if (fromType.isEmpty() || toType.isEmpty()) {
        setError(QStringLiteral("Unknown node or port")); return {};
    }
    if (fromType != toType) {
        setError(QStringLiteral("Incompatible ports: %1 output cannot connect to %2 input")
                     .arg(fromType, toType));
        return {};
    }
    if (fromNodeId == toNodeId || reaches(toNodeId, fromNodeId)) {
        setError(QStringLiteral("Connection would create a cycle")); return {};
    }
    if (m_edges.size() >= kMaxEdges) { setError(QStringLiteral("Graph edge limit reached")); return {}; }
    for (const Edge &edge : m_edges)
        if (edge.toNode == toNodeId && edge.toPort == toPortId) {
            setError(QStringLiteral("Input port already connected")); return {};
        }
    rememberEdit();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_edges.append({id, fromNodeId, fromPortId, toNodeId, toPortId});
    invalidateFrom(toNodeId);
    setError({});
    emit changed();
    return id;
}

bool NodeGraphModel::disconnectEdge(const QString &edgeId)
{
    const int index = edgeIndex(edgeId);
    if (index < 0) { setError(QStringLiteral("Unknown edge")); return false; }
    const QString to = m_edges.at(index).toNode;
    rememberEdit();
    m_edges.removeAt(index);
    invalidateFrom(to);
    setError({});
    emit changed();
    return true;
}

void NodeGraphModel::restore(GraphState graph)
{
    m_nodes = std::move(graph.nodes);
    m_edges = std::move(graph.edges);
    m_groups = std::move(graph.groups);
    ++m_revision;
    invalidateAll();
    setError({});
    emit changed();
}

bool NodeGraphModel::undo()
{
    if (m_editing) { setError(QStringLiteral("Commit or cancel the open edit first")); return false; }
    if (m_undo.isEmpty()) return false;
    m_redo.append(state());
    restore(m_undo.takeLast());
    return true;
}

bool NodeGraphModel::redo()
{
    if (m_editing) { setError(QStringLiteral("Commit or cancel the open edit first")); return false; }
    if (m_redo.isEmpty()) return false;
    m_undo.append(state());
    restore(m_redo.takeLast());
    return true;
}

} // namespace QindaTK
