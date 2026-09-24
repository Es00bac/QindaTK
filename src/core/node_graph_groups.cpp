// SPDX-License-Identifier: LGPL-3.0-or-later
#include "node_graph_model.h"

#include <QUuid>
#include <cmath>

namespace QindaTK {

int NodeGraphModel::groupIndex(const QString &id) const
{
    for (int i = 0; i < m_groups.size(); ++i)
        if (m_groups.at(i).id == id) return i;
    return -1;
}

int NodeGraphModel::groupOf(const QString &nodeId) const
{
    for (int i = 0; i < m_groups.size(); ++i)
        if (m_groups.at(i).nodeIds.contains(nodeId)) return i;
    return -1;
}

QVariantList NodeGraphModel::groups() const
{
    QVariantList result;
    result.reserve(m_groups.size());
    for (const Group &group : m_groups) {
        result.append(QVariantMap{{QStringLiteral("groupId"), group.id},
            {QStringLiteral("title"), group.title},
            {QStringLiteral("nodeIds"), group.nodeIds}});
    }
    return result;
}

QString NodeGraphModel::groupNodes(const QStringList &nodeIds, const QString &title)
{
    QSet<QString> unique;
    for (const QString &id : nodeIds) {
        if (nodeIndex(id) < 0 || unique.contains(id) || groupOf(id) >= 0) {
            setError(QStringLiteral("A group needs distinct nodes that are not already grouped"));
            return {};
        }
        unique.insert(id);
    }
    if (unique.isEmpty() || title.size() > 128) {
        setError(QStringLiteral("Select at least one node and a title of at most 128 characters"));
        return {};
    }
    rememberEdit();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_groups.append({id, title.isEmpty() ? QStringLiteral("Group") : title, nodeIds});
    setError({});
    emit changed();
    return id;
}

bool NodeGraphModel::ungroup(const QString &groupId)
{
    const int index = groupIndex(groupId);
    if (index < 0) { setError(QStringLiteral("Unknown group")); return false; }
    rememberEdit();
    m_groups.removeAt(index);
    setError({});
    emit changed();
    return true;
}

bool NodeGraphModel::moveGroup(const QString &groupId, double dx, double dy)
{
    const int index = groupIndex(groupId);
    bool valid = index >= 0 && std::isfinite(dx) && std::isfinite(dy);
    for (const QString &nodeId : valid ? m_groups.at(index).nodeIds : QStringList()) {
        const Node &node = m_nodes.at(nodeIndex(nodeId));
        valid = valid && std::abs(node.x + dx) <= 1000000 && std::abs(node.y + dy) <= 1000000;
    }
    if (!valid) { setError(QStringLiteral("Invalid group move")); return false; }
    if (dx == 0 && dy == 0) return true;
    rememberEdit();
    for (const QString &nodeId : m_groups.at(index).nodeIds) {
        Node &node = m_nodes[nodeIndex(nodeId)];
        node.x += dx;
        node.y += dy;
    }
    setError({});
    emit changed();
    return true;
}

bool NodeGraphModel::beginEdit()
{
    if (m_editing) { setError(QStringLiteral("An edit is already open")); return false; }
    m_undo.append(state());
    if (m_undo.size() > 128) m_undo.removeFirst();
    m_editRedo = m_redo;
    m_editing = true;
    setError({});
    emit changed();
    return true;
}

bool NodeGraphModel::commitEdit()
{
    if (!m_editing) { setError(QStringLiteral("No edit is open")); return false; }
    m_editing = false;
    // A gesture that changed nothing leaves no undo entry and keeps redo.
    if (m_undo.last() == state()) {
        m_undo.removeLast();
        m_redo = m_editRedo;
    }
    m_editRedo.clear();
    emit changed();
    return true;
}

bool NodeGraphModel::cancelEdit()
{
    if (!m_editing) return false;
    m_editing = false;
    GraphState base = m_undo.takeLast();
    m_redo = m_editRedo;
    m_editRedo.clear();
    if (base == state()) { emit changed(); return true; }
    restore(std::move(base));
    return true;
}

} // namespace QindaTK
