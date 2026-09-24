// SPDX-License-Identifier: LGPL-3.0-or-later
#include "node_graph_model.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {
constexpr qint64 kMaxGraphBytes = 4 * 1024 * 1024;
}

bool NodeGraphModel::saveToFile(const QString &path)
{
    if (path.trimmed().isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        setError(QStringLiteral("Graph save path is unavailable")); return false;
    }
    QJsonArray nodesJson;
    for (const Node &node : m_nodes) {
        nodesJson.append(QJsonObject{{QStringLiteral("nodeId"), node.id},
            {QStringLiteral("type"), node.type}, {QStringLiteral("title"), node.title},
            {QStringLiteral("x"), node.x}, {QStringLiteral("y"), node.y},
            {QStringLiteral("parameters"), QJsonObject::fromVariantMap(node.parameters)}});
    }
    QJsonArray edgesJson;
    for (const Edge &edge : m_edges) {
        edgesJson.append(QJsonObject{{QStringLiteral("edgeId"), edge.id},
            {QStringLiteral("fromNodeId"), edge.fromNode},
            {QStringLiteral("fromPortId"), edge.fromPort},
            {QStringLiteral("toNodeId"), edge.toNode},
            {QStringLiteral("toPortId"), edge.toPort}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("qindatk.node-graph")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("revision"), QString::number(m_revision)},
        {QStringLiteral("nodes"), nodesJson}, {QStringLiteral("edges"), edgesJson}})
        .toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaxGraphBytes) {
        setError(QStringLiteral("Graph exceeds the 4 MiB file bound")); return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Graph save failed: %1").arg(file.errorString())); return false;
    }
    setError({});
    return true;
}

bool NodeGraphModel::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > kMaxGraphBytes) {
        setError(QStringLiteral("Graph file is missing, empty or oversized")); return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(QStringLiteral("Graph JSON is invalid")); return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qindatk.node-graph")
        || root.value(QStringLiteral("version")).toInt() != 1
        || !root.value(QStringLiteral("nodes")).isArray()
        || !root.value(QStringLiteral("edges")).isArray()) {
        setError(QStringLiteral("Unsupported graph format")); return false;
    }
    bool revisionOk = false;
    const quint64 revision = root.value(QStringLiteral("revision")).toString()
        .toULongLong(&revisionOk);
    const QJsonArray nodeRecords = root.value(QStringLiteral("nodes")).toArray();
    const QJsonArray edgeRecords = root.value(QStringLiteral("edges")).toArray();
    if (!revisionOk || nodeRecords.size() > 1000 || edgeRecords.size() > 2000) {
        setError(QStringLiteral("Graph revision or size is invalid")); return false;
    }
    QVector<Node> nodes;
    QVector<Edge> edges;
    QSet<QString> ids, edgeIds, occupiedInputs;
    for (const QJsonValue &value : nodeRecords) {
        if (!value.isObject()) { setError(QStringLiteral("Graph node is not an object")); return false; }
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("nodeId")).toString();
        const QString type = item.value(QStringLiteral("type")).toString();
        const QString title = item.value(QStringLiteral("title")).toString();
        const double x = item.value(QStringLiteral("x")).toDouble(qQNaN());
        const double y = item.value(QStringLiteral("y")).toDouble(qQNaN());
        const QJsonValue parameters = item.value(QStringLiteral("parameters"));
        if (id.isEmpty() || id.size() > 128 || ids.contains(id) || !validType(type)
            || title.size() > 128 || !std::isfinite(x) || !std::isfinite(y)
            || std::abs(x) > 1000000 || std::abs(y) > 1000000 || !parameters.isObject()
            || !validParameters(type, parameters.toObject().toVariantMap())) {
            setError(QStringLiteral("Invalid node record or duplicate node ID")); return false;
        }
        ids.insert(id);
        nodes.append({id, type, title, x, y, parameters.toObject().toVariantMap()});
    }
    const auto typeOf = [&](const QString &id) -> QString {
        for (const Node &node : nodes) if (node.id == id) return node.type;
        return {};
    };
    for (const QJsonValue &value : edgeRecords) {
        if (!value.isObject()) { setError(QStringLiteral("Graph edge is not an object")); return false; }
        const QJsonObject item = value.toObject();
        const Edge edge{item.value(QStringLiteral("edgeId")).toString(),
            item.value(QStringLiteral("fromNodeId")).toString(),
            item.value(QStringLiteral("fromPortId")).toString(),
            item.value(QStringLiteral("toNodeId")).toString(),
            item.value(QStringLiteral("toPortId")).toString()};
        const QString inputKey = edge.toNode + QLatin1Char('/') + edge.toPort;
        if (edge.id.isEmpty() || edge.id.size() > 128 || edgeIds.contains(edge.id)
            || !ids.contains(edge.fromNode) || !ids.contains(edge.toNode)
            || edge.fromNode == edge.toNode || !hasPort(typeOf(edge.fromNode), edge.fromPort, false)
            || !hasPort(typeOf(edge.toNode), edge.toPort, true)
            || occupiedInputs.contains(inputKey)) {
            setError(QStringLiteral("Invalid edge record, port or duplicate input")); return false;
        }
        edgeIds.insert(edge.id);
        occupiedInputs.insert(inputKey);
        edges.append(edge);
    }
    // Validate topology while still detached from the live graph.
    for (const Edge &edge : edges) {
        QSet<QString> seen{edge.toNode};
        QStringList frontier{edge.toNode};
        for (qsizetype i = 0; i < frontier.size(); ++i) {
            for (const Edge &next : edges) {
                if (next.fromNode != frontier.at(i) || seen.contains(next.toNode)) continue;
                seen.insert(next.toNode); frontier.append(next.toNode);
            }
        }
        if (seen.contains(edge.fromNode)) {
            setError(QStringLiteral("Graph contains a cycle")); return false;
        }
    }
    m_nodes = std::move(nodes);
    m_edges = std::move(edges);
    m_revision = revision;
    m_sources.clear();
    m_sourceBytes = 0;
    m_undo.clear(); m_redo.clear();
    invalidateAll();
    m_frame = {}; m_imageDataUrl.clear();
    setError({});
    emit changed();
    emit frameChanged();
    return true;
}

} // namespace QindaTK
