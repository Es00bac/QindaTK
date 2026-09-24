// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagram_document.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaTK;

class TstDiagramDocument : public QObject {
    Q_OBJECT
private slots:
    void editsUndoAndRoundtrip();
    void layoutCyclesAndQuantitativeFlow();
};

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

QTEST_MAIN(TstDiagramDocument)
#include "tst_diagram_document.moc"
