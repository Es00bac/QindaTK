// SPDX-License-Identifier: LGPL-3.0-or-later
#include "cad_document.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace QindaTK;

class TstCadDocument : public QObject {
    Q_OBJECT
private slots:
    void referenceSketchConstraintsAndPersistence();
};

void TstCadDocument::referenceSketchConstraintsAndPersistence()
{
    CadDocument sketch;
    const QString a = sketch.addPoint(0, 0);
    const QString b = sketch.addPoint(101, 1);
    const QString c = sketch.addPoint(101, 61);
    const QString d = sketch.addPoint(0, 60);
    const QString center = sketch.addPoint(50, 30);
    const QString ab = sketch.addSegment(a, b);
    const QString bc = sketch.addSegment(b, c);
    const QString cd = sketch.addSegment(c, d);
    const QString da = sketch.addSegment(d, a);
    const QString circle = sketch.addCircle(center, 10);
    QVERIFY(!circle.isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("horizontal"), ab).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("vertical"), bc).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("horizontal"), cd).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("vertical"), da).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("distance"), a, b, 100).isEmpty());
    QVERIFY(!sketch.addConstraint(QStringLiteral("distance"), b, c, 60).isEmpty());
    const QString e = sketch.addPoint(200, 200), f = sketch.addPoint(205, 207);
    QVERIFY(!sketch.addConstraint(QStringLiteral("coincident"), e, f).isEmpty());
    const auto joined = sketch.points();
    QCOMPARE(joined.at(5).toMap().value(QStringLiteral("x")),
             joined.at(6).toMap().value(QStringLiteral("x")));
    QCOMPARE(joined.at(5).toMap().value(QStringLiteral("y")),
             joined.at(6).toMap().value(QStringLiteral("y")));
    QVERIFY(std::abs(sketch.measure(ab).value(QStringLiteral("length")).toDouble() - 100) < 1e-5);
    QVERIFY(std::abs(sketch.measure(bc).value(QStringLiteral("length")).toDouble() - 60) < 1e-5);
    QCOMPARE(sketch.measure(circle).value(QStringLiteral("area")).toDouble(),
             3.14159265358979323846 * 100);
    const QVariantMap nearCorner = sketch.snap(0.1, 0.1, 3, 10);
    QCOMPARE(nearCorner.value(QStringLiteral("kind")).toString(), QStringLiteral("point"));
    QCOMPARE(nearCorner.value(QStringLiteral("pointId")).toString(), a);
    const QVariantMap grid = sketch.snap(20.2, 9.9, 1, 10);
    QCOMPARE(grid.value(QStringLiteral("kind")).toString(), QStringLiteral("grid"));
    QCOMPARE(grid.value(QStringLiteral("x")).toDouble(), 20.0);
    QCOMPARE(grid.value(QStringLiteral("y")).toDouble(), 10.0);

    const QVariantList stablePoints = sketch.points();
    const qulonglong beforeRejected = sketch.revision();
    QVERIFY(sketch.addConstraint(QStringLiteral("vertical"), ab).isEmpty());
    QVERIFY(sketch.lastError().contains(QStringLiteral("Unsatisfiable")));
    QCOMPARE(sketch.points(), stablePoints);
    QCOMPARE(sketch.revision(), beforeRejected);
    QVERIFY(sketch.addSegment(a, a).isEmpty());
    QVERIFY(sketch.addCircle(center, 0).isEmpty());

    QVERIFY(sketch.movePoint(a, 10, 10));
    const QVariantList movedPoints = sketch.points();
    QVERIFY(std::abs(sketch.measure(ab).value(QStringLiteral("length")).toDouble() - 100) < 1e-5);
    QVERIFY(sketch.undo());
    QCOMPARE(sketch.points(), stablePoints);
    QVERIFY(sketch.redo());
    QCOMPARE(sketch.points(), movedPoints);

    QVERIFY(sketch.changeUnits(QStringLiteral("in")));
    QVERIFY(std::abs(sketch.measure(ab).value(QStringLiteral("length")).toDouble()
                     - 100.0 / 25.4) < 1e-6);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("reference.qcad"));
    QVERIFY(sketch.saveToFile(path));
    CadDocument reopened;
    QVERIFY(reopened.loadFromFile(path));
    QCOMPARE(reopened.units(), QStringLiteral("in"));
    QCOMPARE(reopened.points(), sketch.points());
    QCOMPARE(reopened.segments(), sketch.segments());
    QCOMPARE(reopened.circles(), sketch.circles());
    QCOMPARE(reopened.constraints(), sketch.constraints());
    QVERIFY(!reopened.canUndo());

    QFile broken(path);
    QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
    broken.write("{\"format\":\"qindatk.cad/1\",\"version\":1,\"units\":\"mm\","
                 "\"points\":[],\"segments\":[{\"id\":\"dangling\",\"start\":\"x\",\"end\":\"y\"}],"
                 "\"circles\":[],\"constraints\":[]}");
    broken.close();
    const QVariantList beforeBadLoad = reopened.points();
    QVERIFY(!reopened.loadFromFile(path));
    QCOMPARE(reopened.points(), beforeBadLoad);
    QVERIFY(reopened.removeEntity(a));
    QCOMPARE(reopened.segments().size(), 2);
    QVERIFY(reopened.undo());
    QCOMPARE(reopened.segments(), sketch.segments());
}

QTEST_MAIN(TstCadDocument)
#include "tst_cad_document.moc"
