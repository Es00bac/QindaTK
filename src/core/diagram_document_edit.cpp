// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QUuid>
#include <algorithm>
#include <cmath>
#include <utility>

namespace QindaTK {

QVariantList DiagramDocument::groups() const
{
    QVariantList result;
    for (const Group &group : m_state.groups)
        result.append(QVariantMap{{QStringLiteral("groupId"), group.id},
            {QStringLiteral("title"), group.title}, {QStringLiteral("nodeIds"), group.nodeIds}});
    return result;
}

// Unique IDs in first-seen order; false when one is unknown or too few remain.
bool DiagramDocument::uniqueExistingNodes(const QStringList &nodeIds, int minimum,
                                          QStringList *unique) const
{
    unique->clear();
    for (const QString &id : nodeIds) {
        if (nodeIndex(id) < 0) return false;
        if (!unique->contains(id)) unique->append(id);
    }
    return unique->size() >= minimum;
}

void DiagramDocument::dropFromGroups(const QSet<QString> &nodeIds)
{
    for (Group &group : m_state.groups)
        group.nodeIds.removeIf([&](const QString &id) { return nodeIds.contains(id); });
    m_state.groups.removeIf([](const Group &group) { return group.nodeIds.isEmpty(); });
}

QString DiagramDocument::addGroup(const QStringList &nodeIds, const QString &title)
{
    QStringList members;
    if (!uniqueExistingNodes(nodeIds, 1, &members) || title.trimmed().isEmpty()
        || title.size() > 128 || m_state.groups.size() >= 500) {
        setError(QStringLiteral("Invalid diagram group")); return {};
    }
    for (const Group &group : m_state.groups)
        for (const QString &id : members)
            if (group.nodeIds.contains(id)) {
                setError(QStringLiteral("Card already belongs to group %1").arg(group.title));
                return {};
            }
    rememberEdit();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_state.groups.append({id, title, members});
    setError({}); emit changed();
    return id;
}

// Ungrouping keeps the cards; only the grouping relation is removed.
bool DiagramDocument::removeGroup(const QString &groupId)
{
    for (int i = 0; i < m_state.groups.size(); ++i) {
        if (m_state.groups.at(i).id != groupId) continue;
        rememberEdit();
        m_state.groups.removeAt(i);
        setError({}); emit changed();
        return true;
    }
    setError(QStringLiteral("Unknown diagram group"));
    return false;
}

// AGENT-NOTE (docs/diagram.md): every card has the same size (the view's
// routeMetrics), so aligning an edge or centre is aligning positions:
// left/top take the minimum, right/bottom the maximum, centres the midpoint.
bool DiagramDocument::alignNodes(const QStringList &nodeIds, const QString &edge)
{
    static const QStringList edges{QStringLiteral("left"), QStringLiteral("centerX"),
        QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("centerY"),
        QStringLiteral("bottom")};
    QStringList members;
    const int mode = int(edges.indexOf(edge));
    if (mode < 0 || !uniqueExistingNodes(nodeIds, 2, &members)) {
        setError(QStringLiteral("Align needs two cards and a known edge")); return false;
    }
    const bool vertical = mode >= 3;
    double low = INFINITY, high = -INFINITY;
    for (const QString &id : members) {
        const Node &node = m_state.nodes.at(nodeIndex(id));
        const double value = vertical ? node.y : node.x;
        low = std::min(low, value); high = std::max(high, value);
    }
    const double target = mode % 3 == 0 ? low : mode % 3 == 2 ? high : (low + high) / 2;
    bool moves = false;
    for (const QString &id : members) {
        const Node &node = m_state.nodes.at(nodeIndex(id));
        moves = moves || (vertical ? node.y : node.x) != target;
    }
    setError({});
    if (!moves) return true;
    rememberEdit();
    for (const QString &id : members) {
        Node &node = m_state.nodes[nodeIndex(id)];
        (vertical ? node.y : node.x) = target;
    }
    emit changed();
    return true;
}

// The outermost cards stay; the others get equal steps between them in
// position order (saved order breaks ties).
bool DiagramDocument::distributeNodes(const QStringList &nodeIds, const QString &axis)
{
    const bool vertical = axis == QLatin1String("vertical");
    QStringList members;
    if ((!vertical && axis != QLatin1String("horizontal"))
        || !uniqueExistingNodes(nodeIds, 3, &members)) {
        setError(QStringLiteral("Distribute needs three cards and an axis")); return false;
    }
    auto position = [&](int index) {
        const Node &node = m_state.nodes.at(index);
        return vertical ? node.y : node.x;
    };
    QVector<int> order;
    for (const QString &id : members) order.append(nodeIndex(id));
    std::sort(order.begin(), order.end());
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return position(a) < position(b); });
    const double first = position(order.first());
    const double step = (position(order.last()) - first) / (order.size() - 1);
    bool moves = false;
    for (int i = 1; i + 1 < order.size(); ++i)
        moves = moves || position(order.at(i)) != first + step * i;
    setError({});
    if (!moves) return true;
    rememberEdit();
    for (int i = 1; i + 1 < order.size(); ++i) {
        Node &node = m_state.nodes[order.at(i)];
        (vertical ? node.y : node.x) = first + step * i;
    }
    emit changed();
    return true;
}

// AGENT-NOTE (D-283): mirrors CadDocument's transactions. Nested begin/commit
// pairs join the outer one; undo/redo are refused while one is open.
bool DiagramDocument::beginTransaction()
{
    if (m_transactionDepth == 0) {
        m_transactionRecorded = false;
        m_transactionBase = m_state;
        m_transactionUndo = m_undo;
        m_transactionRedo = m_redo;
    }
    ++m_transactionDepth;
    return true;
}

bool DiagramDocument::commitTransaction()
{
    if (m_transactionDepth == 0) return false;
    if (--m_transactionDepth == 0) {
        m_transactionRecorded = false;
        m_transactionBase = {};
        m_transactionUndo.clear();
        m_transactionRedo.clear();
    }
    return true;
}

// Keeps lastError so the view can say why the gesture was abandoned.
bool DiagramDocument::cancelTransaction()
{
    if (m_transactionDepth == 0) return false;
    m_transactionDepth = 0;
    const bool recorded = m_transactionRecorded;
    m_transactionRecorded = false;
    if (recorded) {
        m_state = std::move(m_transactionBase);
        m_undo = std::move(m_transactionUndo);
        m_redo = std::move(m_transactionRedo);
        ++m_revision;
    }
    m_transactionBase = {};
    m_transactionUndo.clear();
    m_transactionRedo.clear();
    if (recorded) emit changed();
    return true;
}

} // namespace QindaTK
