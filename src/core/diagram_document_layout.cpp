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

// AGENT-NOTE (docs/diagram.md, D-123): collapse cycles before assigning
// columns so feedback edges and parallel links never make topological layout
// ambiguous. Components in a column are then ordered by the mean row of their
// predecessors (one downward barycenter sweep) to reduce crossings; saved
// node order breaks ties. autoLayout and sankeyLayout share this ranking.
DiagramDocument::Ranking DiagramDocument::rank() const
{
    const int count = m_state.nodes.size();
    Ranking result;
    result.depth.fill(0, count);
    result.row.fill(0, count);
    if (!count) return result;
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
    QVector<QVector<int>> members(componentCount), predecessors(count);
    for (int i = 0; i < count; ++i) {
        first[component[i]] = std::min(first[component[i]], i);
        members[component[i]].append(i);
        for (int next : links.at(i)) {
            if (component[i] == component[next]) continue;
            successors[component[i]].insert(component[next]);
            predecessors[next].append(i);
        }
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

    int maxDepth = 0;
    for (int d : depth) maxDepth = std::max(maxDepth, d);
    QVector<QVector<int>> columns(maxDepth + 1);
    for (int group = 0; group < componentCount; ++group) columns[depth[group]].append(group);
    // Predecessors always sit in shallower columns, so their rows are final
    // when a column is ordered. Hash iteration never affects output.
    for (int column = 0; column <= maxDepth; ++column) {
        QHash<int, double> barycenter;
        for (int group : columns.at(column)) {
            double sum = 0;
            int incoming = 0;
            for (int member : members.at(group))
                for (int previous : predecessors.at(member)) {
                    sum += result.row.at(previous);
                    ++incoming;
                }
            barycenter.insert(group, incoming ? sum / incoming : 0.0);
        }
        auto &groups = columns[column];
        std::sort(groups.begin(), groups.end(), [&](int a, int b) {
            const double left = barycenter.value(a), right = barycenter.value(b);
            return left != right ? left < right : first[a] < first[b];
        });
        int row = 0;
        for (int group : groups)
            for (int member : members.at(group)) {
                result.depth[member] = column;
                result.row[member] = row++;
            }
    }
    result.columns = maxDepth + 1;
    return result;
}

bool DiagramDocument::autoLayout(double columnStep, double rowStep,
                                 double originX, double originY)
{
    if (!std::isfinite(columnStep) || !std::isfinite(rowStep)
        || columnStep <= 0 || rowStep <= 0 || !std::isfinite(originX)
        || !std::isfinite(originY) || m_state.nodes.isEmpty()) {
        setError(QStringLiteral("Invalid diagram layout")); return false;
    }
    const int count = m_state.nodes.size();
    const Ranking ranking = rank();
    QVector<QPointF> positions(count);
    bool changedPosition = false;
    for (int i = 0; i < count; ++i) {
        const double x = originX + ranking.depth.at(i) * columnStep;
        const double y = originY + ranking.row.at(i) * rowStep;
        if (!std::isfinite(x) || !std::isfinite(y)
            || std::abs(x) > 1000000 || std::abs(y) > 1000000) {
            setError(QStringLiteral("Diagram layout exceeds coordinate range")); return false;
        }
        positions[i] = QPointF(x, y);
        changedPosition |= m_state.nodes.at(i).x != x || m_state.nodes.at(i).y != y;
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
