// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-083 / EXT-03: persistent groups, align/distribute and transactions on
// DiagramDocument, plus the shared transaction contract with CadDocument.
#include "cad_document.h"
#include "diagram_document.h"

#include <QFile>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaTK;

class TstDiagramCommands : public QObject {
    Q_OBJECT
private slots:
    void groupsPersistAndFollowRemoval();
    void alignAndDistributeAreOneStep();
    void stencilTransactionIsOneStep();
    void transactionContractIsShared();
};

namespace {
QVariantMap nodeRow(const DiagramDocument &doc, const QString &id)
{
    for (const QVariant &value : doc.nodes())
        if (value.toMap().value(QStringLiteral("nodeId")).toString() == id) return value.toMap();
    return {};
}
double x(const DiagramDocument &doc, const QString &id) { return nodeRow(doc, id).value(QStringLiteral("x")).toDouble(); }
double y(const DiagramDocument &doc, const QString &id) { return nodeRow(doc, id).value(QStringLiteral("y")).toDouble(); }
}

void TstDiagramCommands::groupsPersistAndFollowRemoval()
{
    DiagramDocument doc;
    const QString a = doc.addNode(QStringLiteral("A"), 0, 0);
    const QString b = doc.addNode(QStringLiteral("B"), 300, 0);
    const QString c = doc.addNode(QStringLiteral("C"), 0, 200);
    const qulonglong revision = doc.revision();
    const QString group = doc.addGroup({a, b, a}, QStringLiteral("Intake"));
    QVERIFY(!group.isEmpty());
    QCOMPARE(doc.revision(), revision + 1);
    QCOMPARE(doc.groups().size(), 1);
    const QVariantMap row = doc.groups().at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("groupId")).toString(), group);
    QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Intake"));
    QCOMPARE(row.value(QStringLiteral("nodeIds")).toStringList(), (QStringList{a, b}));
    // A card belongs to one group; unknown cards and blank titles are refused.
    QVERIFY(doc.addGroup({b, c}).isEmpty());
    QVERIFY(doc.lastError().contains(QStringLiteral("Intake")));
    QVERIFY(doc.addGroup({QStringLiteral("missing")}).isEmpty());
    QVERIFY(doc.addGroup({c}, QStringLiteral(" ")).isEmpty());
    QCOMPARE(doc.revision(), revision + 1);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("groups.qdiagram"));
    QVERIFY(doc.saveToFile(path));
    DiagramDocument reopened;
    QVERIFY(reopened.loadFromFile(path));
    QCOMPARE(reopened.groups(), doc.groups());

    // Removing a member prunes it; removing the last member drops the group.
    QVERIFY(reopened.removeNode(a));
    QCOMPARE(reopened.groups().at(0).toMap().value(QStringLiteral("nodeIds")).toStringList(),
             QStringList{b});
    QVERIFY(reopened.removeNodes({b}));
    QVERIFY(reopened.groups().isEmpty());
    QVERIFY(reopened.undo());
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.groups(), doc.groups());
    // Ungroup keeps the cards.
    QVERIFY(reopened.removeGroup(group));
    QVERIFY(reopened.groups().isEmpty());
    QCOMPARE(reopened.nodes().size(), 3);
    QVERIFY(!reopened.removeGroup(group));
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.groups().size(), 1);

    // A grouped file with a member in two groups is invalid and atomic.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray bytes = file.readAll();
    file.close();
    bytes.replace("\"groups\": [", QByteArray("\"groups\": [{\"id\":\"dup\",\"title\":\"Dup\",\"nodes\":[\"")
                  + a.toUtf8() + "\"]},");
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
    file.close();
    DiagramDocument rejected;
    QVERIFY(!rejected.loadFromFile(path));
    QVERIFY(rejected.nodes().isEmpty());

    // Files without groups keep their version-1 shape (no "groups" key).
    DiagramDocument plain;
    plain.addNode(QStringLiteral("Solo"), 0, 0);
    const QString plainPath = directory.filePath(QStringLiteral("plain.qdiagram"));
    QVERIFY(plain.saveToFile(plainPath));
    QFile plainFile(plainPath);
    QVERIFY(plainFile.open(QIODevice::ReadOnly));
    QVERIFY(!plainFile.readAll().contains("groups"));
}

void TstDiagramCommands::alignAndDistributeAreOneStep()
{
    DiagramDocument doc;
    const QString a = doc.addNode(QStringLiteral("A"), 10, 40);
    const QString b = doc.addNode(QStringLiteral("B"), 70, 10);
    const QString c = doc.addNode(QStringLiteral("C"), 400, 100);
    const QString d = doc.addNode(QStringLiteral("D"), 130, 90);
    const QStringList all{a, b, c, d};
    const qulonglong revision = doc.revision();
    QVERIFY(doc.alignNodes(all, QStringLiteral("top")));
    QCOMPARE(doc.revision(), revision + 1);
    for (const QString &id : all) QCOMPARE(y(doc, id), 10.0);
    QCOMPARE(x(doc, c), 400.0); // the other axis is untouched
    QVERIFY(doc.alignNodes(all, QStringLiteral("top"))); // already aligned: no step
    QCOMPARE(doc.revision(), revision + 1);
    QVERIFY(doc.undo());
    QCOMPARE(y(doc, a), 40.0);
    QCOMPARE(y(doc, c), 100.0);
    QVERIFY(doc.alignNodes({a, c}, QStringLiteral("right")));
    QCOMPARE(x(doc, a), 400.0);
    QVERIFY(doc.undo());
    QVERIFY(doc.alignNodes({a, c}, QStringLiteral("centerX")));
    QCOMPARE(x(doc, a), 205.0);
    QCOMPARE(x(doc, c), 205.0);
    QVERIFY(doc.undo());
    QVERIFY(doc.alignNodes({b, c}, QStringLiteral("bottom")));
    QCOMPARE(y(doc, b), 100.0);
    QVERIFY(doc.undo());
    QVERIFY(doc.alignNodes({a, b}, QStringLiteral("centerY")));
    QCOMPARE(y(doc, a), 25.0);
    QVERIFY(doc.undo());

    // Distribute: A (10) and C (400) stay; B and D get 130 px steps in x order.
    QVERIFY(doc.distributeNodes(all, QStringLiteral("horizontal")));
    QCOMPARE(x(doc, a), 10.0);
    QCOMPARE(x(doc, b), 140.0);
    QCOMPARE(x(doc, d), 270.0);
    QCOMPARE(x(doc, c), 400.0);
    QCOMPARE(y(doc, b), 10.0);
    QVERIFY(doc.undo());
    QCOMPARE(x(doc, b), 70.0);
    QVERIFY(doc.distributeNodes(all, QStringLiteral("vertical")));
    QCOMPARE(y(doc, b), 10.0);   // top
    QCOMPARE(y(doc, a), 40.0);   // 10 + 30
    QCOMPARE(y(doc, d), 70.0);
    QCOMPARE(y(doc, c), 100.0);  // bottom
    // Invalid requests change nothing.
    const qulonglong settled = doc.revision();
    QVERIFY(!doc.alignNodes({a}, QStringLiteral("left")));
    QVERIFY(!doc.alignNodes(all, QStringLiteral("diagonal")));
    QVERIFY(!doc.distributeNodes({a, b}, QStringLiteral("horizontal")));
    QVERIFY(!doc.distributeNodes(all, QStringLiteral("radial")));
    QVERIFY(!doc.alignNodes({a, QStringLiteral("missing")}, QStringLiteral("left")));
    QCOMPARE(doc.revision(), settled);
}

void TstDiagramCommands::stencilTransactionIsOneStep()
{
    DiagramDocument doc;
    const QString seed = doc.addNode(QStringLiteral("Seed"), 0, 0);
    QVERIFY(doc.beginTransaction());
    const QString card = doc.addNode(QStringLiteral("Filter"), 200, 0, QStringLiteral("filter"));
    QVERIFY(!doc.addPort(card, QStringLiteral("In"), QStringLiteral("input")).isEmpty());
    QVERIFY(!doc.addPort(card, QStringLiteral("Out"), QStringLiteral("output")).isEmpty());
    QVERIFY(!doc.undo());
    QVERIFY(doc.commitTransaction());
    QCOMPARE(doc.nodes().size(), 2);
    QVERIFY(doc.undo());
    QCOMPARE(doc.nodes().size(), 1);
    QVERIFY(doc.redo());
    QCOMPARE(nodeRow(doc, card).value(QStringLiteral("outputs")).toList().size(), 1);

    // A failed port rolls the whole card back.
    const QVariantList before = doc.nodes();
    QVERIFY(doc.beginTransaction());
    const QString broken = doc.addNode(QStringLiteral("Broken"), 0, 300);
    QVERIFY(doc.addPort(broken, QString(), QStringLiteral("input")).isEmpty());
    QVERIFY(doc.cancelTransaction());
    QCOMPARE(doc.nodes(), before);
    QCOMPARE(doc.lastError(), QStringLiteral("Invalid diagram port"));
    QVERIFY(doc.undo());
    QCOMPARE(doc.nodes().size(), 1);
    QCOMPARE(doc.nodes().at(0).toMap().value(QStringLiteral("nodeId")).toString(), seed);
}

// D-283: both domain documents expose the same transaction members.
void TstDiagramCommands::transactionContractIsShared()
{
    DiagramDocument diagram;
    CadDocument cad;
    for (QObject *document : {static_cast<QObject *>(&diagram), static_cast<QObject *>(&cad)}) {
        const qulonglong revision = document->property("revision").toULongLong();
        bool ok = false;
        QVERIFY(QMetaObject::invokeMethod(document, "beginTransaction", Q_RETURN_ARG(bool, ok)));
        QVERIFY(ok);
        if (document == &cad) {
            QString id;
            QVERIFY(QMetaObject::invokeMethod(document, "addPoint", Q_RETURN_ARG(QString, id),
                                              Q_ARG(double, 1), Q_ARG(double, 2)));
            QVERIFY(QMetaObject::invokeMethod(document, "addPoint", Q_RETURN_ARG(QString, id),
                                              Q_ARG(double, 3), Q_ARG(double, 4)));
        } else {
            QString id;
            QVERIFY(QMetaObject::invokeMethod(document, "addNode", Q_RETURN_ARG(QString, id),
                                              Q_ARG(QString, QStringLiteral("A")), Q_ARG(double, 0),
                                              Q_ARG(double, 0), Q_ARG(QString, QStringLiteral("p"))));
            QVERIFY(QMetaObject::invokeMethod(document, "addNode", Q_RETURN_ARG(QString, id),
                                              Q_ARG(QString, QStringLiteral("B")), Q_ARG(double, 9),
                                              Q_ARG(double, 0), Q_ARG(QString, QStringLiteral("p"))));
        }
        QVERIFY(QMetaObject::invokeMethod(document, "commitTransaction", Q_RETURN_ARG(bool, ok)));
        QVERIFY(ok);
        QVERIFY(document->property("revision").toULongLong() > revision);
        QVERIFY(QMetaObject::invokeMethod(document, "undo", Q_RETURN_ARG(bool, ok)));
        QVERIFY(ok);
        QVERIFY(!document->property("canUndo").toBool()); // both edits in one step
        QVERIFY(QMetaObject::invokeMethod(document, "cancelTransaction", Q_RETURN_ARG(bool, ok)));
        QVERIFY(!ok); // nothing open
    }
}

QTEST_MAIN(TstDiagramCommands)
#include "tst_diagram_commands.moc"
