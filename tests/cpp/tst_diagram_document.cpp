// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"
#include "diagram_document.h"

#include <QFile>
#include <QMetaObject>
#include <QPointF>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>
#include <functional>

using namespace QindaTK;

class TstDiagramDocument : public QObject {
    Q_OBJECT
private slots:
    void editsUndoAndRoundtrip();
    void layoutCyclesAndQuantitativeFlow();
    void barycentricLayoutUncrossesColumns();
    void routesAvoidCards();
    void sankeyBandsAndConservation();
    void groupRemovalAndExports();
    void sharedCanvasContract();
};

namespace {
QVariantMap row(const QVariantList &rows, const QString &key, const QString &id)
{
    for (const QVariant &value : rows)
        if (value.toMap().value(key).toString() == id) return value.toMap();
    return {};
}

QVariantList points(std::initializer_list<QPointF> list)
{
    QVariantList result;
    for (const QPointF &point : list) result.append(point);
    return result;
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool wellFormed(const QString &xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) reader.readNext();
    return !reader.hasError();
}
}

void TstDiagramDocument::editsUndoAndRoundtrip()
{
    DiagramDocument document;
    const QString a = document.addNode(QStringLiteral("Receive"), 10, 20);
    const QString b = document.addNode(QStringLiteral("Review"), 320, 50);
    QVERIFY(!a.isEmpty() && !b.isEmpty() && a != b);
    const QString out = document.addPort(a, QStringLiteral("Work"), QStringLiteral("output"),
                                         QStringLiteral("work"));
    QVERIFY(document.undo());
    QCOMPARE(document.nodes().at(0).toMap().value(QStringLiteral("outputs")).toList().size(), 0);
    QVERIFY(document.redo());
    QCOMPARE(document.nodes().at(0).toMap().value(QStringLiteral("outputs")).toList()
                 .at(0).toMap().value(QStringLiteral("portId")).toString(), out);
    const QString in = document.addPort(b, QStringLiteral("Work"), QStringLiteral("input"),
                                        QStringLiteral("work"));
    QVERIFY(!out.isEmpty() && !in.isEmpty() && out != in);
    const QString incompatible = document.addPort(b, QStringLiteral("Audio"),
        QStringLiteral("input"), QStringLiteral("audio"));
    QVERIFY(document.connectPorts(a, out, b, incompatible).isEmpty());
    const QString edge = document.connectPorts(a, out, b, in);
    QVERIFY(!edge.isEmpty());
    QVERIFY(document.connectPorts(a, out, b, in).isEmpty());
    QCOMPARE(document.edges().size(), 1);

    QVERIFY(document.moveNode(a, 15, 25));
    QVERIFY(document.undo());
    QCOMPARE(document.nodes().at(0).toMap().value(QStringLiteral("x")).toDouble(), 10.0);

    const qulonglong beforeMove = document.revision();
    QVERIFY(document.moveNodes({a, b}, 35, -15));
    QCOMPARE(document.revision(), beforeMove + 1);
    QCOMPARE(document.nodes().at(0).toMap().value(QStringLiteral("x")).toDouble(), 45.0);
    QCOMPARE(document.nodes().at(1).toMap().value(QStringLiteral("y")).toDouble(), 35.0);
    QCOMPARE(document.edges().at(0).toMap().value(QStringLiteral("edgeId")).toString(), edge);
    QVERIFY(document.undo());
    QCOMPARE(document.nodes().at(0).toMap().value(QStringLiteral("x")).toDouble(), 10.0);
    QVERIFY(document.redo());
    QCOMPARE(document.nodes().at(0).toMap().value(QStringLiteral("x")).toDouble(), 45.0);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("flow.qdiagram"));
    QVERIFY(document.saveToFile(path));
    DiagramDocument reopened;
    QVERIFY(reopened.loadFromFile(path));
    QCOMPARE(reopened.nodes(), document.nodes());
    QCOMPARE(reopened.edges(), document.edges());
    QVERIFY(!reopened.canUndo());
    QVERIFY(reopened.removeNode(a));
    QCOMPARE(reopened.edges().size(), 0);
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.edges().at(0).toMap().value(QStringLiteral("edgeId")).toString(), edge);

    QFile broken(path);
    QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
    broken.write("{\"format\":\"qindatk.diagram/1\",\"version\":1,\"nodes\":[],\"edges\":[{\"id\":\"dangling\"}]}");
    broken.close();
    const QVariantList stableNodes = reopened.nodes();
    QVERIFY(!reopened.loadFromFile(path));
    QCOMPARE(reopened.nodes(), stableNodes);
}

void TstDiagramDocument::layoutCyclesAndQuantitativeFlow()
{
    DiagramDocument graph;
    const QString a = graph.addNode(QStringLiteral("A"), 11, 17);
    const QString b = graph.addNode(QStringLiteral("B"), 21, 27);
    const QString c = graph.addNode(QStringLiteral("C"), 31, 37);
    const QString d = graph.addNode(QStringLiteral("D"), 41, 47);
    const QString a1 = graph.addPort(a, QStringLiteral("one"), QStringLiteral("output"));
    const QString a2 = graph.addPort(a, QStringLiteral("two"), QStringLiteral("output"));
    const QString b1 = graph.addPort(b, QStringLiteral("one"), QStringLiteral("input"));
    const QString b2 = graph.addPort(b, QStringLiteral("two"), QStringLiteral("input"));
    const QString bo = graph.addPort(b, QStringLiteral("out"), QStringLiteral("output"));
    const QString ci = graph.addPort(c, QStringLiteral("in"), QStringLiteral("input"));
    const QString co = graph.addPort(c, QStringLiteral("out"), QStringLiteral("output"));
    const QString bi = graph.addPort(b, QStringLiteral("loop"), QStringLiteral("input"));
    const QString di = graph.addPort(d, QStringLiteral("in"), QStringLiteral("input"));
    const QString e1 = graph.connectPorts(a, a1, b, b1);
    const QString e2 = graph.connectPorts(a, a2, b, b2);
    QVERIFY(!e1.isEmpty() && !e2.isEmpty() && e1 != e2);
    QVERIFY(!graph.connectPorts(b, bo, c, ci).isEmpty());
    QVERIFY(!graph.connectPorts(c, co, b, bi).isEmpty());
    QVERIFY(!graph.connectPorts(c, co, d, di).isEmpty());
    QVERIFY(graph.setEdgeFlow(e1, 3));
    QVERIFY(graph.setEdgeFlow(e2, 5));
    QVERIFY(!graph.setEdgeFlow(e1, -1));
    QCOMPARE(graph.edges().at(0).toMap().value(QStringLiteral("flow")).toDouble(), 3.0);
    QCOMPARE(graph.nodes().at(1).toMap().value(QStringLiteral("inFlow")).toDouble(), 9.0);
    QCOMPARE(graph.nodes().at(0).toMap().value(QStringLiteral("outFlow")).toDouble(), 8.0);
    QCOMPARE(graph.nodes().at(0).toMap().value(QStringLiteral("x")).toDouble(), 11.0);
    const QVariantList manual = graph.nodes();
    const qulonglong before = graph.revision();
    QVERIFY(graph.autoLayout(300, 100, 20, 30));
    QCOMPARE(graph.revision(), before + 1);
    const QVariantList laidOut = graph.nodes();
    QCOMPARE(laidOut.at(0).toMap().value(QStringLiteral("x")).toDouble(), 20.0);
    QCOMPARE(laidOut.at(1).toMap().value(QStringLiteral("x")).toDouble(), 320.0);
    QCOMPARE(laidOut.at(2).toMap().value(QStringLiteral("x")).toDouble(), 320.0);
    QCOMPARE(laidOut.at(1).toMap().value(QStringLiteral("y")).toDouble(), 30.0);
    QCOMPARE(laidOut.at(2).toMap().value(QStringLiteral("y")).toDouble(), 130.0);
    QCOMPARE(laidOut.at(3).toMap().value(QStringLiteral("x")).toDouble(), 620.0);
    QVERIFY(graph.autoLayout(300, 100, 20, 30));
    QCOMPARE(graph.revision(), before + 1);
    QVERIFY(graph.undo());
    QCOMPARE(graph.nodes(), manual);
    QVERIFY(graph.redo());
    QCOMPARE(graph.nodes(), laidOut);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("quantified.qdiagram"));
    QVERIFY(graph.saveToFile(path));
    DiagramDocument reopened;
    QVERIFY(reopened.loadFromFile(path));
    QCOMPARE(reopened.nodes(), laidOut);
    QCOMPARE(reopened.edges(), graph.edges());
}

void TstDiagramDocument::barycentricLayoutUncrossesColumns()
{
    // Saved order would put C above D and cross A->D with B->C.
    DiagramDocument graph;
    const QString a = graph.addNode(QStringLiteral("A"), 0, 0);
    const QString b = graph.addNode(QStringLiteral("B"), 0, 0);
    const QString c = graph.addNode(QStringLiteral("C"), 0, 0);
    const QString d = graph.addNode(QStringLiteral("D"), 0, 0);
    const QString ao = graph.addPort(a, QStringLiteral("out"), QStringLiteral("output"));
    const QString bo = graph.addPort(b, QStringLiteral("out"), QStringLiteral("output"));
    const QString ci = graph.addPort(c, QStringLiteral("in"), QStringLiteral("input"));
    const QString di = graph.addPort(d, QStringLiteral("in"), QStringLiteral("input"));
    QVERIFY(!graph.connectPorts(a, ao, d, di).isEmpty());
    QVERIFY(!graph.connectPorts(b, bo, c, ci).isEmpty());
    QVERIFY(graph.autoLayout(300, 100));
    const QVariantList nodes = graph.nodes();
    QCOMPARE(row(nodes, QStringLiteral("nodeId"), a).value(QStringLiteral("y")).toDouble(), 0.0);
    QCOMPARE(row(nodes, QStringLiteral("nodeId"), b).value(QStringLiteral("y")).toDouble(), 100.0);
    QCOMPARE(row(nodes, QStringLiteral("nodeId"), d).value(QStringLiteral("y")).toDouble(), 0.0);
    QCOMPARE(row(nodes, QStringLiteral("nodeId"), c).value(QStringLiteral("y")).toDouble(), 100.0);
    QCOMPARE(row(nodes, QStringLiteral("nodeId"), c).value(QStringLiteral("x")).toDouble(), 300.0);
}

void TstDiagramDocument::routesAvoidCards()
{
    DiagramDocument graph;
    const QString a = graph.addNode(QStringLiteral("A"), 0, 0);
    const QString b = graph.addNode(QStringLiteral("B"), 400, 200);
    const QString c = graph.addNode(QStringLiteral("Obstacle"), 260, 100);
    const QString ao = graph.addPort(a, QStringLiteral("out"), QStringLiteral("output"));
    const QString ai = graph.addPort(a, QStringLiteral("in"), QStringLiteral("input"));
    const QString bi = graph.addPort(b, QStringLiteral("in"), QStringLiteral("input"));
    const QString bo = graph.addPort(b, QStringLiteral("out"), QStringLiteral("output"));
    const QString forward = graph.connectPorts(a, ao, b, bi);
    const QString backward = graph.connectPorts(b, bo, a, ai);
    QVERIFY(!forward.isEmpty() && !backward.isEmpty());
    const QVariantMap metrics{{QStringLiteral("nodeWidth"), 200}, {QStringLiteral("nodeHeight"), 50},
        {QStringLiteral("headerHeight"), 24}, {QStringLiteral("rowHeight"), 22},
        {QStringLiteral("stub"), 14}, {QStringLiteral("clearance"), 16}};
    QVariantMap routes = graph.routeEdges(metrics);
    QCOMPARE(routes.size(), 2);
    // The midpoint channel x=300 crosses the obstacle; x=246 passes left of it.
    QCOMPARE(routes.value(forward).toMap().value(QStringLiteral("points")).toList(),
             points({{200, 35}, {246, 35}, {246, 235}, {400, 235}}));
    QVERIFY(routes.value(forward).toMap().value(QStringLiteral("clear")).toBool());
    // The feedback lane runs between A (bottom 50) and the obstacle (top 100).
    QCOMPARE(routes.value(backward).toMap().value(QStringLiteral("points")).toList(),
             points({{600, 235}, {614, 235}, {614, 64}, {-14, 64}, {-14, 35}, {0, 35}}));
    QVERIFY(routes.value(backward).toMap().value(QStringLiteral("clear")).toBool());

    QVERIFY(graph.removeNode(c));
    routes = graph.routeEdges(metrics);
    QCOMPARE(routes.value(forward).toMap().value(QStringLiteral("points")).toList(),
             points({{200, 35}, {300, 35}, {300, 235}, {400, 235}}));
    QVERIFY(graph.undo());
    QCOMPARE(graph.routeEdges(metrics).value(forward).toMap().value(QStringLiteral("points"))
                 .toList().at(1).toPointF(), QPointF(246, 35));
    // Default metrics derive the card height from the busiest side: 24 + 22 + 4.
    QCOMPARE(graph.routeEdges().value(forward).toMap().value(QStringLiteral("points")).toList()
                 .size(), 4);
    QVERIFY(graph.routeEdges({{QStringLiteral("stub"), 0}}).isEmpty());
    QVERIFY(graph.routeEdges({{QStringLiteral("nodeWidth"), qQNaN()}}).isEmpty());
}

void TstDiagramDocument::sankeyBandsAndConservation()
{
    DiagramDocument graph;
    const QString s = graph.addNode(QStringLiteral("Supply"), 0, 0);
    const QString m = graph.addNode(QStringLiteral("Mill"), 0, 0);
    const QString x = graph.addNode(QStringLiteral("Export"), 0, 0);
    const QString y = graph.addNode(QStringLiteral("Local"), 0, 0);
    const QString so = graph.addPort(s, QStringLiteral("out"), QStringLiteral("output"));
    const QString mi = graph.addPort(m, QStringLiteral("in"), QStringLiteral("input"));
    const QString mo = graph.addPort(m, QStringLiteral("out"), QStringLiteral("output"));
    const QString xi = graph.addPort(x, QStringLiteral("in"), QStringLiteral("input"));
    const QString yi = graph.addPort(y, QStringLiteral("in"), QStringLiteral("input"));
    const QString sm = graph.connectPorts(s, so, m, mi);
    const QString mx = graph.connectPorts(m, mo, x, xi);
    const QString my = graph.connectPorts(m, mo, y, yi);
    QVERIFY(graph.setEdgeFlow(sm, 10) && graph.setEdgeFlow(mx, 6) && graph.setEdgeFlow(my, 4));
    QVERIFY(graph.flowImbalances().isEmpty());

    const QVariantMap layout = graph.sankeyLayout(500, 200, 10, 20);
    QVERIFY(layout.value(QStringLiteral("valid")).toBool());
    // Column 2 limits the scale: (200 - 20) / (6 + 4) = 18 px per unit.
    QCOMPARE(layout.value(QStringLiteral("scale")).toDouble(), 18.0);
    QCOMPARE(layout.value(QStringLiteral("columns")).toInt(), 3);
    const QVariantList nodes = layout.value(QStringLiteral("nodes")).toList();
    const QVariantList links = layout.value(QStringLiteral("links")).toList();
    auto node = [&](const QString &id, const char *key) {
        return row(nodes, QStringLiteral("nodeId"), id).value(QLatin1String(key));
    };
    auto link = [&](const QString &id, const char *key) {
        return row(links, QStringLiteral("edgeId"), id).value(QLatin1String(key));
    };
    QCOMPARE(node(s, "x").toDouble(), 0.0);
    QCOMPARE(node(s, "y").toDouble(), 10.0);
    QCOMPARE(node(s, "height").toDouble(), 180.0);
    QCOMPARE(node(s, "role").toString(), QStringLiteral("source"));
    QCOMPARE(node(m, "x").toDouble(), 245.0);
    QCOMPARE(node(m, "role").toString(), QStringLiteral("through"));
    QCOMPARE(node(x, "x").toDouble(), 490.0);
    QCOMPARE(node(x, "y").toDouble(), 0.0);
    QCOMPARE(node(x, "height").toDouble(), 108.0);
    QCOMPARE(node(y, "y").toDouble(), 128.0);
    QCOMPARE(node(y, "height").toDouble(), 72.0);
    QCOMPARE(node(y, "role").toString(), QStringLiteral("sink"));
    QCOMPARE(link(sm, "width").toDouble(), 180.0);
    QCOMPARE(link(sm, "x0").toDouble(), 10.0);
    QCOMPARE(link(sm, "y0").toDouble(), 100.0);
    QCOMPARE(link(sm, "x1").toDouble(), 245.0);
    QCOMPARE(link(sm, "y1").toDouble(), 100.0);
    QCOMPARE(link(mx, "y0").toDouble(), 64.0);
    QCOMPARE(link(mx, "y1").toDouble(), 54.0);
    QCOMPARE(link(my, "width").toDouble(), 72.0);
    QCOMPARE(link(my, "y0").toDouble(), 154.0);
    QCOMPARE(link(my, "y1").toDouble(), 164.0);
    QVERIFY(!link(my, "feedback").toBool());
    // Bands leaving the mill fill exactly its height: 108 + 72 = 180.
    QCOMPARE(link(mx, "width").toDouble() + link(my, "width").toDouble(), node(m, "height").toDouble());

    QVERIFY(graph.setEdgeFlow(my, 3));
    const QVariantList imbalances = graph.flowImbalances();
    QCOMPARE(imbalances.size(), 1);
    QCOMPARE(imbalances.at(0).toMap().value(QStringLiteral("nodeId")).toString(), m);
    QCOMPARE(imbalances.at(0).toMap().value(QStringLiteral("imbalance")).toDouble(), 1.0);
    QVERIFY(graph.flowImbalances(0.2).isEmpty()); // 1 <= 0.2 * 10
    QVERIFY(graph.undo());
    QVERIFY(graph.flowImbalances().isEmpty());

    // Degenerate inputs never produce a partial layout.
    QVERIFY(!graph.sankeyLayout(5, 200, 10, 20).value(QStringLiteral("valid")).toBool());
    QVERIFY(!graph.sankeyLayout(500, 10, 10, 20).value(QStringLiteral("valid")).toBool());
    QVERIFY(!graph.sankeyLayout(500, 200, 10, -1).value(QStringLiteral("valid")).toBool());
    QVERIFY(!DiagramDocument().sankeyLayout(500, 200, 10, 20).value(QStringLiteral("valid")).toBool());
    QVERIFY(graph.flowImbalances(-1).isEmpty());

    DiagramDocument loop;
    const QString p = loop.addNode(QStringLiteral("P"), 0, 0);
    const QString q = loop.addNode(QStringLiteral("Q"), 0, 0);
    const QString po = loop.addPort(p, QStringLiteral("out"), QStringLiteral("output"));
    const QString pi = loop.addPort(p, QStringLiteral("in"), QStringLiteral("input"));
    const QString qo = loop.addPort(q, QStringLiteral("out"), QStringLiteral("output"));
    const QString qi = loop.addPort(q, QStringLiteral("in"), QStringLiteral("input"));
    const QString pq = loop.connectPorts(p, po, q, qi);
    const QString qp = loop.connectPorts(q, qo, p, pi);
    QVERIFY(!pq.isEmpty() && !qp.isEmpty());
    QVERIFY(loop.setEdgeFlow(pq, 2));
    const QVariantMap cyclic = loop.sankeyLayout(300, 100, 10, 10);
    QVERIFY(cyclic.value(QStringLiteral("valid")).toBool());
    QCOMPARE(cyclic.value(QStringLiteral("columns")).toInt(), 1);
    // Two return lanes reserve 2 gaps + 3 units: (100 - 10 - 20) / (4 + 3) = 10.
    QCOMPARE(cyclic.value(QStringLiteral("scale")).toDouble(), 10.0);
    const QVariantList cycleNodes = cyclic.value(QStringLiteral("nodes")).toList();
    const QVariantList cycleLinks = cyclic.value(QStringLiteral("links")).toList();
    auto cycleNode = [&](const QString &id, const char *key) {
        return row(cycleNodes, QStringLiteral("nodeId"), id).value(QLatin1String(key)).toDouble();
    };
    auto cycleLink = [&](const QString &id, const char *key) {
        return row(cycleLinks, QStringLiteral("edgeId"), id).value(QLatin1String(key));
    };
    QVERIFY(cycleLink(pq, "feedback").toBool() && cycleLink(qp, "feedback").toBool());
    // Bars fill the 50 px node area; lanes sit below it and never overlap bars.
    QCOMPARE(cycleNode(p, "y"), 0.0);
    QCOMPARE(cycleNode(p, "height"), 20.0);
    QCOMPARE(cycleNode(q, "y"), 30.0);
    QCOMPARE(cycleNode(q, "height"), 20.0);
    // Q->P leaves lower (y0 35), so it takes the inner lane.
    QCOMPARE(cycleLink(qp, "y0").toDouble(), 35.0);
    QCOMPARE(cycleLink(qp, "laneY").toDouble(), 65.0);
    QCOMPARE(cycleLink(qp, "loop").toDouble(), 15.0);
    QCOMPARE(cycleLink(pq, "y0").toDouble(), 10.0);
    QCOMPARE(cycleLink(pq, "y1").toDouble(), 40.0);
    QCOMPARE(cycleLink(pq, "laneY").toDouble(), 90.0);
    QCOMPARE(cycleLink(pq, "loop").toDouble(), 40.0);
    QCOMPARE(cycleLink(pq, "laneY").toDouble() + cycleLink(pq, "width").toDouble() / 2, 100.0);
    // The widest leg (40 + 10) moves labels and reserves room on both sides.
    QCOMPARE(cycleNode(p, "labelInsetRight"), 50.0);
    QCOMPARE(cycleNode(p, "labelInsetLeft"), 50.0);
    QCOMPARE(cycleNode(p, "x"), 50.0);
    QCOMPARE(cycleLink(pq, "x0").toDouble(), 60.0);
    QCOMPARE(cycleLink(pq, "x1").toDouble(), 50.0);
    QCOMPARE(row(nodes, QStringLiteral("nodeId"), s).value(QStringLiteral("labelInsetRight")).toDouble(), 0.0);
    QVERIFY(!loop.sankeyLayout(100, 100, 10, 10).value(QStringLiteral("valid")).toBool());
    // P receives 1 and sends 2, Q the reverse: both through nodes are unbalanced.
    QCOMPARE(loop.flowImbalances().size(), 2);
}

void TstDiagramDocument::groupRemovalAndExports()
{
    DiagramDocument graph;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!graph.exportSvg(directory.filePath(QStringLiteral("empty.svg"))));
    QVERIFY(!graph.lastError().isEmpty());
    const QString a = graph.addNode(QStringLiteral("A & B"), 0, 0);
    const QString b = graph.addNode(QStringLiteral("Review"), 300, 0);
    const QString ao = graph.addPort(a, QStringLiteral("out"), QStringLiteral("output"));
    const QString bi = graph.addPort(b, QStringLiteral("in"), QStringLiteral("input"));
    const QString edge = graph.connectPorts(a, ao, b, bi);
    QVERIFY(graph.setEdgeFlow(edge, 2));

    const QString svgPath = directory.filePath(QStringLiteral("diagram.svg"));
    QVERIFY(graph.exportSvg(svgPath));
    QString expected = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"532\" height=\"82\" viewBox=\"-16 -16 532 82\">\n"
        "<g fill=\"none\" stroke=\"#4b5563\" stroke-width=\"1\">\n"
        "<polyline data-edge-id=\"@E@\" data-flow=\"2\" points=\"200,35 250,35 300,35\"/>\n"
        "</g>\n<g font-family=\"sans-serif\" font-size=\"11\">\n"
        "<g data-node-id=\"@A@\" data-stencil=\"process\">\n"
        "<rect x=\"0\" y=\"0\" width=\"200\" height=\"50\" rx=\"4\" fill=\"#ffffff\" stroke=\"#4b5563\"/>\n"
        "<text x=\"8\" y=\"16\" fill=\"#111827\">A &amp; B</text>\n"
        "<circle data-port-id=\"@AO@\" cx=\"200\" cy=\"35\" r=\"3\" fill=\"#4b5563\"/>\n"
        "</g>\n"
        "<g data-node-id=\"@B@\" data-stencil=\"process\">\n"
        "<rect x=\"300\" y=\"0\" width=\"200\" height=\"50\" rx=\"4\" fill=\"#ffffff\" stroke=\"#4b5563\"/>\n"
        "<text x=\"308\" y=\"16\" fill=\"#111827\">Review</text>\n"
        "<circle data-port-id=\"@BI@\" cx=\"300\" cy=\"35\" r=\"3\" fill=\"#4b5563\"/>\n"
        "</g>\n</g>\n</svg>\n");
    auto fill = [&](QString text) {
        return text.replace(QStringLiteral("@AO@"), ao).replace(QStringLiteral("@BI@"), bi)
            .replace(QStringLiteral("@A@"), a).replace(QStringLiteral("@B@"), b)
            .replace(QStringLiteral("@E@"), edge);
    };
    QCOMPARE(readText(svgPath), fill(expected));
    QVERIFY(wellFormed(readText(svgPath)));
    QVERIFY(!graph.exportSvg(svgPath, {{QStringLiteral("nodeWidth"), -1}}));
    QCOMPARE(readText(svgPath), fill(expected)); // a failed export leaves the file

    const QString graphMlPath = directory.filePath(QStringLiteral("diagram.graphml"));
    QVERIFY(graph.exportGraphMl(graphMlPath));
    expected = QStringLiteral(
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
        "<graph id=\"diagram\" edgedefault=\"directed\">\n"
        "<node id=\"@A@\"><data key=\"title\">A &amp; B</data><data key=\"stencil\">process</data>"
        "<data key=\"x\">0</data><data key=\"y\">0</data>\n"
        "<port name=\"@AO@\"><data key=\"direction\">output</data><data key=\"label\">out</data>"
        "<data key=\"type\">any</data></port>\n</node>\n"
        "<node id=\"@B@\"><data key=\"title\">Review</data><data key=\"stencil\">process</data>"
        "<data key=\"x\">300</data><data key=\"y\">0</data>\n"
        "<port name=\"@BI@\"><data key=\"direction\">input</data><data key=\"label\">in</data>"
        "<data key=\"type\">any</data></port>\n</node>\n"
        "<edge id=\"@E@\" source=\"@A@\" target=\"@B@\" sourceport=\"@AO@\" targetport=\"@BI@\">"
        "<data key=\"flow\">2</data></edge>\n"
        "</graph>\n</graphml>\n");
    QCOMPARE(readText(graphMlPath), fill(expected));
    QVERIFY(wellFormed(readText(graphMlPath)));

    // Removing a selection is one undo step and takes incident edges along.
    const QString c = graph.addNode(QStringLiteral("C"), 600, 0);
    const qulonglong before = graph.revision();
    QVERIFY(!graph.removeNodes({a, QStringLiteral("missing")}));
    QCOMPARE(graph.nodes().size(), 3);
    QVERIFY(graph.removeNodes({a, b, a}));
    QCOMPARE(graph.revision(), before + 1);
    QCOMPARE(graph.nodes().size(), 1);
    QCOMPARE(graph.nodes().at(0).toMap().value(QStringLiteral("nodeId")).toString(), c);
    QVERIFY(graph.edges().isEmpty());
    QVERIFY(graph.undo());
    QCOMPARE(graph.nodes().size(), 3);
    QCOMPARE(graph.edges().at(0).toMap().value(QStringLiteral("edgeId")).toString(), edge);
}

// AGENT-CONTRACT (docs/diagram.md "Domain canvas contract", D-122): both
// domain documents answer the same generic calls through the meta-object.
void TstDiagramDocument::sharedCanvasContract()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DiagramDocument diagram;
    CadDocument cad;
    const QList<QPair<QObject *, std::function<void()>>> documents{
        {&diagram, [&] { diagram.addNode(QStringLiteral("Node"), 0, 0); }},
        {&cad, [&] { cad.addPoint(1, 2); }}};
    int serial = 0;
    for (const auto &entry : documents) {
        QObject *document = entry.first;
        const QMetaObject *meta = document->metaObject();
        for (const char *property : {"revision", "canUndo", "canRedo", "lastError"})
            QVERIFY2(meta->indexOfProperty(property) >= 0, property);
        for (const char *method : {"undo()", "redo()", "saveToFile(QString)",
                                   "loadFromFile(QString)", "exportSvg(QString)"})
            QVERIFY2(meta->indexOfMethod(method) >= 0, method);
        const qulonglong revision = document->property("revision").toULongLong();
        entry.second();
        QCOMPARE(document->property("revision").toULongLong(), revision + 1);
        QVERIFY(document->property("canUndo").toBool());
        const QString path = directory.filePath(QStringLiteral("doc%1").arg(++serial));
        bool ok = false;
        QVERIFY(QMetaObject::invokeMethod(document, "saveToFile", Q_RETURN_ARG(bool, ok),
                                          Q_ARG(QString, path)) && ok);
        QVERIFY(QMetaObject::invokeMethod(document, "undo", Q_RETURN_ARG(bool, ok)) && ok);
        QVERIFY(document->property("canRedo").toBool());
        QVERIFY(QMetaObject::invokeMethod(document, "loadFromFile", Q_RETURN_ARG(bool, ok),
                                          Q_ARG(QString, path)) && ok);
        QVERIFY(!document->property("canUndo").toBool());
        QVERIFY(!document->property("canRedo").toBool());
        const QString svg = path + QStringLiteral(".svg");
        QVERIFY(QMetaObject::invokeMethod(document, "exportSvg", Q_RETURN_ARG(bool, ok),
                                          Q_ARG(QString, svg)) && ok);
        QVERIFY(readText(svg).startsWith(QStringLiteral("<svg ")));
        QVERIFY(wellFormed(readText(svg)));
        QVERIFY(document->property("lastError").toString().isEmpty());
    }
}

QTEST_MAIN(TstDiagramDocument)
#include "tst_diagram_document.moc"
