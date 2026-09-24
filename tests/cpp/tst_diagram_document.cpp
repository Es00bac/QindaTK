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

QTEST_MAIN(TstDiagramDocument)
#include "tst_diagram_document.moc"
