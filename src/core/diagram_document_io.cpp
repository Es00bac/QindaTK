// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <utility>

namespace QindaTK {

bool DiagramDocument::saveToFile(const QString &path)
{
    if (path.isEmpty()) { setError(QStringLiteral("Missing diagram path")); return false; }
    QJsonArray nodeArray, edgeArray;
    for (const Node &node : m_state.nodes) {
        QJsonArray inputs, outputs;
        for (const Port &port : node.inputs)
            inputs.append(QJsonObject{{QStringLiteral("id"), port.id},
                {QStringLiteral("label"), port.label}, {QStringLiteral("type"), port.type}});
        for (const Port &port : node.outputs)
            outputs.append(QJsonObject{{QStringLiteral("id"), port.id},
                {QStringLiteral("label"), port.label}, {QStringLiteral("type"), port.type}});
        nodeArray.append(QJsonObject{{QStringLiteral("id"), node.id},
            {QStringLiteral("title"), node.title}, {QStringLiteral("stencilId"), node.stencilId},
            {QStringLiteral("x"), node.x}, {QStringLiteral("y"), node.y},
            {QStringLiteral("inputs"), inputs}, {QStringLiteral("outputs"), outputs}});
    }
    for (const Edge &edge : m_state.edges)
        edgeArray.append(QJsonObject{{QStringLiteral("id"), edge.id},
            {QStringLiteral("fromNode"), edge.fromNode}, {QStringLiteral("fromPort"), edge.fromPort},
            {QStringLiteral("toNode"), edge.toNode}, {QStringLiteral("toPort"), edge.toPort},
            {QStringLiteral("flow"), edge.flow}});
    QJsonObject root{{QStringLiteral("format"), QStringLiteral("qindatk.diagram/1")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("nodes"), nodeArray}, {QStringLiteral("edges"), edgeArray}};
    // Optional: files without groups keep their exact version-1 bytes.
    if (!m_state.groups.isEmpty()) {
        QJsonArray groupArray;
        for (const Group &group : m_state.groups)
            groupArray.append(QJsonObject{{QStringLiteral("id"), group.id},
                {QStringLiteral("title"), group.title},
                {QStringLiteral("nodes"), QJsonArray::fromStringList(group.nodeIds)}});
        root.insert(QStringLiteral("groups"), groupArray);
    }
    const QByteArray bytes = QJsonDocument(root).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Could not save diagram")); return false;
    }
    setError({});
    return true;
}

bool DiagramDocument::decode(const QByteArray &bytes, State *result)
{
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) return false;
    const QJsonObject root = parsed.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qindatk.diagram/1")
        || root.value(QStringLiteral("version")).toInt() != 1
        || !root.value(QStringLiteral("nodes")).isArray()
        || !root.value(QStringLiteral("edges")).isArray()) return false;
    const QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
    const QJsonArray edges = root.value(QStringLiteral("edges")).toArray();
    if (nodes.size() > 2000 || edges.size() > 4000) return false;

    QSet<QString> nodeIds, portIds, edgeIds;
    QHash<QString, QSet<QString>> inputsByNode, outputsByNode;
    QHash<QString, QString> typesByPort;
    auto readPorts = [&](const QJsonValue &value, QVector<Port> *out) {
        if (!value.isArray() || value.toArray().size() > 16) return false;
        for (const QJsonValue &entry : value.toArray()) {
            if (!entry.isObject()) return false;
            const QJsonObject p = entry.toObject();
            const QString id = p.value(QStringLiteral("id")).toString();
            const QString label = p.value(QStringLiteral("label")).toString();
            const QString type = p.value(QStringLiteral("type")).toString();
            if (id.isEmpty() || portIds.contains(id) || label.trimmed().isEmpty()
                || label.size() > 64 || type.isEmpty() || type.size() > 64) return false;
            portIds.insert(id);
            typesByPort.insert(id, type);
            out->append({id, label, type});
        }
        return true;
    };
    for (const QJsonValue &entry : nodes) {
        if (!entry.isObject()) return false;
        const QJsonObject n = entry.toObject();
        Node node;
        node.id = n.value(QStringLiteral("id")).toString();
        node.title = n.value(QStringLiteral("title")).toString();
        node.stencilId = n.value(QStringLiteral("stencilId")).toString();
        const QJsonValue xv = n.value(QStringLiteral("x"));
        const QJsonValue yv = n.value(QStringLiteral("y"));
        if (node.id.isEmpty() || nodeIds.contains(node.id) || node.title.trimmed().isEmpty()
            || node.title.size() > 128 || node.stencilId.isEmpty() || node.stencilId.size() > 64
            || !xv.isDouble() || !yv.isDouble()) return false;
        node.x = xv.toDouble();
        node.y = yv.toDouble();
        if (!std::isfinite(node.x) || !std::isfinite(node.y)
            || std::abs(node.x) > 1000000 || std::abs(node.y) > 1000000
            || !readPorts(n.value(QStringLiteral("inputs")), &node.inputs)
            || !readPorts(n.value(QStringLiteral("outputs")), &node.outputs)) return false;
        nodeIds.insert(node.id);
        for (const Port &port : node.inputs) inputsByNode[node.id].insert(port.id);
        for (const Port &port : node.outputs) outputsByNode[node.id].insert(port.id);
        result->nodes.append(std::move(node));
    }
    QSet<QString> connections;
    for (const QJsonValue &entry : edges) {
        if (!entry.isObject()) return false;
        const QJsonObject e = entry.toObject();
        Edge edge{e.value(QStringLiteral("id")).toString(),
            e.value(QStringLiteral("fromNode")).toString(), e.value(QStringLiteral("fromPort")).toString(),
            e.value(QStringLiteral("toNode")).toString(), e.value(QStringLiteral("toPort")).toString()};
        const QJsonValue flowValue = e.value(QStringLiteral("flow"));
        if (!flowValue.isUndefined()) {
            if (!flowValue.isDouble() || !std::isfinite(flowValue.toDouble())
                || flowValue.toDouble() < 0 || flowValue.toDouble() > 1000000000.0) return false;
            edge.flow = flowValue.toDouble();
        }
        const QString key = edge.fromNode + QChar(0) + edge.fromPort + QChar(0)
            + edge.toNode + QChar(0) + edge.toPort;
        if (edge.id.isEmpty() || edgeIds.contains(edge.id) || connections.contains(key)
            || !outputsByNode.value(edge.fromNode).contains(edge.fromPort)
            || !inputsByNode.value(edge.toNode).contains(edge.toPort)
            || (typesByPort.value(edge.fromPort) != typesByPort.value(edge.toPort)
                && typesByPort.value(edge.fromPort) != QLatin1String("any")
                && typesByPort.value(edge.toPort) != QLatin1String("any"))) return false;
        edgeIds.insert(edge.id);
        connections.insert(key);
        result->edges.append(std::move(edge));
    }
    const QJsonValue groups = root.value(QStringLiteral("groups"));
    if (groups.isUndefined()) return true;
    if (!groups.isArray() || groups.toArray().size() > 500) return false;
    QSet<QString> groupIds, grouped;
    for (const QJsonValue &entry : groups.toArray()) {
        if (!entry.isObject()) return false;
        const QJsonObject g = entry.toObject();
        Group group{g.value(QStringLiteral("id")).toString(),
                    g.value(QStringLiteral("title")).toString(), {}};
        const QJsonValue members = g.value(QStringLiteral("nodes"));
        if (group.id.isEmpty() || groupIds.contains(group.id) || group.title.trimmed().isEmpty()
            || group.title.size() > 128 || !members.isArray() || members.toArray().isEmpty())
            return false;
        for (const QJsonValue &member : members.toArray()) {
            const QString id = member.toString();
            if (!nodeIds.contains(id) || grouped.contains(id)) return false;
            grouped.insert(id);
            group.nodeIds.append(id);
        }
        groupIds.insert(group.id);
        result->groups.append(std::move(group));
    }
    return true;
}

bool DiagramDocument::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        setError(QStringLiteral("Could not open diagram")); return false;
    }
    State decoded;
    if (!decode(file.readAll(), &decoded)) {
        setError(QStringLiteral("Invalid diagram file")); return false;
    }
    m_state = std::move(decoded);
    m_undo.clear();
    m_redo.clear();
    m_transactionDepth = 0;
    m_transactionRecorded = false;
    ++m_revision;
    setError({});
    emit changed();
    return true;
}

} // namespace QindaTK
