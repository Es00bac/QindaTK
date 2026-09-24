// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
QString number(double value) { return QString::number(value == 0 ? 0 : value, 'g', 17); }
QString escaped(const QString &text) { return text.toHtmlEscaped(); }
bool writeAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

// AGENT-CONTRACT (docs/diagram.md): SVG export draws the same routes as
// DiagramCanvas (computeRoutes) in document units; data-* attributes carry
// the stable IDs. Colours are fixed print colours, not theme roles.
bool DiagramDocument::exportSvg(const QString &path, const QVariantMap &metrics)
{
    Metrics m;
    if (path.isEmpty() || m_state.nodes.isEmpty() || !metricsFrom(metrics, &m)) {
        setError(QStringLiteral("SVG export needs a path, valid metrics and a nonempty diagram"));
        return false;
    }
    const QHash<QString, Route> routes = computeRoutes(m);
    double left = std::numeric_limits<double>::infinity();
    double top = left, right = -left, bottom = -left;
    auto include = [&](double x, double y) {
        left = std::min(left, x); right = std::max(right, x);
        top = std::min(top, y); bottom = std::max(bottom, y);
    };
    for (const Node &node : m_state.nodes) {
        include(node.x, node.y);
        include(node.x + m.nodeWidth, node.y + m.nodeHeight);
    }
    for (const Route &route : routes)
        for (const QPointF &point : route.points) include(point.x(), point.y());
    left -= m.clearance; top -= m.clearance; right += m.clearance; bottom += m.clearance;
    const QString width = number(right - left), height = number(bottom - top);
    QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"%3 %4 %1 %2\">\n")
        .arg(width, height, number(left), number(top));
    svg += QStringLiteral("<g fill=\"none\" stroke=\"#4b5563\" stroke-width=\"1\">\n");
    for (const Edge &edge : m_state.edges) {
        QStringList points;
        const Route route = routes.value(edge.id);
        for (const QPointF &point : route.points)
            points << number(point.x()) + QChar(',') + number(point.y());
        svg += QStringLiteral("<polyline data-edge-id=\"%1\" data-flow=\"%2\" points=\"%3\"/>\n")
            .arg(escaped(edge.id), number(edge.flow), points.join(QChar(' ')));
    }
    svg += QStringLiteral("</g>\n<g font-family=\"sans-serif\" font-size=\"11\">\n");
    for (const Node &node : m_state.nodes) {
        svg += QStringLiteral("<g data-node-id=\"%1\" data-stencil=\"%2\">\n")
            .arg(escaped(node.id), escaped(node.stencilId));
        svg += QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"4\" fill=\"#ffffff\" stroke=\"#4b5563\"/>\n")
            .arg(number(node.x), number(node.y), number(m.nodeWidth), number(m.nodeHeight));
        svg += QStringLiteral("<text x=\"%1\" y=\"%2\" fill=\"#111827\">%3</text>\n")
            .arg(number(node.x + 8), number(node.y + m.headerHeight - 8), escaped(node.title));
        for (int side = 0; side < 2; ++side) {
            const auto &ports = side ? node.outputs : node.inputs;
            for (int i = 0; i < ports.size(); ++i)
                svg += QStringLiteral("<circle data-port-id=\"%1\" cx=\"%2\" cy=\"%3\" r=\"3\" fill=\"#4b5563\"/>\n")
                    .arg(escaped(ports.at(i).id), number(node.x + (side ? m.nodeWidth : 0)),
                         number(node.y + m.headerHeight + m.rowHeight * (i + 0.5)));
        }
        svg += QStringLiteral("</g>\n");
    }
    svg += QStringLiteral("</g>\n</svg>\n");
    if (!writeAtomic(path, svg.toUtf8())) { setError(QStringLiteral("Could not export SVG")); return false; }
    setError({});
    return true;
}

// GraphML keeps every stable ID: nodes, edges and ports (as GraphML ports
// with direction/label/type data), plus title, stencil, position and flow.
bool DiagramDocument::exportGraphMl(const QString &path)
{
    if (path.isEmpty() || m_state.nodes.isEmpty()) {
        setError(QStringLiteral("GraphML export needs a path and a nonempty diagram"));
        return false;
    }
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\">\n"
        "<key id=\"title\" for=\"node\" attr.name=\"title\" attr.type=\"string\"/>\n"
        "<key id=\"stencil\" for=\"node\" attr.name=\"stencilId\" attr.type=\"string\"/>\n"
        "<key id=\"x\" for=\"node\" attr.name=\"x\" attr.type=\"double\"/>\n"
        "<key id=\"y\" for=\"node\" attr.name=\"y\" attr.type=\"double\"/>\n"
        "<key id=\"direction\" for=\"port\" attr.name=\"direction\" attr.type=\"string\"/>\n"
        "<key id=\"label\" for=\"port\" attr.name=\"label\" attr.type=\"string\"/>\n"
        "<key id=\"type\" for=\"port\" attr.name=\"type\" attr.type=\"string\"/>\n"
        "<key id=\"flow\" for=\"edge\" attr.name=\"flow\" attr.type=\"double\"/>\n"
        "<graph id=\"diagram\" edgedefault=\"directed\">\n");
    auto data = [](const QString &key, const QString &value) {
        return QStringLiteral("<data key=\"%1\">%2</data>").arg(key, escaped(value));
    };
    for (const Node &node : m_state.nodes) {
        xml += QStringLiteral("<node id=\"%1\">").arg(escaped(node.id))
            + data(QStringLiteral("title"), node.title)
            + data(QStringLiteral("stencil"), node.stencilId)
            + data(QStringLiteral("x"), number(node.x)) + data(QStringLiteral("y"), number(node.y))
            + QChar('\n');
        for (int side = 0; side < 2; ++side)
            for (const Port &port : side ? node.outputs : node.inputs)
                xml += QStringLiteral("<port name=\"%1\">").arg(escaped(port.id))
                    + data(QStringLiteral("direction"),
                           side ? QStringLiteral("output") : QStringLiteral("input"))
                    + data(QStringLiteral("label"), port.label)
                    + data(QStringLiteral("type"), port.type) + QStringLiteral("</port>\n");
        xml += QStringLiteral("</node>\n");
    }
    for (const Edge &edge : m_state.edges)
        xml += QStringLiteral("<edge id=\"%1\" source=\"%2\" target=\"%3\" sourceport=\"%4\" targetport=\"%5\">")
                   .arg(escaped(edge.id), escaped(edge.fromNode), escaped(edge.toNode),
                        escaped(edge.fromPort), escaped(edge.toPort))
            + data(QStringLiteral("flow"), number(edge.flow)) + QStringLiteral("</edge>\n");
    xml += QStringLiteral("</graph>\n</graphml>\n");
    if (!writeAtomic(path, xml.toUtf8())) { setError(QStringLiteral("Could not export GraphML")); return false; }
    setError({});
    return true;
}

} // namespace QindaTK
