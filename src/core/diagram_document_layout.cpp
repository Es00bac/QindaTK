// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace QindaTK {

// AGENT-NOTE (docs/diagram.md): collapse cycles before assigning columns so
// feedback edges and parallel links never make topological layout ambiguous.
bool DiagramDocument::autoLayout(double columnStep, double rowStep,
                                 double originX, double originY)
{
    if (!std::isfinite(columnStep) || !std::isfinite(rowStep)
        || columnStep <= 0 || rowStep <= 0 || !std::isfinite(originX)
        || !std::isfinite(originY) || m_state.nodes.isEmpty()) {
        setError(QStringLiteral("Invalid diagram layout")); return false;
    }
    const int count = m_state.nodes.size();
    QHash<QString, int> byId;
    for (int i = 0; i < count; ++i) byId.insert(m_state.nodes.at(i).id, i);
    QVector<QVector<int>> links(count);
    for (const Edge &edge : m_state.edges)
        links[byId.value(edge.fromNode)].append(byId.value(edge.toNode));
    for (auto &next : links) {
        std::sort(next.begin(), next.end());
        next.erase(std::unique(next.begin(), next.end()), next.end());
    }

    QVector<int> visit(count, -1), low(count), component(count, -1), stack;
    QVector<bool> onStack(count, false);
    int serial = 0, componentCount = 0;
    std::function<void(int)> visitNode = [&](int node) {
        visit[node] = low[node] = serial++;
        stack.append(node);
        onStack[node] = true;
        for (int next : links.at(node)) {
            if (visit[next] < 0) {
                visitNode(next);
                low[node] = std::min(low[node], low[next]);
            } else if (onStack[next]) {
                low[node] = std::min(low[node], visit[next]);
            }
        }
        if (low[node] != visit[node]) return;
        int member;
        do {
            member = stack.takeLast();
            onStack[member] = false;
            component[member] = componentCount;
        } while (member != node);
        ++componentCount;
    };
    for (int i = 0; i < count; ++i)
        if (visit[i] < 0) visitNode(i);

    QVector<QSet<int>> successors(componentCount);
    QVector<int> indegree(componentCount), depth(componentCount), first(componentCount, count);
    for (int i = 0; i < count; ++i) {
        first[component[i]] = std::min(first[component[i]], i);
        for (int next : links.at(i))
            if (component[i] != component[next]) successors[component[i]].insert(component[next]);
    }
    for (const auto &successorSet : successors)
        for (int next : successorSet) ++indegree[next];
    QSet<int> ready;
    for (int i = 0; i < componentCount; ++i)
        if (!indegree[i]) ready.insert(i);
    while (!ready.isEmpty()) {
        int current = -1;
        for (int candidate : ready)
            if (current < 0 || first[candidate] < first[current]) current = candidate;
        ready.remove(current);
        for (int next : successors.at(current)) {
            depth[next] = std::max(depth[next], depth[current] + 1);
            if (--indegree[next] == 0) ready.insert(next);
        }
    }

    QHash<int, int> rowByDepth;
    QVector<QPointF> positions(count);
    bool changedPosition = false;
    // Component order follows the first persisted node, then nodes within a
    // component follow persisted order. Hash iteration never affects output.
    QVector<int> order(componentCount);
    for (int i = 0; i < componentCount; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return first[a] < first[b]; });
    for (int group : order) {
        for (int i = 0; i < count; ++i) {
            if (component[i] != group) continue;
            const int row = rowByDepth[depth[group]]++;
            const double x = originX + depth[group] * columnStep;
            const double y = originY + row * rowStep;
            if (!std::isfinite(x) || !std::isfinite(y)
                || std::abs(x) > 1000000 || std::abs(y) > 1000000) {
                setError(QStringLiteral("Diagram layout exceeds coordinate range")); return false;
            }
            positions[i] = QPointF(x, y);
            changedPosition |= m_state.nodes.at(i).x != x || m_state.nodes.at(i).y != y;
        }
    }
    if (!changedPosition) { setError({}); return true; }
    rememberEdit();
    for (int i = 0; i < count; ++i) {
        m_state.nodes[i].x = positions.at(i).x();
        m_state.nodes[i].y = positions.at(i).y();
    }
    setError({});
    emit changed();
    return true;
}

} // namespace QindaTK
