// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QRectF>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
// True when an axis-aligned segment enters a card's interior. Touching an
// edge is allowed: ports sit on card borders.
bool blocks(const QRectF &card, const QPointF &p, const QPointF &q)
{
    const double left = std::min(p.x(), q.x()), right = std::max(p.x(), q.x());
    const double top = std::min(p.y(), q.y()), bottom = std::max(p.y(), q.y());
    if (top == bottom)
        return card.top() < top && top < card.bottom()
            && std::max(left, card.left()) < std::min(right, card.right());
    return card.left() < left && left < card.right()
        && std::max(top, card.top()) < std::min(bottom, card.bottom());
}

bool clearOf(const QVector<QRectF> &cards, const QVector<QPointF> &points)
{
    for (int i = 1; i < points.size(); ++i)
        for (const QRectF &card : cards)
            if (blocks(card, points.at(i - 1), points.at(i))) return false;
    return true;
}

QVector<QPointF> compact(const QVector<QPointF> &points)
{
    QVector<QPointF> result;
    for (const QPointF &point : points)
        if (result.isEmpty() || result.constLast() != point) result.append(point);
    return result;
}

bool readMetric(const QVariantMap &map, const char *key, double *target)
{
    const QVariant value = map.value(QLatin1String(key));
    if (!value.isValid()) return true;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || number <= 0 || number > 100000) return false;
    *target = number;
    return true;
}
}

bool DiagramDocument::metricsFrom(const QVariantMap &map, Metrics *metrics) const
{
    if (!readMetric(map, "nodeWidth", &metrics->nodeWidth)
        || !readMetric(map, "nodeHeight", &metrics->nodeHeight)
        || !readMetric(map, "headerHeight", &metrics->headerHeight)
        || !readMetric(map, "rowHeight", &metrics->rowHeight)
        || !readMetric(map, "stub", &metrics->stub)
        || !readMetric(map, "clearance", &metrics->clearance)) return false;
    if (metrics->nodeHeight <= 0) {
        int ports = 1;
        for (const Node &node : m_state.nodes)
            ports = std::max({ports, int(node.inputs.size()), int(node.outputs.size())});
        metrics->nodeHeight = metrics->headerHeight + ports * metrics->rowHeight + 4;
    }
    return true;
}

// AGENT-NOTE (docs/diagram.md, D-124): one router serves the canvas and SVG
// export. Candidates are tried in a fixed order: the midpoint dogleg, then
// vertical channels beside cards nearest the midpoint, then horizontal lanes
// above/below cards with the shortest detour. The first route that enters no
// card interior wins; otherwise the outermost lane is returned, not clear.
QHash<QString, DiagramDocument::Route> DiagramDocument::computeRoutes(const Metrics &m) const
{
    QHash<QString, Route> routes;
    QVector<QRectF> cards;
    QHash<QString, int> byId;
    for (const Node &node : m_state.nodes) {
        byId.insert(node.id, cards.size());
        cards.append(QRectF(node.x, node.y, m.nodeWidth, m.nodeHeight));
    }
    auto anchor = [&](const QString &nodeId, const QString &portId, bool output) {
        const Node &node = m_state.nodes.at(byId.value(nodeId));
        const auto &ports = output ? node.outputs : node.inputs;
        int index = 0;
        for (int i = 0; i < ports.size(); ++i)
            if (ports.at(i).id == portId) { index = i; break; }
        return QPointF(node.x + (output ? m.nodeWidth : 0),
                       node.y + m.headerHeight + m.rowHeight * (index + 0.5));
    };
    double outerTop = std::numeric_limits<double>::infinity();
    for (const QRectF &card : cards) outerTop = std::min(outerTop, card.top());

    for (const Edge &edge : m_state.edges) {
        const QPointF a = anchor(edge.fromNode, edge.fromPort, true);
        const QPointF b = anchor(edge.toNode, edge.toPort, false);
        Route route;
        bool found = false;
        if (b.x() >= a.x() + m.stub * 2) {
            const double middle = (a.x() + b.x()) / 2;
            QVector<double> channels{middle};
            for (const QRectF &card : cards)
                channels << card.left() - m.stub << card.right() + m.stub;
            channels.erase(std::remove_if(channels.begin(), channels.end(), [&](double x) {
                return x < a.x() + m.stub || x > b.x() - m.stub;
            }), channels.end());
            std::stable_sort(channels.begin(), channels.end(), [&](double l, double r) {
                const double dl = std::abs(l - middle), dr = std::abs(r - middle);
                return dl != dr ? dl < dr : l < r;
            });
            for (double x : channels) {
                const QVector<QPointF> points = compact({a, {x, a.y()}, {x, b.y()}, b});
                if (clearOf(cards, points)) { route.points = points; found = true; break; }
            }
        }
        if (!found) {
            const double left = a.x() + m.stub, right = b.x() - m.stub;
            QVector<double> lanes{std::min(a.y(), b.y()) - m.clearance};
            for (const QRectF &card : cards)
                lanes << card.top() - m.stub << card.bottom() + m.stub;
            std::stable_sort(lanes.begin(), lanes.end(), [&](double l, double r) {
                const double dl = std::abs(l - a.y()) + std::abs(l - b.y());
                const double dr = std::abs(r - a.y()) + std::abs(r - b.y());
                return dl != dr ? dl < dr : l < r;
            });
            for (double y : lanes) {
                const QVector<QPointF> points = compact({a, {left, a.y()}, {left, y},
                                                         {right, y}, {right, b.y()}, b});
                if (clearOf(cards, points)) { route.points = points; found = true; break; }
            }
            if (!found) {
                const double y = std::min(outerTop, std::min(a.y(), b.y())) - m.clearance;
                route.points = compact({a, {left, a.y()}, {left, y}, {right, y}, {right, b.y()}, b});
                route.clear = false;
            }
        }
        routes.insert(edge.id, route);
    }
    return routes;
}

QVariantMap DiagramDocument::routeEdges(const QVariantMap &metrics) const
{
    Metrics m;
    if (!metricsFrom(metrics, &m)) return {};
    const QHash<QString, Route> routes = computeRoutes(m);
    QVariantMap result;
    for (auto it = routes.cbegin(); it != routes.cend(); ++it) {
        QVariantList points;
        for (const QPointF &point : it->points) points.append(point);
        result.insert(it.key(), QVariantMap{{QStringLiteral("points"), points},
                                            {QStringLiteral("clear"), it->clear}});
    }
    return result;
}

} // namespace QindaTK
