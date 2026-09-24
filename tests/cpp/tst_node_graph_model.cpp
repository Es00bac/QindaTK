// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/node_graph_model.h"

#include <QColorSpace>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class NodeGraphModelTest : public QObject {
    Q_OBJECT
private slots:
    void graphRendersAndReopens();
    void solidColorNodeRenders();
};

void NodeGraphModelTest::graphRendersAndReopens()
{
    NodeGraphModel graph;
    const QString red = graph.addNode(QStringLiteral("source"), QStringLiteral("Red"), 0, 0);
    const QString blue = graph.addNode(QStringLiteral("source"), QStringLiteral("Blue"), 0, 120);
    const QString blend = graph.addNode(QStringLiteral("blend"), QStringLiteral("Blend"), 240, 50,
                                        {{QStringLiteral("opacity"), 0.5}});
    const QString output = graph.addNode(QStringLiteral("output"), QStringLiteral("Output"), 480, 50);
    QVERIFY(!red.isEmpty() && !blue.isEmpty() && !blend.isEmpty() && !output.isEmpty());
    const QString first = graph.connectPorts(red, QStringLiteral("image"), blend,
                                             QStringLiteral("background"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!graph.connectPorts(blue, QStringLiteral("image"), blend,
                                QStringLiteral("foreground")).isEmpty());
    QVERIFY(!graph.connectPorts(blend, QStringLiteral("image"), output,
                                QStringLiteral("image")).isEmpty());
    const QString cycleA = graph.addNode(QStringLiteral("transform"), QStringLiteral("Cycle A"), 0, 250);
    const QString cycleB = graph.addNode(QStringLiteral("transform"), QStringLiteral("Cycle B"), 220, 250);
    QVERIFY(!graph.connectPorts(cycleA, QStringLiteral("image"), cycleB,
                                QStringLiteral("image")).isEmpty());
    QVERIFY(graph.connectPorts(cycleB, QStringLiteral("image"), cycleA,
                               QStringLiteral("image")).isEmpty());
    QCOMPARE(graph.lastError(), QStringLiteral("Unknown port, cycle or edge limit"));

    QImage redPixels(4, 4, QImage::Format_ARGB32_Premultiplied);
    redPixels.setColorSpace(QColorSpace(QColorSpace::SRgb));
    redPixels.fill(Qt::red);
    QImage bluePixels = redPixels;
    bluePixels.fill(Qt::blue);
    QVERIFY(graph.setSourceImage(red, redPixels));
    QVERIFY(graph.setSourceImage(blue, bluePixels));
    const QVariantMap firstRender = graph.render(output, 4, 4);
    QVERIFY2(firstRender.value(QStringLiteral("ok")).toBool(), qPrintable(graph.lastError()));
    const QString firstUrl = firstRender.value(QStringLiteral("imageDataUrl")).toString();
    QVERIFY(firstUrl.startsWith(QStringLiteral("data:image/png;base64,")));
    const QImage firstImage = QImage::fromData(QByteArray::fromBase64(firstUrl.mid(22).toLatin1()), "PNG");
    QVERIFY(!firstImage.isNull());
    const QColor pixel = firstImage.pixelColor(1, 1);
    QVERIFY(pixel.red() >= 126 && pixel.red() <= 129);
    QVERIFY(pixel.blue() >= 126 && pixel.blue() <= 129);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("graph.qtkgraph"));
    QVERIFY(graph.saveToFile(path));
    NodeGraphModel reopened;
    QVERIFY2(reopened.loadFromFile(path), qPrintable(reopened.lastError()));
    QCOMPARE(reopened.nodes().size(), 6);
    QCOMPARE(reopened.edges().size(), 4);
    QVERIFY(!reopened.render(output, 4, 4).value(QStringLiteral("ok")).toBool());
    QVERIFY(reopened.setSourceImage(red, redPixels));
    QVERIFY(reopened.setSourceImage(blue, bluePixels));
    QVERIFY(reopened.render(output, 4, 4).value(QStringLiteral("ok")).toBool());
    QVERIFY(reopened.disconnectEdge(first));
    QVERIFY(!reopened.render(output, 4, 4).value(QStringLiteral("ok")).toBool());
    QVERIFY(reopened.undo());
    QVERIFY(reopened.render(output, 4, 4).value(QStringLiteral("ok")).toBool());
}

void NodeGraphModelTest::solidColorNodeRenders()
{
    NodeGraphModel graph;
    const QString solid = graph.addNode(QStringLiteral("solid"), QStringLiteral("Teal"), 0, 0,
                                        {{QStringLiteral("color"), QStringLiteral("#007f99")}});
    const QString output = graph.addNode(QStringLiteral("output"), QStringLiteral("Output"), 200, 0);
    QVERIFY(!solid.isEmpty());
    QVERIFY(!output.isEmpty());
    QVERIFY(!graph.connectPorts(solid, QStringLiteral("image"), output,
                                QStringLiteral("image")).isEmpty());
    QVERIFY2(graph.render(output, 2, 2).value(QStringLiteral("ok")).toBool(),
             qPrintable(graph.lastError()));
    QCOMPARE(graph.frame().pixelColor(1, 1), QColor(QStringLiteral("#007f99")));
    QVERIFY(!graph.setParameters(solid, {{QStringLiteral("color"), QStringLiteral("not-a-color")}}));
}

QTEST_MAIN(NodeGraphModelTest)
#include "tst_node_graph_model.moc"
