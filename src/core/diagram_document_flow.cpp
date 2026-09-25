// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QHash>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
QString roleFor(bool hasIn, bool hasOut)
{
    if (hasIn && hasOut) return QStringLiteral("through");
    if (hasOut) return QStringLiteral("source");
    if (hasIn) return QStringLiteral("sink");
    return QStringLiteral("isolated");
}
}

// AGENT-NOTE (docs/diagram.md): only nodes with both incoming and outgoing
// edges must conserve flow; sources and sinks are the graph's boundary.
QVariantList DiagramDocument::flowImbalances(double tolerance) const
{
    QVariantList result;
    if (!std::isfinite(tolerance) || tolerance < 0) return result;
    QHash<QString, double> incoming, outgoing;
    QHash<QString, int> inCount, outCount;
    for (const Edge &edge : m_state.edges) {
        outgoing[edge.fromNode] += edge.flow;
        incoming[edge.toNode] += edge.flow;
        ++outCount[edge.fromNode];
        ++inCount[edge.toNode];
    }
    for (const Node &node : m_state.nodes) {
        if (!inCount.value(node.id) || !outCount.value(node.id)) continue;
        const double in = incoming.value(node.id), out = outgoing.value(node.id);
        if (std::abs(in - out) <= tolerance * std::max(1.0, std::max(in, out))) continue;
        result.append(QVariantMap{{QStringLiteral("nodeId"), node.id},
            {QStringLiteral("title"), node.title}, {QStringLiteral("inFlow"), in},
            {QStringLiteral("outFlow"), out}, {QStringLiteral("imbalance"), in - out}});
    }
    return result;
}

// AGENT-CONTRACT (docs/diagram.md, SankeyDiagram.qml): band widths are
// flow × scale in pixels; one scale serves every column so equal flows
// have equal widths everywhere. Link y0/y1 and laneY are band centers;
// feedback links run source → right by `loop` → down to laneY → left to
// x1 − loop → up to y1 → target.
QVariantMap DiagramDocument::sankeyLayout(double width, double height,
                                          double nodeWidth, double gap) const
{
    const QVariantMap invalid{{QStringLiteral("valid"), false}};
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(nodeWidth)
        || !std::isfinite(gap) || nodeWidth <= 0 || width <= nodeWidth || height <= 0
        || gap < 0 || m_state.nodes.isEmpty()) return invalid;
    const int count = m_state.nodes.size();
    const Ranking ranking = rank();
    QHash<QString, int> byId;
    for (int i = 0; i < count; ++i) byId.insert(m_state.nodes.at(i).id, i);
    QVector<double> in(count, 0), out(count, 0), value(count, 0);
    QVector<int> inCount(count, 0), outCount(count, 0);
    for (const Edge &edge : m_state.edges) {
        out[byId.value(edge.fromNode)] += edge.flow;
        in[byId.value(edge.toNode)] += edge.flow;
        ++outCount[byId.value(edge.fromNode)];
        ++inCount[byId.value(edge.toNode)];
    }
    // Rows are dense 0..n-1 in each column; order[column][row] = node.
    QVector<QVector<int>> order(ranking.columns);
    for (int i = 0; i < count; ++i) value[i] = std::max(in.at(i), out.at(i));
    for (int i = 0; i < count; ++i) {
        auto &column = order[ranking.depth.at(i)];
        if (column.size() <= ranking.row.at(i)) column.resize(ranking.row.at(i) + 1);
        column[ranking.row.at(i)] = i;
    }
    // Feedback links (target not right of the source) return through lanes
    // below every column, so their width is reserved out of the height.
    const int edgeCount = m_state.edges.size();
    auto from = [&](int edge) { return byId.value(m_state.edges.at(edge).fromNode); };
    auto to = [&](int edge) { return byId.value(m_state.edges.at(edge).toNode); };
    QVector<bool> feedback(edgeCount);
    int feedbackCount = 0;
    double feedbackFlow = 0;
    for (int i = 0; i < edgeCount; ++i) {
        feedback[i] = ranking.depth.at(to(i)) <= ranking.depth.at(from(i));
        if (!feedback.at(i)) continue;
        ++feedbackCount;
        feedbackFlow += m_state.edges.at(i).flow;
    }
    double scale = std::numeric_limits<double>::infinity();
    for (const auto &column : order) {
        const double available = height - gap * (column.size() - 1) - gap * feedbackCount;
        if (available <= 0) return invalid;
        double sum = feedbackFlow;
        for (int node : column) sum += value.at(node);
        if (sum > 0) scale = std::min(scale, available / sum);
    }
    if (!std::isfinite(scale)) scale = 0;
    const double nodeArea = height - gap * feedbackCount - feedbackFlow * scale;

    QVector<double> x(count), y(count), extent(count);
    for (const auto &column : order) {
        double used = gap * (column.size() - 1);
        for (int node : column) used += value.at(node) * scale;
        double cursor = (nodeArea - used) / 2;
        for (int node : column) {
            y[node] = cursor;
            extent[node] = value.at(node) * scale;
            cursor += extent.at(node) + gap;
        }
    }

    // Stack outgoing bands by target position and incoming bands by source
    // position, feedback bands last (nearest the return lanes); saved edge
    // order breaks ties, so the result is deterministic.
    QVector<int> edgeOrder(edgeCount);
    for (int i = 0; i < edgeCount; ++i) edgeOrder[i] = i;
    auto center = [&](int node) { return y.at(node) + extent.at(node) / 2; };
    QVector<double> y0(edgeCount), y1(edgeCount), sourceCursor(count, 0), targetCursor(count, 0);
    std::stable_sort(edgeOrder.begin(), edgeOrder.end(), [&](int a, int b) {
        if (feedback.at(a) != feedback.at(b)) return feedback.at(b);
        return center(to(a)) < center(to(b));
    });
    for (int edge : edgeOrder) {
        const double band = m_state.edges.at(edge).flow * scale;
        y0[edge] = y.at(from(edge)) + sourceCursor.at(from(edge)) + band / 2;
        sourceCursor[from(edge)] += band;
    }
    for (int i = 0; i < edgeCount; ++i) edgeOrder[i] = i;
    std::stable_sort(edgeOrder.begin(), edgeOrder.end(), [&](int a, int b) {
        if (feedback.at(a) != feedback.at(b)) return feedback.at(b);
        return center(from(a)) < center(from(b));
    });
    for (int edge : edgeOrder) {
        const double band = m_state.edges.at(edge).flow * scale;
        y1[edge] = y.at(to(edge)) + targetCursor.at(to(edge)) + band / 2;
        targetCursor[to(edge)] += band;
    }

    // AGENT-NOTE (docs/diagram.md): return lanes nest. The feedback band that
    // leaves lowest takes the innermost lane (highest, nearest the bars);
    // `loop` is how far its vertical legs sit outside the source's right
    // edge and the target's left edge. Labels in a column move past the
    // widest leg (labelInset*), and the outer columns reserve that room.
    QVector<int> lanes;
    for (int i = 0; i < edgeCount; ++i)
        if (feedback.at(i)) lanes.append(i);
    std::stable_sort(lanes.begin(), lanes.end(), [&](int a, int b) { return y0.at(a) > y0.at(b); });
    QVector<double> laneY(edgeCount, 0), loop(edgeCount, 0);
    QVector<double> insetRight(ranking.columns, 0), insetLeft(ranking.columns, 0);
    double offset = 0;
    for (int edge : lanes) {
        const double band = m_state.edges.at(edge).flow * scale;
        offset += gap;
        laneY[edge] = nodeArea + offset + band / 2;
        loop[edge] = offset + band / 2;
        offset += band;
        auto &right = insetRight[ranking.depth.at(from(edge))];
        auto &left = insetLeft[ranking.depth.at(to(edge))];
        right = std::max(right, loop.at(edge) + band / 2);
        left = std::max(left, loop.at(edge) + band / 2);
    }
    const double leftReserve = insetLeft.constFirst();
    const double rightReserve = insetRight.constLast();
    const double span = width - nodeWidth - leftReserve - rightReserve;
    if (span < 0 || (ranking.columns > 1 && span <= 0)) return invalid;
    const double step = ranking.columns > 1 ? span / (ranking.columns - 1) : 0;
    for (int i = 0; i < count; ++i) x[i] = leftReserve + ranking.depth.at(i) * step;

    QVariantList nodes, links;
    for (int i = 0; i < count; ++i) {
        const Node &node = m_state.nodes.at(i);
        nodes.append(QVariantMap{{QStringLiteral("nodeId"), node.id},
            {QStringLiteral("title"), node.title}, {QStringLiteral("x"), x.at(i)},
            {QStringLiteral("y"), y.at(i)}, {QStringLiteral("width"), nodeWidth},
            {QStringLiteral("height"), extent.at(i)}, {QStringLiteral("value"), value.at(i)},
            {QStringLiteral("inFlow"), in.at(i)}, {QStringLiteral("outFlow"), out.at(i)},
            {QStringLiteral("column"), ranking.depth.at(i)},
            {QStringLiteral("labelInsetRight"), insetRight.at(ranking.depth.at(i))},
            {QStringLiteral("labelInsetLeft"), insetLeft.at(ranking.depth.at(i))},
            {QStringLiteral("role"), roleFor(inCount.at(i) > 0, outCount.at(i) > 0)}});
    }
    for (int i = 0; i < edgeCount; ++i) {
        const Edge &edge = m_state.edges.at(i);
        links.append(QVariantMap{{QStringLiteral("edgeId"), edge.id},
            {QStringLiteral("fromNodeId"), edge.fromNode},
            {QStringLiteral("toNodeId"), edge.toNode}, {QStringLiteral("flow"), edge.flow},
            {QStringLiteral("width"), edge.flow * scale},
            {QStringLiteral("x0"), x.at(from(i)) + nodeWidth}, {QStringLiteral("y0"), y0.at(i)},
            {QStringLiteral("x1"), x.at(to(i))}, {QStringLiteral("y1"), y1.at(i)},
            {QStringLiteral("feedback"), feedback.at(i)},
            {QStringLiteral("laneY"), laneY.at(i)}, {QStringLiteral("loop"), loop.at(i)}});
    }
    return QVariantMap{{QStringLiteral("valid"), true}, {QStringLiteral("scale"), scale},
                       {QStringLiteral("columns"), ranking.columns},
                       {QStringLiteral("nodes"), nodes}, {QStringLiteral("links"), links}};
}

} // namespace QindaTK
