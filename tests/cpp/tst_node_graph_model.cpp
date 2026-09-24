// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/node_graph_model.h"

#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString kImage = QStringLiteral("image");

QImage renderedFrame(NodeGraphModel &graph, const QString &output, int width, int height)
{
    const QVariantMap result = graph.render(output, width, height);
    if (!result.value(QStringLiteral("ok")).toBool()) return {};
    return graph.frame();
}

QStringList evaluated(NodeGraphModel &graph, const QString &output)
{
    QStringList ids = graph.render(output, 8, 8).value(QStringLiteral("evaluatedNodeIds")).toStringList();
    ids.sort();
    return ids;
}

// Red background, blue foreground cut by a mask over the left half.
struct MaskedScene {
    NodeGraphModel graph;
    QString red, blue, mask, blend, output;
    MaskedScene()
    {
        red = graph.addNode(QStringLiteral("solid"), QStringLiteral("Red"), 0, 0,
                            {{QStringLiteral("color"), QStringLiteral("#ff0000")}});
        blue = graph.addNode(QStringLiteral("solid"), QStringLiteral("Blue"), 0, 100,
                             {{QStringLiteral("color"), QStringLiteral("#0000ff")}});
        mask = graph.addNode(QStringLiteral("mask"), QStringLiteral("Left"), 0, 200,
                             {{QStringLiteral("width"), 4}, {QStringLiteral("height"), 8}});
        blend = graph.addNode(QStringLiteral("blend"), QStringLiteral("Blend"), 200, 100);
        output = graph.addNode(QStringLiteral("output"), QStringLiteral("Output"), 400, 100);
        graph.connectPorts(red, kImage, blend, QStringLiteral("background"));
        graph.connectPorts(blue, kImage, blend, QStringLiteral("foreground"));
        graph.connectPorts(mask, QStringLiteral("mask"), blend, QStringLiteral("mask"));
        graph.connectPorts(blend, kImage, output, kImage);
    }
};
}

class NodeGraphModelTest : public QObject {
    Q_OBJECT
private slots:
    void graphRendersAndReopens();
    void solidColorNodeRenders();
    void typedPortsRejectIncompatibleLinks();
    void maskedBlendModesAreDeterministic();
    void incrementalInvalidationEvaluatesOnlyDownstream();
    void deletedNodesDropPortsAndStaleRequests();
    void transactionsCommitOneStepAndCancelRestores();
    void groupsMoveUndoAndPersistWithVersions();
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
    QCOMPARE(graph.lastError(), QStringLiteral("Connection would create a cycle"));

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

void NodeGraphModelTest::typedPortsRejectIncompatibleLinks()
{
    NodeGraphModel graph;
    const QString solid = graph.addNode(QStringLiteral("solid"), {}, 0, 0);
    const QString mask = graph.addNode(QStringLiteral("mask"), {}, 0, 100);
    const QString blend = graph.addNode(QStringLiteral("blend"), {}, 200, 0);
    const QString output = graph.addNode(QStringLiteral("output"), {}, 400, 0);
    const qulonglong revision = graph.revision();
    QVERIFY(graph.connectPorts(mask, QStringLiteral("mask"), output, kImage).isEmpty());
    QCOMPARE(graph.lastError(), QStringLiteral("Incompatible ports: mask output cannot connect to image input"));
    QVERIFY(graph.connectPorts(solid, kImage, blend, QStringLiteral("mask")).isEmpty());
    QVERIFY(graph.connectPorts(solid, kImage, blend, QStringLiteral("matte")).isEmpty());
    QCOMPARE(graph.lastError(), QStringLiteral("Unknown node or port"));
    QVERIFY(graph.connectPorts(output, kImage, blend, QStringLiteral("background")).isEmpty());
    QCOMPARE(graph.revision(), revision);
    QVERIFY(graph.edges().isEmpty());
    QVERIFY(!graph.connectPorts(mask, QStringLiteral("mask"), blend, QStringLiteral("mask")).isEmpty());
    QVERIFY(!graph.connectPorts(solid, kImage, blend, QStringLiteral("background")).isEmpty());
    QVERIFY(graph.connectPorts(solid, kImage, blend, QStringLiteral("background")).isEmpty());
    QCOMPARE(graph.lastError(), QStringLiteral("Input port already connected"));

    // Parameter identity: descriptors name every parameter with its range.
    QVariantMap blendType;
    for (const QVariant &type : NodeGraphModel::nodeTypes())
        if (type.toMap().value(QStringLiteral("type")) == QStringLiteral("blend")) blendType = type.toMap();
    const QVariantList inputs = blendType.value(QStringLiteral("inputs")).toList();
    QCOMPARE(inputs.size(), 3);
    QCOMPARE(inputs.at(2).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("mask"));
    QVERIFY(inputs.at(2).toMap().value(QStringLiteral("optional")).toBool());
    const QVariantMap mode = blendType.value(QStringLiteral("parameters")).toList().at(1).toMap();
    QCOMPARE(mode.value(QStringLiteral("parameterId")).toString(), QStringLiteral("mode"));
    QVERIFY(mode.value(QStringLiteral("options")).toStringList().contains(QStringLiteral("multiply")));
    QVERIFY(!graph.setParameter(blend, QStringLiteral("mode"), QStringLiteral("dissolve")));
    QVERIFY(!graph.setParameter(blend, QStringLiteral("opacity"), 1.5));
    QVERIFY(!graph.setParameter(blend, QStringLiteral("gain"), 1));
    QVERIFY(graph.setParameter(blend, QStringLiteral("mode"), QStringLiteral("screen")));
}

void NodeGraphModelTest::maskedBlendModesAreDeterministic()
{
    MaskedScene first;
    const QImage image = renderedFrame(first.graph, first.output, 8, 8);
    QVERIFY2(!image.isNull(), qPrintable(first.graph.lastError()));
    QCOMPARE(image.pixelColor(1, 4), QColor(QStringLiteral("#0000ff")));
    QCOMPARE(image.pixelColor(6, 4), QColor(QStringLiteral("#ff0000")));

    MaskedScene second;
    QCOMPARE(renderedFrame(second.graph, second.output, 8, 8), image);
    QCOMPARE(second.graph.imageDataUrl(), first.graph.imageDataUrl());
    QVERIFY(second.graph.setParameter(second.blend, QStringLiteral("mode"), QStringLiteral("add")));
    QCOMPARE(renderedFrame(second.graph, second.output, 8, 8).pixelColor(1, 4),
             QColor(QStringLiteral("#ff00ff")));
    QVERIFY(second.graph.setParameter(second.blend, QStringLiteral("mode"), QStringLiteral("multiply")));
    QCOMPARE(renderedFrame(second.graph, second.output, 8, 8).pixelColor(1, 4),
             QColor(QStringLiteral("#000000")));
    QVERIFY(second.graph.setParameter(second.blend, QStringLiteral("mode"), QStringLiteral("normal")));
    QCOMPARE(renderedFrame(second.graph, second.output, 8, 8), image);

    // Transform: turning the half-blue frame 180 degrees about the canvas
    // centre swaps its halves.
    const QString turn = second.graph.addNode(QStringLiteral("transform"), {}, 300, 200,
                                              {{QStringLiteral("rotation"), 180}});
    QVERIFY(second.graph.disconnectEdge(second.graph.edges().at(3).toMap()
                                        .value(QStringLiteral("edgeId")).toString()));
    QVERIFY(!second.graph.connectPorts(second.blend, kImage, turn, kImage).isEmpty());
    QVERIFY(!second.graph.connectPorts(turn, kImage, second.output, kImage).isEmpty());
    const QImage rotated = renderedFrame(second.graph, second.output, 8, 8);
    QVERIFY2(!rotated.isNull(), qPrintable(second.graph.lastError()));
    const auto near = [](const QColor &actual, const QColor &expected) {
        return std::abs(actual.red() - expected.red()) <= 2
            && std::abs(actual.blue() - expected.blue()) <= 2 && actual.alpha() >= 253;
    };
    QVERIFY(near(rotated.pixelColor(1, 4), QColor(QStringLiteral("#ff0000"))));
    QVERIFY(near(rotated.pixelColor(6, 4), QColor(QStringLiteral("#0000ff"))));
}

void NodeGraphModelTest::incrementalInvalidationEvaluatesOnlyDownstream()
{
    MaskedScene scene;
    NodeGraphModel &graph = scene.graph;
    QStringList all{scene.red, scene.blue, scene.mask, scene.blend, scene.output};
    all.sort();
    QCOMPARE(evaluated(graph, scene.output), all);
    QCOMPARE(evaluated(graph, scene.output), QStringList());
    QVERIFY(graph.moveNode(scene.blue, 40, 140));
    QCOMPARE(evaluated(graph, scene.output), QStringList());
    QVERIFY(graph.setParameter(scene.blue, QStringLiteral("color"), QStringLiteral("#00ff00")));
    QStringList downstream{scene.blue, scene.blend, scene.output};
    downstream.sort();
    QCOMPARE(evaluated(graph, scene.output), downstream);
    QVERIFY(graph.setParameter(scene.mask, QStringLiteral("shape"), QStringLiteral("ellipse")));
    QStringList maskPath{scene.mask, scene.blend, scene.output};
    maskPath.sort();
    QCOMPARE(evaluated(graph, scene.output), maskPath);
}

void NodeGraphModelTest::deletedNodesDropPortsAndStaleRequests()
{
    MaskedScene scene;
    NodeGraphModel &graph = scene.graph;
    QVERIFY(!graph.groupNodes({scene.blend, scene.output}, QStringLiteral("Result")).isEmpty());
    QVERIFY(graph.removeNode(scene.blend));
    QCOMPARE(graph.edges().size(), 0);
    QCOMPARE(graph.groups().first().toMap().value(QStringLiteral("nodeIds")).toStringList(),
             QStringList{scene.output});
    const qulonglong revision = graph.revision();
    QVERIFY(graph.connectPorts(scene.red, kImage, scene.blend, QStringLiteral("background")).isEmpty());
    QVERIFY(!graph.moveNode(scene.blend, 1, 1));
    QVERIFY(!graph.setParameter(scene.blend, QStringLiteral("opacity"), 0.5));
    QCOMPARE(graph.revision(), revision);
    QVERIFY(!graph.render(scene.output, 8, 8).value(QStringLiteral("ok")).toBool());
    QCOMPARE(graph.lastError(), QStringLiteral("Unconnected image input on Output"));
    QVERIFY(graph.undo());
    QCOMPARE(graph.edges().size(), 4);
    QVERIFY2(!renderedFrame(graph, scene.output, 8, 8).isNull(), qPrintable(graph.lastError()));

    // A saved edge that names a port the type no longer has is rejected
    // before the live graph changes.
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("stale.qtkgraph"));
    QVERIFY(graph.saveToFile(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonArray edges = root.value(QStringLiteral("edges")).toArray();
    QJsonObject edge = edges.at(0).toObject();
    edge.insert(QStringLiteral("toPortId"), QStringLiteral("matte"));
    edges.replace(0, edge);
    root.insert(QStringLiteral("edges"), edges);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
    file.close();
    const QVariantList before = graph.nodes();
    QVERIFY(!graph.loadFromFile(path));
    QCOMPARE(graph.lastError(), QStringLiteral("Invalid edge record, port or duplicate input"));
    QCOMPARE(graph.nodes(), before);
    QCOMPARE(graph.edges().size(), 4);
}

void NodeGraphModelTest::transactionsCommitOneStepAndCancelRestores()
{
    MaskedScene scene;
    NodeGraphModel &graph = scene.graph;
    QVERIFY(!renderedFrame(graph, scene.output, 8, 8).isNull());
    const QVariantList before = graph.nodes();
    QVERIFY(graph.beginEdit());
    QVERIFY(graph.editing());
    QVERIFY(!graph.canUndo());
    QVERIFY(!graph.beginEdit());
    for (double opacity : {0.2, 0.4, 0.6})
        QVERIFY(graph.setParameter(scene.blend, QStringLiteral("opacity"), opacity));
    QVERIFY(graph.moveNode(scene.blend, 220, 120));
    QVERIFY(!graph.undo());
    QVERIFY(graph.commitEdit());
    QVERIFY(!graph.editing());
    QVERIFY(graph.undo());
    QCOMPARE(graph.nodes(), before);
    QVERIFY(graph.canRedo());

    QVERIFY(graph.beginEdit());
    QVERIFY(graph.setParameter(scene.blend, QStringLiteral("opacity"), 0.1));
    QVERIFY(graph.removeNode(scene.mask));
    QVERIFY(graph.cancelEdit());
    QCOMPARE(graph.nodes(), before);
    QCOMPARE(graph.edges().size(), 4);
    QVERIFY(graph.canRedo());
    QVERIFY2(!renderedFrame(graph, scene.output, 8, 8).isNull(), qPrintable(graph.lastError()));

    // An unchanged gesture adds no history entry.
    QVERIFY(graph.beginEdit());
    QVERIFY(graph.commitEdit());
    QVERIFY(graph.redo());
    QCOMPARE(graph.nodes().at(3).toMap().value(QStringLiteral("x")).toDouble(), 220.0);
}

void NodeGraphModelTest::groupsMoveUndoAndPersistWithVersions()
{
    MaskedScene scene;
    NodeGraphModel &graph = scene.graph;
    const QString group = graph.groupNodes({scene.red, scene.blue}, QStringLiteral("Plates"));
    QVERIFY(!group.isEmpty());
    QVERIFY(graph.groupNodes({scene.blue, scene.mask}).isEmpty());
    QVERIFY(graph.groupNodes({scene.mask, scene.mask}).isEmpty());
    QVERIFY(graph.moveGroup(group, 30, -10));
    QCOMPARE(graph.nodes().at(0).toMap().value(QStringLiteral("x")).toDouble(), 30.0);
    QCOMPARE(graph.nodes().at(1).toMap().value(QStringLiteral("y")).toDouble(), 90.0);
    QVERIFY(graph.undo());
    QCOMPARE(graph.nodes().at(1).toMap().value(QStringLiteral("y")).toDouble(), 100.0);
    QVERIFY(graph.redo());

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("groups.qtkgraph"));
    QVERIFY(graph.saveToFile(path));
    NodeGraphModel reopened;
    QVERIFY2(reopened.loadFromFile(path), qPrintable(reopened.lastError()));
    QCOMPARE(reopened.groups(), graph.groups());
    QCOMPARE(reopened.nodes(), graph.nodes());
    QCOMPARE(reopened.revision(), graph.revision());
    QVERIFY(reopened.ungroup(group));
    QVERIFY(reopened.groups().isEmpty());
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.groups(), graph.groups());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
    const auto writeVariant = [&](int version, bool withGroups) {
        QJsonObject copy = root;
        copy.insert(QStringLiteral("version"), version);
        if (!withGroups) copy.remove(QStringLiteral("groups"));
        QFile out(path);
        return out.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && out.write(QJsonDocument(copy).toJson()) > 0;
    };
    QVERIFY(writeVariant(1, false));
    NodeGraphModel legacy;
    QVERIFY2(legacy.loadFromFile(path), qPrintable(legacy.lastError()));
    QCOMPARE(legacy.nodes().size(), 5);
    QVERIFY(legacy.groups().isEmpty());
    QVERIFY(writeVariant(3, true));
    QVERIFY(!legacy.loadFromFile(path));
    QCOMPARE(legacy.lastError(), QStringLiteral("Unsupported graph format or version"));
    QCOMPARE(legacy.nodes().size(), 5);
    QVERIFY(writeVariant(2, false));
    QVERIFY(!legacy.loadFromFile(path));
}

QTEST_MAIN(NodeGraphModelTest)
#include "tst_node_graph_model.moc"
