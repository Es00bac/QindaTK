// SPDX-License-Identifier: LGPL-3.0-or-later
#include "node_graph_model.h"

#include <QMetaType>
#include <QColor>
#include <QUuid>
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {
constexpr int kMaxNodes = 1000;
constexpr int kMaxEdges = 2000;

bool finiteNumber(const QVariant &value, double *number)
{
    const int type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::UInt
        && type != QMetaType::LongLong && type != QMetaType::ULongLong
        && type != QMetaType::Double && type != QMetaType::Float) return false;
    bool ok = false;
    *number = value.toDouble(&ok);
    return ok && std::isfinite(*number);
}

QVariantMap port(const QString &id, const QString &label)
{
    return {{QStringLiteral("portId"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("type"), QStringLiteral("image")}};
}
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

bool NodeGraphModel::validType(const QString &type)
{
    return type == QLatin1String("source") || type == QLatin1String("transform")
        || type == QLatin1String("solid") || type == QLatin1String("blend")
        || type == QLatin1String("output");
}

QVariantList NodeGraphModel::portList(const QString &type, bool inputs)
{
    if (inputs) {
        if (type == QLatin1String("transform") || type == QLatin1String("output"))
            return {port(QStringLiteral("image"), QStringLiteral("Image"))};
        if (type == QLatin1String("blend"))
            return {port(QStringLiteral("background"), QStringLiteral("Background")),
                    port(QStringLiteral("foreground"), QStringLiteral("Foreground"))};
        return {};
    }
    return type == QLatin1String("output") ? QVariantList{}
        : QVariantList{port(QStringLiteral("image"), QStringLiteral("Image"))};
}

bool NodeGraphModel::hasPort(const QString &type, const QString &portId, bool input)
{
    for (const QVariant &value : portList(type, input))
        if (value.toMap().value(QStringLiteral("portId")).toString() == portId) return true;
    return false;
}

bool NodeGraphModel::validParameters(const QString &type, const QVariantMap &parameters)
{
    if (parameters.size() > 16) return false;
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        if (type == QLatin1String("source") && it.key() == QLatin1String("sourceId")) {
            if (it.value().metaType().id() != QMetaType::QString
                || it.value().toString().size() > 128) return false;
            continue;
        }
        if (type == QLatin1String("solid") && it.key() == QLatin1String("color")) {
            if (it.value().metaType().id() != QMetaType::QString
                || !QColor(it.value().toString()).isValid()) return false;
            continue;
        }
        double number = 0;
        if (!finiteNumber(it.value(), &number)) return false;
        if (type == QLatin1String("transform")
            && (it.key() == QLatin1String("x") || it.key() == QLatin1String("y"))) {
            if (std::abs(number) > 1000000) return false;
        } else if (type == QLatin1String("transform") && it.key() == QLatin1String("scale")) {
            if (number < 0.001 || number > 1000) return false;
        } else if (type == QLatin1String("blend") && it.key() == QLatin1String("opacity")) {
            if (number < 0 || number > 1) return false;
        } else return false;
    }
    return true;
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
    m_undo.append({m_nodes, m_edges});
    if (m_undo.size() > 128) m_undo.removeFirst();
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
    if (from < 0 || to < 0 || !hasPort(m_nodes.at(from).type, fromPortId, false)
        || !hasPort(m_nodes.at(to).type, toPortId, true) || fromNodeId == toNodeId
        || reaches(toNodeId, fromNodeId) || m_edges.size() >= kMaxEdges) {
        setError(QStringLiteral("Unknown port, cycle or edge limit")); return {};
    }
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

bool NodeGraphModel::undo()
{
    if (m_undo.isEmpty()) return false;
    m_redo.append({m_nodes, m_edges});
    GraphState state = m_undo.takeLast();
    m_nodes = std::move(state.nodes); m_edges = std::move(state.edges);
    ++m_revision;
    invalidateAll();
    setError({});
    emit changed();
    return true;
}

bool NodeGraphModel::redo()
{
    if (m_redo.isEmpty()) return false;
    m_undo.append({m_nodes, m_edges});
    GraphState state = m_redo.takeLast();
    m_nodes = std::move(state.nodes); m_edges = std::move(state.edges);
    ++m_revision;
    invalidateAll();
    setError({});
    emit changed();
    return true;
}

} // namespace QindaTK
